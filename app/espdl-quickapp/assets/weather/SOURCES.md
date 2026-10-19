# Original weather artwork

These are original AI-generated digital sky paintings made specifically for this
project using the built-in `image_gen` tool. No external reference image, stock
image, phone wallpaper or third-party application asset was used. The clear-sky
revision edits only this project's own generated `clear-draft.png`.

The downloaded stock photographs from the rejected draft have been removed.
The original 1536×1024 PNGs, exact prompts and generation method are retained
here. Weather values still come from the existing service; these backgrounds
are illustrations, not a live camera or an observed moon phase.

| Original | Content |
| --- | --- |
| clear-original.png | Cloudless blue sky with a bright sun |
| cloudy-original.png | Blue sky, scattered volumetric clouds and diffused sunlight |
| overcast-original.png | Continuous grey cloud cover with no visible sun |
| clear-night-original.png | Cloudless midnight sky with a crescent moon |
| cloudy-night-original.png | Silver clouds and a crescent moon |
| rain-original.png | Light rain with sparse fine streaks |
| rain-heavy-original.png | Dense rain curtains and reduced visibility |
| thunder-original.png | Storm clouds lit by branching lightning |
| snow-original.png | Gently falling snow at several depths |
| snow-heavy-original.png | Dense wind-driven snow and reduced visibility |
| sleet-original.png | Mixed liquid rain and wet snow |
| fog-original.png | Cool diffuse mist with reduced visibility |
| haze-original.png | Muted warm-grey fine-particle haze |
| dust-original.png | Ochre airborne dust and turbulent sand layers |
| drizzle-original.png | Very fine mistlike drizzle under low stratus |
| rain-moderate-original.png | Sustained moderate rain with separated streaks |
| rain-large-original.png | Abundant rain and layered water curtains |
| rain-extreme-original.png | Extreme dense downpour and very low visibility |
| showers-original.png | Localized rain shaft between cloud breaks |
| hail-original.png | Small translucent ice pellets beneath storm clouds |
| freezing-rain-original.png | Cold liquid rainfall with an icy atmosphere |
| snow-moderate-original.png | Steady moderate natural snowfall |
| snow-large-original.png | Dense steady snow with large soft flakes |
| snow-showers-original.png | Localized snow shaft between winter clouds |
| dust-floating-original.png | Quiet suspended fine mineral dust |
| dust-blowing-original.png | Wind-lifted sand in sweeping wisps |
| wind-original.png | Wind-sheared elongated clouds and turbulent flow |
| thunder-hail-original.png | Lightning, rain and small hail pellets |
| tornado-original.png | Condensation funnel below a rotating cloud base |
| tropical-storm-original.png | Wind-sheared tropical storm clouds and driving rain |

`tools/prepare_weather_art.py` creates native 307×272 / 588×326 baseline
JPEG assets at quality 84, with 4:4:4 chroma and no progressive encoding. Only
those 60 compressed assets are embedded into firmware. `compression.json`
records exact byte counts and hashes. `generation.json` and
`generation-expanded.json` and `generation-complete.json` preserve the complete prompts. Only one native-size
decoded image is retained per view; adding asset types does not add pixel caches.
The complete JPEG arrays are decoded directly from read-only storage without a
compressed-input heap copy. Same-sized pixel allocations are reused when weather
changes. Frost uses the same pixels and a 480-byte stack line buffer for in-place
sliding-window blur; there is no full-image temporary blur allocation.

There are 30 original paintings. Clear and cloudy skies have dedicated moonlit
night paintings; all other conditions reuse their painting with a cached-view
night tint at draw time. The frost cache uses the same tint without reblurring.
No new image copies are needed for these day/night variations. Similar intensity
descriptions (such as 大暴雨/特大暴雨) share the corresponding extreme-rain scene.
Freezing rain is an atmospheric illustration, not a visual measurement of whether
droplets are supercooled. Unknown descriptions keep a neutral fallback.

The existing service's free-form Chinese descriptions are handled by a bounded
priority classifier; this does not assume that every possible weather text is
enumerated. The API reference was checked on 2026-10-18:
https://uapis.cn/docs/api-reference/get-misc-weather
