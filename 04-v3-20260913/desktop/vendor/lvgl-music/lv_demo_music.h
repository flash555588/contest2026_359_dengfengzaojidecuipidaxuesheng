/* Local adapter: only compile the unchanged upstream image resources. */
#include <lvgl/lvgl.h>
#undef LV_USE_DEMO_MUSIC
#define LV_USE_DEMO_MUSIC 1
#undef LV_DEMO_MUSIC_LARGE
#define LV_DEMO_MUSIC_LARGE 0
