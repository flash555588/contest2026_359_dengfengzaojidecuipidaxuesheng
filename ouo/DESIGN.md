# Grok Ball LVGL adaptation

The upstream engine is retained verbatim. VelaMood remains the compatibility facade; states now follow Grok Ball rather than the previous Mood Console meanings.

## State protocol

IDs: 00..07, 10..21, 30..41 (32 total). 00 is sleep, 02 idle, 10 happy, 30 thinking. GrokBall.EMOTIONS is the authoritative table. Old IDs 08 and 09, missing fields, malformed JSON and unknown IDs fall back to 02. AI prompts using old meanings must be updated.

```js
VelaMood.handleAIMessage({emotionId: '30', tips: '正在分析任务'});
VelaMood.setEmotion('10');
VelaMood.setGaze(0.5, -0.2); // normalized coordinates
VelaMood.startTour(1800);
VelaMood.stopTour();
```

Messages also accept JSON strings. Explicit selections save through package storage; tour changes do not repeatedly write storage.

## Rendering

One native 50 ms interval drives the engine. A minimal scene adapter handles the SVG commands/transforms used in the pinned version. Filled contours are scanned once, strokes use LVGL lines. SVG painter order is preserved by ui.front; removed FX nodes release native widgets.

The upstream engine is active even though browser rAF is unused: its active gate controls pool changes, blinks, idle antics and entry confetti/ribbons. setActive(false) stops the tour and freezes native ticks. AI tips events contain {text}, which the app reads explicitly.

The expression picker groups 8 lifecycle, 12 emotion and 12 agent states. Opening it stops the tour and freezes scene updates to keep SVG foreground widgets below the modal. Closing/choosing releases all picker widgets. Touch begins on the sphere only; control taps do not spin it. External state/AI updates close the picker before rendering.

ui.polygon(points, color, opacity, stroke) creates a non-interactive contour; ui.polygonSet(handle, points, color, opacity, stroke) updates it. Each contour has 3..128 integer points, coordinates -8192..8192, opacity 0..255 and stroke 0..8. Arguments are converted before committing updates; generation checks reject widget removal during JS property getters. ui.front(handle) raises a widget.

Body/eyes retain upstream state colors. Radial/linear SVG gradients retain all stops and use short native horizontal gradients. Optional polygon gradient descriptors are [kind, stopCount, x1, y1, x2, y2, radius, offset0, color0, ...], with 2..5 stops. Text is native. Host fidelity and sanitizers do not establish real-device performance.
