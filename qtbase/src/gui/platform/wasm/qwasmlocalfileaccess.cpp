/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwasmlocalfileaccess_p.h"
#include <private/qstdweb_p.h>
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/html5.h>
#include <emscripten/val.h>

QT_BEGIN_NAMESPACE

namespace QWasmLocalFileAccess {

void streamFile(const qstdweb::File &file, uint32_t offset, uint32_t length, char *buffer, const std::function<void ()> &completed)
{
    // Read file in chunks in order to avoid holding two copies in memory at the same time
    const uint32_t chunkSize = 256 * 1024;
    const uint32_t end = offset + length;
    // assert end < file.size
    auto fileReader = std::make_shared<qstdweb::FileReader>();

    auto chunkCompleted = std::make_shared<std::function<void (uint32_t, char *buffer)>>();
    *chunkCompleted = [=](uint32_t chunkBegin, char *chunkBuffer) mutable {

        // Copy current chunk from JS memory to Wasm memory
        qstdweb::ArrayBuffer result = fileReader->result();
        qstdweb::Uint8Array(result).copyTo(chunkBuffer);

        // Read next chunk if not at buffer end
        uint32_t nextChunkBegin = std::min(chunkBegin + result.byteLength(), end);
        uint32_t nextChunkEnd = std::min(nextChunkBegin + chunkSize, end);
        if (nextChunkBegin == end) {
            completed();
            chunkCompleted.reset();
            return;
        }
        char *nextChunkBuffer = chunkBuffer + result.byteLength();
        fileReader->onLoad([=]() { (*chunkCompleted)(nextChunkBegin, nextChunkBuffer); });
        qstdweb::Blob blob = file.slice(nextChunkBegin, nextChunkEnd);
        fileReader->readAsArrayBuffer(blob);
    };

    // Read first chunk. First iteration is a dummy iteration with no available data.
    (*chunkCompleted)(offset, buffer);
}

void streamFile(const qstdweb::File &file, char *buffer, const std::function<void ()> &completed)
{
    streamFile(file, 0, file.size(), buffer, completed);
}

void readFiles(const qstdweb::FileList &fileList,
    const std::function<char *(uint64_t size, const std::string name)> &acceptFile,
    const std::function<void ()> &fileDataReady)
{
    auto readFile = std::make_shared<std::function<void(int)>>();

    *readFile = [=](int fileIndex) mutable {
        // Stop when all files have been processed
        if (fileIndex >= fileList.length()) {
            readFile.reset();
            return;
        }

        const qstdweb::File file = fileList[fileIndex];

        // Ask caller if the file should be accepted
        char *buffer = acceptFile(file.size(), file.name());
        if (buffer == nullptr) {
            (*readFile)(fileIndex + 1);
            return;
        }

        // Read file data into caller-provided buffer
        streamFile(file, buffer, [=]() {
            fileDataReady();
            (*readFile)(fileIndex + 1);
        });
    };

    (*readFile)(0);
}

typedef std::function<void (const qstdweb::FileList &fileList)> OpenFileDialogCallback;
void openFileDialog(const std::string &accept, FileSelectMode fileSelectMode,
                    const OpenFileDialogCallback &filesSelected)
{
    // Create file input html element which will display a native file dialog
    // and call back to our onchange handler once the user has selected
    // one or more files.
    emscripten::val document = emscripten::val::global("document");
    emscripten::val input = document.call<emscripten::val>("createElement", std::string("input"));
    input.set("type", "file");
    input.set("style", "display:none");
    input.set("accept", emscripten::val(accept));
    input.set("multiple", emscripten::val(fileSelectMode == MultipleFiles));

    // Note: there is no event in case the user cancels the file dialog.
    static std::unique_ptr<qstdweb::EventCallback> changeEvent;
    auto callback = [=]() { filesSelected(qstdweb::FileList(input["files"])); };
    changeEvent.reset(new qstdweb::EventCallback(input, "change", callback));

    // Activate file input
    emscripten::val body = document["body"];
    body.call<void>("appendChild", input);
    input.call<void>("click");
    body.call<void>("removeChild", input);
}

void openFiles(const std::string &accept, FileSelectMode fileSelectMode,
    const std::function<void (int fileCount)> &fileDialogClosed,
    const std::function<char *(uint64_t size, const std::string name)> &acceptFile,
    const std::function<void()> &fileDataReady)
{
    openFileDialog(accept, fileSelectMode, [=](const qstdweb::FileList &files) {
        fileDialogClosed(files.length());
        readFiles(files, acceptFile, fileDataReady);
    });
}

void openFile(const std::string &accept,
    const std::function<void (bool fileSelected)> &fileDialogClosed,
    const std::function<char *(uint64_t size, const std::string name)> &acceptFile,
    const std::function<void()> &fileDataReady)
{
    auto fileDialogClosedWithInt = [=](int fileCount) { fileDialogClosed(fileCount != 0); };
    openFiles(accept, FileSelectMode::SingleFile, fileDialogClosedWithInt, acceptFile, fileDataReady);
}

} // namespace QWasmLocalFileAccess

QT_END_NAMESPACE
