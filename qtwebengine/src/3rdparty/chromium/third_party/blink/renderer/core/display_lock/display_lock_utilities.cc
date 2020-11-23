// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/display_lock/display_lock_utilities.h"

#include "third_party/blink/renderer/core/display_lock/display_lock_context.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/editing/editing_boundary.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"

#include <set>

namespace blink {
namespace {

// Returns the frame owner node for the frame that contains the given child, if
// one exists. Returns nullptr otherwise.
const Node* GetFrameOwnerNode(const Node* child) {
  if (!child || !child->GetDocument().GetFrame() ||
      !child->GetDocument().GetFrame()->OwnerLayoutObject()) {
    return nullptr;
  }
  return child->GetDocument().GetFrame()->OwnerLayoutObject()->GetNode();
}

bool UpdateStyleAndLayoutForRangeIfNeeded(const EphemeralRangeInFlatTree& range,
                                          DisplayLockActivationReason reason) {
  if (range.IsNull() || range.IsCollapsed())
    return false;
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      range.GetDocument().LockedDisplayLockCount() ==
          range.GetDocument().DisplayLockBlockingAllActivationCount())
    return false;
  Vector<DisplayLockContext::ScopedForcedUpdate> scoped_forced_update_list_;
  for (Node& node : range.Nodes()) {
    for (Element* locked_activatable_ancestor :
         DisplayLockUtilities::ActivatableLockedInclusiveAncestors(node,
                                                                   reason)) {
      DCHECK(locked_activatable_ancestor->GetDisplayLockContext());
      DCHECK(locked_activatable_ancestor->GetDisplayLockContext()->IsLocked());
      if (locked_activatable_ancestor->GetDisplayLockContext()->UpdateForced())
        break;
      scoped_forced_update_list_.push_back(
          locked_activatable_ancestor->GetDisplayLockContext()
              ->GetScopedForcedUpdate());
    }
  }
  if (!scoped_forced_update_list_.IsEmpty()) {
    range.GetDocument().UpdateStyleAndLayout(
        DocumentUpdateReason::kDisplayLock);
  }
  return !scoped_forced_update_list_.IsEmpty();
}

void PopulateAncestorContexts(Node* node,
                              std::set<DisplayLockContext*>* contexts) {
  DCHECK(node);
  for (Node& ancestor : FlatTreeTraversal::InclusiveAncestorsOf(*node)) {
    auto* ancestor_element = DynamicTo<Element>(ancestor);
    if (!ancestor_element)
      continue;
    if (auto* context = ancestor_element->GetDisplayLockContext())
      contexts->insert(context);
  }
}

}  // namespace

bool DisplayLockUtilities::ActivateFindInPageMatchRangeIfNeeded(
    const EphemeralRangeInFlatTree& range) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled())
    return false;
  DCHECK(!range.IsNull());
  DCHECK(!range.IsCollapsed());
  if (range.GetDocument().LockedDisplayLockCount() ==
      range.GetDocument().DisplayLockBlockingAllActivationCount())
    return false;
  // Find-in-page matches can't span multiple block-level elements (because the
  // text will be broken by newlines between blocks), so first we find the
  // block-level element which contains the match.
  // This means we only need to traverse up from one node in the range, in this
  // case we are traversing from the start position of the range.
  Element* enclosing_block =
      EnclosingBlock(range.StartPosition(), kCannotCrossEditingBoundary);
  // Note that we don't check the `range.EndPosition()` since we just activate
  // the beginning of the range. In find-in-page cases, the end position is the
  // same since the matches cannot cross block boundaries. However, in
  // scroll-to-text, the range might be different, but we still just activate
  // the beginning of the range. See
  // https://github.com/WICG/display-locking/issues/125 for more details.
  DCHECK(enclosing_block);
  return enclosing_block->ActivateDisplayLockIfNeeded(
      DisplayLockActivationReason::kFindInPage);
}

bool DisplayLockUtilities::ActivateSelectionRangeIfNeeded(
    const EphemeralRangeInFlatTree& range) {
  if (range.IsNull() || range.IsCollapsed())
    return false;
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      range.GetDocument().LockedDisplayLockCount() ==
          range.GetDocument().DisplayLockBlockingAllActivationCount())
    return false;
  UpdateStyleAndLayoutForRangeIfNeeded(range,
                                       DisplayLockActivationReason::kSelection);
  HeapHashSet<Member<Element>> elements_to_activate;
  for (Node& node : range.Nodes()) {
    DCHECK(!node.GetDocument().NeedsLayoutTreeUpdateForNode(node));
    const ComputedStyle* style = node.GetComputedStyle();
    if (!style || style->UserSelect() == EUserSelect::kNone)
      continue;
    if (auto* nearest_locked_ancestor = NearestLockedExclusiveAncestor(node))
      elements_to_activate.insert(nearest_locked_ancestor);
  }
  for (Element* element : elements_to_activate) {
    element->ActivateDisplayLockIfNeeded(
        DisplayLockActivationReason::kSelection);
  }
  return !elements_to_activate.IsEmpty();
}

const HeapVector<Member<Element>>
DisplayLockUtilities::ActivatableLockedInclusiveAncestors(
    const Node& node,
    DisplayLockActivationReason reason) {
  HeapVector<Member<Element>> elements_to_activate;
  const_cast<Node*>(&node)->UpdateDistributionForFlatTreeTraversal();
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      node.GetDocument().LockedDisplayLockCount() ==
          node.GetDocument().DisplayLockBlockingAllActivationCount())
    return elements_to_activate;

  for (Node& ancestor : FlatTreeTraversal::InclusiveAncestorsOf(node)) {
    auto* ancestor_element = DynamicTo<Element>(ancestor);
    if (!ancestor_element)
      continue;
    if (auto* context = ancestor_element->GetDisplayLockContext()) {
      if (!context->IsLocked())
        continue;
      if (!context->IsActivatable(reason)) {
        // If we find a non-activatable locked ancestor, then we shouldn't
        // activate anything.
        elements_to_activate.clear();
        return elements_to_activate;
      }
      elements_to_activate.push_back(ancestor_element);
    }
  }
  return elements_to_activate;
}

DisplayLockUtilities::ScopedChainForcedUpdate::ScopedChainForcedUpdate(
    const Node* node,
    bool include_self) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled())
    return;

  CreateParentFrameScopeIfNeeded(node);

  if (node->GetDocument().LockedDisplayLockCount() == 0)
    return;
  const_cast<Node*>(node)->UpdateDistributionForFlatTreeTraversal();

  // Get the right ancestor view. Only use inclusive ancestors if the node
  // itself is locked and it prevents self layout, or if |include_self| is true.
  // If self layout is not prevented, we don't need to force the subtree layout,
  // so use exclusive ancestors in that case.
  auto ancestor_view = [node, include_self] {
    if (auto* element = DynamicTo<Element>(node)) {
      auto* context = element->GetDisplayLockContext();
      if (context && (include_self || !context->ShouldLayout(
                                          DisplayLockLifecycleTarget::kSelf))) {
        return FlatTreeTraversal::InclusiveAncestorsOf(*node);
      }
    }
    return FlatTreeTraversal::AncestorsOf(*node);
  }();

  // TODO(vmpstr): This is somewhat inefficient, since we would pay the cost
  // of traversing the ancestor chain even for nodes that are not in the
  // locked subtree. We need to figure out if there is a supplementary
  // structure that we can use to quickly identify nodes that are in the
  // locked subtree.
  for (Node& ancestor : ancestor_view) {
    auto* ancestor_node = DynamicTo<Element>(ancestor);
    if (!ancestor_node)
      continue;
    if (auto* context = ancestor_node->GetDisplayLockContext()) {
      if (context->UpdateForced())
        break;
      scoped_update_forced_list_.push_back(context->GetScopedForcedUpdate());
    }
  }
}

void DisplayLockUtilities::ScopedChainForcedUpdate::
    CreateParentFrameScopeIfNeeded(const Node* node) {
  auto* owner_node = GetFrameOwnerNode(node);
  if (owner_node) {
    parent_frame_scope_ =
        std::make_unique<ScopedChainForcedUpdate>(owner_node, true);
  }
}

const Element* DisplayLockUtilities::NearestLockedInclusiveAncestor(
    const Node& node) {
  const_cast<Node*>(&node)->UpdateDistributionForFlatTreeTraversal();
  auto* element = DynamicTo<Element>(node);
  if (!element)
    return NearestLockedExclusiveAncestor(node);
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      !node.isConnected() || node.GetDocument().LockedDisplayLockCount() == 0 ||
      !node.CanParticipateInFlatTree()) {
    return nullptr;
  }
  if (auto* context = element->GetDisplayLockContext()) {
    if (context->IsLocked())
      return element;
  }
  return NearestLockedExclusiveAncestor(node);
}

Element* DisplayLockUtilities::NearestLockedInclusiveAncestor(Node& node) {
  return const_cast<Element*>(
      NearestLockedInclusiveAncestor(static_cast<const Node&>(node)));
}

Element* DisplayLockUtilities::NearestLockedExclusiveAncestor(
    const Node& node) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      !node.isConnected() || node.GetDocument().LockedDisplayLockCount() == 0 ||
      !node.CanParticipateInFlatTree()) {
    return nullptr;
  }
  const_cast<Node*>(&node)->UpdateDistributionForFlatTreeTraversal();
  // TODO(crbug.com/924550): Once we figure out a more efficient way to
  // determine whether we're inside a locked subtree or not, change this.
  for (Node& ancestor : FlatTreeTraversal::AncestorsOf(node)) {
    auto* ancestor_element = DynamicTo<Element>(ancestor);
    if (!ancestor_element)
      continue;
    if (auto* context = ancestor_element->GetDisplayLockContext()) {
      if (context->IsLocked())
        return ancestor_element;
    }
  }
  return nullptr;
}

Element* DisplayLockUtilities::HighestLockedInclusiveAncestor(
    const Node& node) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      node.GetDocument().LockedDisplayLockCount() == 0 ||
      !node.CanParticipateInFlatTree()) {
    return nullptr;
  }
  const_cast<Node*>(&node)->UpdateDistributionForFlatTreeTraversal();
  Element* locked_ancestor = nullptr;
  for (Node& ancestor : FlatTreeTraversal::InclusiveAncestorsOf(node)) {
    auto* ancestor_node = DynamicTo<Element>(ancestor);
    if (!ancestor_node)
      continue;
    if (auto* context = ancestor_node->GetDisplayLockContext()) {
      if (context->IsLocked())
        locked_ancestor = ancestor_node;
    }
  }
  return locked_ancestor;
}

Element* DisplayLockUtilities::HighestLockedExclusiveAncestor(
    const Node& node) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      node.GetDocument().LockedDisplayLockCount() == 0 ||
      !node.CanParticipateInFlatTree()) {
    return nullptr;
  }
  const_cast<Node*>(&node)->UpdateDistributionForFlatTreeTraversal();

  if (Node* parent = FlatTreeTraversal::Parent(node))
    return HighestLockedInclusiveAncestor(*parent);
  return nullptr;
}

Element* DisplayLockUtilities::NearestLockedInclusiveAncestor(
    const LayoutObject& object) {
  auto* node = object.GetNode();
  auto* ancestor = object.Parent();
  while (ancestor && !node) {
    node = ancestor->GetNode();
    ancestor = ancestor->Parent();
  }
  return node ? NearestLockedInclusiveAncestor(*node) : nullptr;
}

Element* DisplayLockUtilities::NearestLockedExclusiveAncestor(
    const LayoutObject& object) {
  if (auto* node = object.GetNode())
    return NearestLockedExclusiveAncestor(*node);
  // Since we now navigate to an ancestor, use the inclusive version.
  if (auto* parent = object.Parent())
    return NearestLockedInclusiveAncestor(*parent);
  return nullptr;
}

bool DisplayLockUtilities::IsInUnlockedOrActivatableSubtree(
    const Node& node,
    DisplayLockActivationReason activation_reason) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled(
          node.GetExecutionContext()) ||
      node.GetDocument().LockedDisplayLockCount() == 0 ||
      node.GetDocument().DisplayLockBlockingAllActivationCount() == 0 ||
      !node.CanParticipateInFlatTree()) {
    return true;
  }

  for (auto* element = NearestLockedExclusiveAncestor(node); element;
       element = NearestLockedExclusiveAncestor(*element)) {
    if (!element->GetDisplayLockContext()->IsActivatable(activation_reason)) {
      return false;
    }
  }
  return true;
}

bool DisplayLockUtilities::IsInLockedSubtreeCrossingFrames(
    const Node& source_node) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled())
    return false;
  const Node* node = &source_node;

  // Special case self-node checking.
  auto* element = DynamicTo<Element>(node);
  if (element && node->GetDocument().LockedDisplayLockCount()) {
    auto* context = element->GetDisplayLockContext();
    if (context && !context->ShouldLayout(DisplayLockLifecycleTarget::kSelf))
      return true;
  }
  const_cast<Node*>(node)->UpdateDistributionForFlatTreeTraversal();

  // Since we handled the self-check above, we need to do inclusive checks
  // starting from the parent.
  node = FlatTreeTraversal::Parent(*node);
  // If we don't have a flat-tree parent, get the |source_node|'s owner node
  // instead.
  if (!node)
    node = GetFrameOwnerNode(&source_node);

  while (node) {
    if (NearestLockedInclusiveAncestor(*node))
      return true;
    node = GetFrameOwnerNode(node);
  }
  return false;
}

void DisplayLockUtilities::ElementLostFocus(Element* element) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      (element && element->GetDocument().DisplayLockCount() == 0))
    return;
  for (; element; element = FlatTreeTraversal::ParentElement(*element)) {
    auto* context = element->GetDisplayLockContext();
    if (context)
      context->NotifySubtreeLostFocus();
  }
}
void DisplayLockUtilities::ElementGainedFocus(Element* element) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      (element && element->GetDocument().DisplayLockCount() == 0))
    return;

  for (; element; element = FlatTreeTraversal::ParentElement(*element)) {
    auto* context = element->GetDisplayLockContext();
    if (context)
      context->NotifySubtreeGainedFocus();
  }
}

void DisplayLockUtilities::SelectionChanged(
    const EphemeralRangeInFlatTree& old_selection,
    const EphemeralRangeInFlatTree& new_selection) {
  if (!RuntimeEnabledFeatures::CSSSubtreeVisibilityEnabled() ||
      (!old_selection.IsNull() &&
       old_selection.GetDocument().DisplayLockCount() == 0) ||
      (!new_selection.IsNull() &&
       new_selection.GetDocument().DisplayLockCount() == 0))
    return;

  TRACE_EVENT0("blink", "DisplayLockUtilities::SelectionChanged");
  std::set<Node*> old_nodes;
  for (Node& node : old_selection.Nodes())
    old_nodes.insert(&node);

  std::set<Node*> new_nodes;
  for (Node& node : new_selection.Nodes())
    new_nodes.insert(&node);

  std::set<DisplayLockContext*> lost_selection_contexts;
  std::set<DisplayLockContext*> gained_selection_contexts;

  // Skip common nodes and extract contexts from nodes that lost selection and
  // contexts from nodes that gained selection.
  // This is similar to std::set_symmetric_difference except that we need to
  // know which set the resulting item came from. In this version, we simply do
  // the relevant operation on each of the items instead of storing the
  // difference.
  std::set<Node*>::iterator old_it = old_nodes.begin();
  std::set<Node*>::iterator new_it = new_nodes.begin();
  while (old_it != old_nodes.end() && new_it != new_nodes.end()) {
    // Compare the addresses since that's how the nodes are ordered in the set.
    if (*old_it < *new_it) {
      PopulateAncestorContexts(*old_it++, &lost_selection_contexts);
    } else if (*old_it > *new_it) {
      PopulateAncestorContexts(*new_it++, &gained_selection_contexts);
    } else {
      ++old_it;
      ++new_it;
    }
  }
  while (old_it != old_nodes.end())
    PopulateAncestorContexts(*old_it++, &lost_selection_contexts);
  while (new_it != new_nodes.end())
    PopulateAncestorContexts(*new_it++, &gained_selection_contexts);

  // Now do a similar thing with contexts: skip common ones, and mark the ones
  // that lost selection or gained selection as such.
  std::set<DisplayLockContext*>::iterator lost_it =
      lost_selection_contexts.begin();
  std::set<DisplayLockContext*>::iterator gained_it =
      gained_selection_contexts.begin();
  while (lost_it != lost_selection_contexts.end() &&
         gained_it != gained_selection_contexts.end()) {
    if (*lost_it < *gained_it) {
      (*lost_it++)->NotifySubtreeLostSelection();
    } else if (*lost_it > *gained_it) {
      (*gained_it++)->NotifySubtreeGainedSelection();
    } else {
      ++lost_it;
      ++gained_it;
    }
  }
  while (lost_it != lost_selection_contexts.end())
    (*lost_it++)->NotifySubtreeLostSelection();
  while (gained_it != gained_selection_contexts.end())
    (*gained_it++)->NotifySubtreeGainedSelection();
}

void DisplayLockUtilities::SelectionRemovedFromDocument(Document& document) {
  document.NotifySelectionRemovedFromDisplayLocks();
}

}  // namespace blink
