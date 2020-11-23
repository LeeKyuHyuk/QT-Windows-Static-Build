// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/scroll_timeline.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_scroll_timeline_options.h"
#include "third_party/blink/renderer/core/animation/scroll_timeline_util.h"
#include "third_party/blink/renderer/core/css/css_to_length_conversion_data.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/properties/css_parsing_utils.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/paint/compositing/paint_layer_compositor.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/platform/geometry/length_functions.h"

namespace blink {

namespace {
using ScrollTimelineSet =
    HeapHashMap<WeakMember<Node>, HeapHashSet<WeakMember<ScrollTimeline>>>;
ScrollTimelineSet& GetScrollTimelineSet() {
  DEFINE_STATIC_LOCAL(Persistent<ScrollTimelineSet>, set,
                      (MakeGarbageCollected<ScrollTimelineSet>()));
  return *set;
}

using ActiveScrollTimelineSet = HeapHashCountedSet<WeakMember<Node>>;
ActiveScrollTimelineSet& GetActiveScrollTimelineSet() {
  DEFINE_STATIC_LOCAL(Persistent<ActiveScrollTimelineSet>, set,
                      (MakeGarbageCollected<ActiveScrollTimelineSet>()));
  return *set;
}

bool StringToScrollDirection(String scroll_direction,
                             ScrollTimeline::ScrollDirection& result) {
  if (scroll_direction == "block") {
    result = ScrollTimeline::Block;
    return true;
  }
  if (scroll_direction == "inline") {
    result = ScrollTimeline::Inline;
    return true;
  }
  if (scroll_direction == "horizontal") {
    result = ScrollTimeline::Horizontal;
    return true;
  }
  if (scroll_direction == "vertical") {
    result = ScrollTimeline::Vertical;
    return true;
  }
  return false;
}

bool StringToScrollOffset(String scroll_offset,
                          const CSSParserContext& context,
                          CSSPrimitiveValue** result) {
  CSSTokenizer tokenizer(scroll_offset);
  const auto tokens = tokenizer.TokenizeToEOF();
  CSSParserTokenRange range(tokens);
  CSSValue* value = css_parsing_utils::ConsumeScrollOffset(range, context);
  if (!value)
    return false;

  // We support 'auto', but for simplicity just store it as nullptr.
  *result = DynamicTo<CSSPrimitiveValue>(value);
  return true;
}

// Note that the resolution process may trigger document lifecycle to clean
// style and layout.
Node* ResolveScrollSource(Element* scroll_source) {
  // When in quirks mode we need the style to be clean, so we don't use
  // |ScrollingElementNoLayout|.
  if (scroll_source &&
      scroll_source == scroll_source->GetDocument().scrollingElement()) {
    return &scroll_source->GetDocument();
  }
  return scroll_source;
}
}  // namespace

ScrollTimeline* ScrollTimeline::Create(Document& document,
                                       ScrollTimelineOptions* options,
                                       ExceptionState& exception_state) {
  Element* scroll_source = options->scrollSource()
                               ? options->scrollSource()
                               : document.scrollingElement();

  // TODO(xiaochengh): Try reusing an existing context in document.
  const CSSParserContext* context =
      MakeGarbageCollected<CSSParserContext>(document);

  ScrollDirection orientation;
  if (!StringToScrollDirection(options->orientation(), orientation)) {
    exception_state.ThrowDOMException(DOMExceptionCode::kNotSupportedError,
                                      "Invalid orientation");
    return nullptr;
  }

  CSSPrimitiveValue* start_scroll_offset = nullptr;
  if (!StringToScrollOffset(options->startScrollOffset(), *context,
                            &start_scroll_offset)) {
    exception_state.ThrowTypeError("Invalid startScrollOffset");
    return nullptr;
  }

  CSSPrimitiveValue* end_scroll_offset = nullptr;
  if (!StringToScrollOffset(options->endScrollOffset(), *context,
                            &end_scroll_offset)) {
    exception_state.ThrowTypeError("Invalid endScrollOffset");
    return nullptr;
  }

  // TODO(smcgruer): Support 'auto' value.
  if (options->timeRange().IsScrollTimelineAutoKeyword()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "'auto' value for timeRange not yet supported");
    return nullptr;
  }

  return MakeGarbageCollected<ScrollTimeline>(
      &document, scroll_source, orientation, start_scroll_offset,
      end_scroll_offset, options->timeRange().GetAsDouble());
}

ScrollTimeline::ScrollTimeline(Document* document,
                               Element* scroll_source,
                               ScrollDirection orientation,
                               CSSPrimitiveValue* start_scroll_offset,
                               CSSPrimitiveValue* end_scroll_offset,
                               double time_range)
    : AnimationTimeline(document),
      scroll_source_(scroll_source),
      resolved_scroll_source_(ResolveScrollSource(scroll_source_)),
      orientation_(orientation),
      start_scroll_offset_(start_scroll_offset),
      end_scroll_offset_(end_scroll_offset),
      time_range_(time_range) {
  if (resolved_scroll_source_) {
    ScrollTimelineSet& set = GetScrollTimelineSet();
    if (!set.Contains(resolved_scroll_source_)) {
      set.insert(resolved_scroll_source_,
                 HeapHashSet<WeakMember<ScrollTimeline>>());
    }
    auto it = set.find(resolved_scroll_source_);
    it->value.insert(this);
  }
  SnapshotState();
}

bool ScrollTimeline::IsActive() const {
  return phase_and_time_snapshotted_.phase != TimelinePhase::kInactive;
}

void ScrollTimeline::Invalidate() {
  ScheduleNextService();
}

bool ScrollTimeline::ComputeIsActive() const {
  LayoutBox* layout_box = resolved_scroll_source_
                              ? resolved_scroll_source_->GetLayoutBox()
                              : nullptr;
  return layout_box && layout_box->HasOverflowClip() &&
         layout_box->GetScrollableArea();
}

AnimationTimeline::PhaseAndTime ScrollTimeline::CurrentPhaseAndTime() {
  return phase_and_time_snapshotted_;
}

AnimationTimeline::PhaseAndTime ScrollTimeline::ComputeCurrentPhaseAndTime()
    const {
  // 1. If scroll timeline is inactive, return an unresolved time value.
  // https://github.com/WICG/scroll-animations/issues/31
  // https://wicg.github.io/scroll-animations/#current-time-algorithm
  if (!ComputeIsActive()) {
    return {TimelinePhase::kInactive, /*current_time*/ base::nullopt};
  }
  LayoutBox* layout_box = resolved_scroll_source_->GetLayoutBox();
  // 2. Otherwise, let current scroll offset be the current scroll offset of
  // scrollSource in the direction specified by orientation.

  double current_offset;
  double max_offset;
  GetCurrentAndMaxOffset(layout_box, current_offset, max_offset);

  double resolved_start_scroll_offset = 0;
  double resolved_end_scroll_offset = max_offset;
  ResolveScrollStartAndEnd(layout_box, max_offset, resolved_start_scroll_offset,
                           resolved_end_scroll_offset);

  // TODO(crbug.com/1060384): Once the spec has been updated to state what the
  // expected result is when startScrollOffset >= endScrollOffset, we might need
  // to add a special case here. See
  // https://github.com/WICG/scroll-animations/issues/20

  // 3. If current scroll offset is less than startScrollOffset:
  if (current_offset < resolved_start_scroll_offset) {
    return {TimelinePhase::kBefore, base::TimeDelta()};
  }

  // 4. If current scroll offset is greater than or equal to endScrollOffset:
  if (current_offset >= resolved_end_scroll_offset) {
    return {TimelinePhase::kAfter,
            base::TimeDelta::FromMillisecondsD(time_range_)};
  }

  // 5. Return the result of evaluating the following expression:
  //   ((current scroll offset - startScrollOffset) /
  //      (endScrollOffset - startScrollOffset)) * effective time range
  base::Optional<base::TimeDelta> calculated_current_time =
      base::TimeDelta::FromMillisecondsD(
          ((current_offset - resolved_start_scroll_offset) /
           (resolved_end_scroll_offset - resolved_start_scroll_offset)) *
          time_range_);
  return {TimelinePhase::kActive, calculated_current_time};
}

// Scroll-linked animations are initialized with the start time of zero.
base::Optional<base::TimeDelta>
ScrollTimeline::InitialStartTimeForAnimations() {
  return base::TimeDelta();
}

void ScrollTimeline::ServiceAnimations(TimingUpdateReason reason) {
  // Snapshot timeline state once at top of animation frame.
  if (reason == kTimingUpdateForAnimationFrame)
    SnapshotState();
  AnimationTimeline::ServiceAnimations(reason);
}

void ScrollTimeline::ScheduleNextService() {
  if (AnimationsNeedingUpdateCount() == 0)
    return;
  if (ComputeCurrentPhaseAndTime() != last_current_phase_and_time_)
    ScheduleServiceOnNextFrame();
}

void ScrollTimeline::SnapshotState() {
  phase_and_time_snapshotted_ = ComputeCurrentPhaseAndTime();
}

Element* ScrollTimeline::scrollSource() {
  return scroll_source_.Get();
}

String ScrollTimeline::orientation() {
  switch (orientation_) {
    case Block:
      return "block";
    case Inline:
      return "inline";
    case Horizontal:
      return "horizontal";
    case Vertical:
      return "vertical";
    default:
      NOTREACHED();
      return "";
  }
}

String ScrollTimeline::startScrollOffset() {
  return start_scroll_offset_ ? start_scroll_offset_->CssText() : "auto";
}

String ScrollTimeline::endScrollOffset() {
  return end_scroll_offset_ ? end_scroll_offset_->CssText() : "auto";
}

void ScrollTimeline::timeRange(DoubleOrScrollTimelineAutoKeyword& result) {
  result.SetDouble(time_range_);
}

void ScrollTimeline::GetCurrentAndMaxOffset(const LayoutBox* layout_box,
                                            double& current_offset,
                                            double& max_offset) const {
  DCHECK(layout_box);
  DCHECK(layout_box->GetScrollableArea());

  // Depending on the writing-mode and direction, the scroll origin shifts and
  // the scroll offset may be negative. The easiest way to deal with this is to
  // use only the magnitude of the scroll offset, and compare it to (max_offset
  // - min_offset).
  PaintLayerScrollableArea* scrollable_area = layout_box->GetScrollableArea();

  // Using the absolute value of the scroll offset only makes sense if either
  // the max or min scroll offset for a given axis is 0. This should be
  // guaranteed by the scroll origin code, but these DCHECKs ensure that.
  DCHECK(scrollable_area->MaximumScrollOffset().Height() == 0 ||
         scrollable_area->MinimumScrollOffset().Height() == 0);
  DCHECK(scrollable_area->MaximumScrollOffset().Width() == 0 ||
         scrollable_area->MinimumScrollOffset().Width() == 0);
  ScrollOffset scroll_offset = scrollable_area->GetScrollOffset();
  ScrollOffset scroll_dimensions = scrollable_area->MaximumScrollOffset() -
                                   scrollable_area->MinimumScrollOffset();

  bool is_horizontal = layout_box->IsHorizontalWritingMode();
  if (orientation_ == Block) {
    current_offset =
        is_horizontal ? scroll_offset.Height() : scroll_offset.Width();
    max_offset =
        is_horizontal ? scroll_dimensions.Height() : scroll_dimensions.Width();
  } else if (orientation_ == Inline) {
    current_offset =
        is_horizontal ? scroll_offset.Width() : scroll_offset.Height();
    max_offset =
        is_horizontal ? scroll_dimensions.Width() : scroll_dimensions.Height();
  } else if (orientation_ == Horizontal) {
    current_offset = scroll_offset.Width();
    max_offset = scroll_dimensions.Width();
  } else {
    DCHECK(orientation_ == Vertical);
    current_offset = scroll_offset.Height();
    max_offset = scroll_dimensions.Height();
  }
  // When using a rtl direction, current_offset grows correctly from 0 to
  // max_offset, but is negative. Since our offsets are all just deltas along
  // the orientation direction, we can just take the absolute current_offset and
  // use that everywhere.
  current_offset = std::abs(current_offset);
}

void ScrollTimeline::ResolveScrollStartAndEnd(
    const LayoutBox* layout_box,
    double max_offset,
    double& resolved_start_scroll_offset,
    double& resolved_end_scroll_offset) const {
  DCHECK(layout_box);
  const ComputedStyle& computed_style = layout_box->StyleRef();
  Document& document = layout_box->GetDocument();
  const ComputedStyle* root_style =
      document.documentElement()
          ? document.documentElement()->GetComputedStyle()
          : document.GetComputedStyle();
  CSSToLengthConversionData conversion_data = CSSToLengthConversionData(
      &computed_style, root_style, document.GetLayoutView(),
      computed_style.EffectiveZoom());
  if (start_scroll_offset_) {
    resolved_start_scroll_offset = FloatValueForLength(
        start_scroll_offset_->ConvertToLength(conversion_data), max_offset);
  }
  if (end_scroll_offset_) {
    resolved_end_scroll_offset = FloatValueForLength(
        end_scroll_offset_->ConvertToLength(conversion_data), max_offset);
  }
}

void ScrollTimeline::WorkletAnimationAttached() {
  if (!resolved_scroll_source_)
    return;
  GetActiveScrollTimelineSet().insert(resolved_scroll_source_);
}

void ScrollTimeline::WorkletAnimationDetached() {
  if (!resolved_scroll_source_)
    return;
  GetActiveScrollTimelineSet().erase(resolved_scroll_source_);
}

void ScrollTimeline::Trace(Visitor* visitor) {
  visitor->Trace(scroll_source_);
  visitor->Trace(resolved_scroll_source_);
  visitor->Trace(start_scroll_offset_);
  visitor->Trace(end_scroll_offset_);
  AnimationTimeline::Trace(visitor);
}

bool ScrollTimeline::HasActiveScrollTimeline(Node* node) {
  ActiveScrollTimelineSet& worklet_animations_set =
      GetActiveScrollTimelineSet();
  auto worklet_animations_it = worklet_animations_set.find(node);
  if (worklet_animations_it != worklet_animations_set.end() &&
      worklet_animations_it->value > 0)
    return true;

  ScrollTimelineSet& set = GetScrollTimelineSet();
  auto it = set.find(node);
  if (it == set.end())
    return false;

  for (auto& timeline : it->value) {
    if (timeline->HasAnimations())
      return true;
  }
  return false;
}

void ScrollTimeline::Invalidate(Node* node) {
  ScrollTimelineSet& set = GetScrollTimelineSet();
  auto it = set.find(node);

  if (it == set.end())
    return;

  for (auto& timeline : it->value) {
    timeline->Invalidate();
  }
}

CompositorAnimationTimeline* ScrollTimeline::EnsureCompositorTimeline() {
  if (compositor_timeline_)
    return compositor_timeline_.get();

  compositor_timeline_ = std::make_unique<CompositorAnimationTimeline>(
      scroll_timeline_util::ToCompositorScrollTimeline(this));
  return compositor_timeline_.get();
}

}  // namespace blink
