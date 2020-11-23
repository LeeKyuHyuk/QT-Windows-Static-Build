// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/raw_clipboard_host_impl.h"

#include "base/bind.h"
#include "content/browser/frame_host/clipboard_host_impl.h"
#include "content/browser/permissions/permission_controller_impl.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/child_process_host.h"
#include "ipc/ipc_message.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/clipboard/raw_clipboard.mojom.h"
#include "third_party/blink/public/mojom/permissions/permission_status.mojom-shared.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"

namespace content {

void RawClipboardHostImpl::Create(
    RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<blink::mojom::RawClipboardHost> receiver) {
  DCHECK(render_frame_host);

  PermissionControllerImpl* permission_controller =
      PermissionControllerImpl::FromBrowserContext(
          render_frame_host->GetProcess()->GetBrowserContext());

  // Feature flags and permission should already be checked in the renderer
  // process, but recheck in the browser process in case of a hijacked renderer.
  if (!base::FeatureList::IsEnabled(blink::features::kRawClipboard)) {
    mojo::ReportBadMessage("Raw Clipboard is not enabled");
    return;
  }

  blink::mojom::PermissionStatus status =
      permission_controller->GetPermissionStatusForFrame(
          PermissionType::CLIPBOARD_READ_WRITE, render_frame_host,
          render_frame_host->GetLastCommittedOrigin().GetURL());

  if (status != blink::mojom::PermissionStatus::GRANTED) {
    // This may be hit by a race condition, where permission is denied after
    // the renderer check, but before the browser check. It may also be hit by
    // a compromised renderer.
    return;
  }

  // Clipboard implementations do interesting things, like run nested message
  // loops. Use manual memory management instead of SelfOwnedReceiver<T> which
  // synchronously destroys on failure and can result in some unfortunate
  // use-after-frees after the nested message loops exit.
  auto* host = new RawClipboardHostImpl(std::move(receiver));
  host->receiver_.set_disconnect_handler(base::BindOnce(
      [](RawClipboardHostImpl* host) {
        base::SequencedTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE, host);
      },
      host));
}

RawClipboardHostImpl::~RawClipboardHostImpl() {
  clipboard_writer_->Reset();
}

RawClipboardHostImpl::RawClipboardHostImpl(
    mojo::PendingReceiver<blink::mojom::RawClipboardHost> receiver)
    : receiver_(this, std::move(receiver)),
      clipboard_(ui::Clipboard::GetForCurrentThread()),
      clipboard_writer_(
          new ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)) {}

void RawClipboardHostImpl::ReadAvailableFormatNames(
    ReadAvailableFormatNamesCallback callback) {
  std::vector<base::string16> raw_types =
      clipboard_->ReadAvailablePlatformSpecificFormatNames(
          ui::ClipboardBuffer::kCopyPaste);
  std::move(callback).Run(raw_types);
}

void RawClipboardHostImpl::Write(const base::string16& format,
                                 mojo_base::BigBuffer data) {
  // Windows / X11 clipboards enter an unrecoverable state after registering
  // some amount of unique formats, and there's no way to un-register these
  // formats. For these clipboards, use a conservative limit to avoid
  // registering too many formats, as:
  // (1) Other native applications may also register clipboard formats.
  // (2) |registered_formats| only persists over one Chrome Clipboard session.
  // (3) Chrome also registers other clipboard formats.
  //
  // The limit is based on Windows, which has the smallest limit, at 0x4000.
  // Windows represents clipboard formats using values in 0xC000 - 0xFFFF.
  // Therefore, Windows supports at most 0x4000 registered formats. Reference:
  // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclipboardformata
  static constexpr int kMaxWindowsClipboardFormats = 0x4000;
  static constexpr int kMaxRegisteredFormats = kMaxWindowsClipboardFormats / 4;
  static base::NoDestructor<std::set<base::string16>> registered_formats;
  if (!base::Contains(*registered_formats, format)) {
    if (registered_formats->size() >= kMaxRegisteredFormats)
      return;
    registered_formats->emplace(format);
  }

  clipboard_writer_->WriteData(format, std::move(data));
}

void RawClipboardHostImpl::CommitWrite() {
  clipboard_writer_.reset(
      new ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste));
}

}  // namespace content
