# LVGL music image resources

Source: https://github.com/lvgl/lvgl/tree/v9.2.2/demos/music/assets

The three cover C files were copied without edits from the existing local
LVGL build tree's `demos/music/assets` directory. They are demo illustrations,
not connected music metadata and not original assets from the user's video.
The local copy has not been independently compared with the upstream tag.

The upstream repository license is included as `LICENCE.txt`.
Check any additional image rights before distributing a commercial product.

`lv_demo_music.h` is a local adapter, not the upstream demo header.
`../../glass_music_assets.inc` includes these resources in the desktop
translation unit and restores the demo configuration macros afterward.
Do not compile these C files separately as well, which would duplicate symbols.
