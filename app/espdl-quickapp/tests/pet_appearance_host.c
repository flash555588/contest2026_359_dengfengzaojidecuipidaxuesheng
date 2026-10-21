/* SPDX-License-Identifier: Apache-2.0 */
#include "production_pet_appearance.inc"
static uint8_t pixels[1024*600*2];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data)
{(void)area;(void)data;lv_display_flush_ready(display);}
static void draw(void)
{lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL);}
int main(int argc,char **argv)
{
  assert(argc==4);appearance_path=argv[1];
  assert(pet_lvgl_reload_sprites()==-ENODEV);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,flush);
  pet_engine_init(1024,600);pet_engine_set_mode(PET_STILL);
  g_ui.root=lv_obj_create(lv_screen_active());g_ui.sprite=lv_image_create(g_ui.root);
  g_ui.bubble=lv_label_create(g_ui.root);
  lv_obj_t **parts[]={&g_ui.body,&g_ui.belly,&g_ui.tail,&g_ui.eye_l,&g_ui.eye_r,
    &g_ui.pupil_l,&g_ui.pupil_r,&g_ui.blush_l,&g_ui.blush_r,&g_ui.mouth};
  for(unsigned i=0;i<sizeof(parts)/sizeof(parts[0]);i++)*parts[i]=lv_obj_create(g_ui.root);
  g_ui.open=true;g_ui.sprite_view=-1;g_ui.sprite_size=-1;
  pet_lvgl_sync();draw();assert(g_ui.sprites.storage==NULL);
  assert(pet_lvgl_reload_sprites()==0);draw();assert(g_ui.sprite_dsc.data);
  const char *sizes[]={"small","medium","large"};
  for(unsigned cycle=0;cycle<60;cycle++){
    pet_engine_set_size(sizes[cycle%3]);
    pet_engine_set_visible(cycle%2==0);
    assert(pet_lvgl_reload_sprites()==0);
    pet_engine_set_visible(true);pet_lvgl_sync();draw();
    for(int dir=PET_DOWN;dir<=PET_RIGHT;dir++){
      struct pet_state_s state;pet_engine_get(&state);state.dir=dir;
      layout_sprite(&state,0);draw();
      assert(g_ui.sprite_view==dir&&g_ui.sprite_size==(int)(cycle%3));
      assert(g_ui.sprite_dsc.data>=g_ui.sprites.storage&&
        g_ui.sprite_dsc.data+g_ui.sprite_dsc.data_size<=g_ui.sprites.storage+g_ui.sprites.storage_size);
    }
    uint8_t *storage=g_ui.sprites.storage;const void *image=g_ui.sprite_dsc.data;
    appearance_path=argv[2];assert(pet_lvgl_reload_sprites()<0);
    assert(g_ui.sprites.storage==storage&&g_ui.sprite_dsc.data==image);draw();
    appearance_path=argv[3];assert(pet_lvgl_reload_sprites()<0);
    assert(g_ui.sprites.storage==storage&&g_ui.sprite_dsc.data==image);draw();
    appearance_path=argv[1];
  }
  lv_image_set_src(g_ui.sprite,NULL);lv_image_cache_drop(&g_ui.sprite_dsc);
  lv_obj_delete(g_ui.root);pet_sprites_unload(&g_ui.sprites);lv_deinit();
  puts("PET_APPEARANCE_OK: 12 images; late load; 60 replacements; hidden reload; corrupt/missing preserve old pixels");
}
