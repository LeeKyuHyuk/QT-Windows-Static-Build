/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_EXECUTION_CONTEXT_EXECUTION_CONTEXT_LIFECYCLE_OBSERVER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_EXECUTION_CONTEXT_EXECUTION_CONTEXT_LIFECYCLE_OBSERVER_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/context_lifecycle_observer.h"

namespace blink {

class ExecutionContext;
class Document;
class LocalDOMWindow;
class LocalFrame;

// ExecutionContextClient and ExecutionContextLifecycleObserver are helpers to
// associate an object with an ExecutionContext. It is unsafe to access an
// associated GC object, *including via GetExecutionContext()*, in the
// destructor of a GC object.
//
// To discourage incorrect usage, these objects both start returning null
// once the execution context shuts down. For a document, this occurs when
// the frame navigates to another document or is detached. For a worker global
// scope, this occurs when it shuts down.
//
// * If an object only needs to refer to a valid ExecutionContext but does not
//   need to stop or suspend any activity, it should be a
//   ExecutionContextClient.
// * If an object associated with an ExecutionContext has shutdown logic to
//   perform, such as halting activity or disconnecting from longer-lived
//   objects, it should be a PausableObject.
// * If an object additionally must suspend its activity during pause (see
//   execution_context_lifecycle_state_observer.h), it should be a
//   ExecutionContextLifecycleStateObserver (and thus, transitively, also a
//   ExecutionContextLifecycleObserver).
//
// If your object has activity which requires that it be kept alive, even if no
// other object has a reference to it, consider whether your object should also
// derive from ActiveScriptWrappable.

// ExecutionContextClient provides access to the associated execution context
// until it is shut down (e.g. for a document, at navigation or frame detach).
class CORE_EXPORT ExecutionContextClient : public GarbageCollectedMixin {
 public:
  // Returns the execution context until it is detached.
  // From then on, returns null instead.
  ExecutionContext* GetExecutionContext() const;

  // Return a live document if associated with it. Returns null otherwise.
  Document* GetDocument() const;

  // If associated with a live document, returns the associated frame.
  // Returns null otherwise.
  LocalFrame* GetFrame() const;

  void Trace(Visitor*) override;

 protected:
  explicit ExecutionContextClient(ExecutionContext*);
  explicit ExecutionContextClient(LocalFrame*);

 private:
  WeakMember<ExecutionContext> execution_context_;
};

// ExecutionContextLifecycleObserver provides an additional ContextDestroyed()
// hook to execute cleanup code when a context is shut down (e.g. for a
// document, at navigation or frame detach -- not when its destructor runs).
//
// Execution context associated objects which have ongoing activity,
// registration with objects which outlive the context, or resources which
// should be promptly released, should consider deriving from
// PausableObject. As a rule of thumb: if the destructor contains
// non-trivial logic, that logic may belong in ContextDestroyed() instead.
//
// If there is ongoing activity associated with the object, consider whether it
// needs to be paused when execution is suspended (see
// ExecutionContextLifecycleStateObserver).
//
// If none of the above applies, prefer the simpler ExecutionContextClient.
class CORE_EXPORT ExecutionContextLifecycleObserver
    : public ContextLifecycleObserver {
 public:

  // Returns the execution context until it is detached.
  // From then on, returns null instead.
  ExecutionContext* GetExecutionContext() const;
  virtual void SetExecutionContext(ExecutionContext*);

  // If associated with a live document, returns the associated frame.
  // Returns null otherwise.
  LocalFrame* GetFrame() const;

  enum Type {
    kGenericType,
    kStateObjectType,
  };

  Type ObserverType() const { return observer_type_; }

  bool IsExecutionContextLifecycleObserver() const override { return true; }

  void Trace(Visitor*) override;

 protected:
  ExecutionContextLifecycleObserver();
  // TODO(crbug.com/1029822): This is a shim to enable migrating
  // ExecutionContext to LocalDOMWindow.
  explicit ExecutionContextLifecycleObserver(Document*,
                                             Type type = kGenericType);

  explicit ExecutionContextLifecycleObserver(
      ExecutionContext* execution_context,
      Type type = kGenericType);

 private:
  Type observer_type_;
};

// DOMWindowClient is a helper to associate an object with a LocalDOMWindow.
//
// - domWindow() returns null after the window is detached.
// - frame() is a syntax sugar for domWindow()->frame(). It returns
//   null after the window is detached.
//
// Both can safely be used up until destruction; i.e., unsafe to
// call upon in a destructor.
//
// If the object is a per-ExecutionContext thing, use ExecutionContextClient/
// ExecutionContextLifecycleObserver. If the object is a per-DOMWindow thing,
// use DOMWindowClient. Basically, DOMWindowClient is expected to be used (only)
// for objects directly held by LocalDOMWindow. Other objects should use
// ExecutionContextClient/ExecutionContextLifecycleObserver.
//
// There is a subtle difference between the timing when the context gets
// detached and the timing when the window gets detached. In common cases,
// these two happen at the same timing. The only exception is a case where
// a frame navigates from an initial empty document to another same-origin
// document. In this case, a Document is recreated but a DOMWindow is reused.
// Hence, in the navigated document
// ExecutionContextClient::GetExecutionContext() returns null while
// DOMWindowClient::domWindow() keeps returning the window.
class CORE_EXPORT DOMWindowClient : public GarbageCollectedMixin {
 public:
  LocalDOMWindow* DomWindow() const;
  LocalFrame* GetFrame() const;

  void Trace(Visitor*) override;

 protected:
  explicit DOMWindowClient(LocalDOMWindow*);
  explicit DOMWindowClient(LocalFrame*);

 private:
  WeakMember<LocalDOMWindow> dom_window_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_EXECUTION_CONTEXT_EXECUTION_CONTEXT_LIFECYCLE_OBSERVER_H_
