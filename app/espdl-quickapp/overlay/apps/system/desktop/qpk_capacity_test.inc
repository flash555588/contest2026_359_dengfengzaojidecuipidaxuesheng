/* SPDX-License-Identifier: Apache-2.0
 * Explicit diagnostic only: exercises the real QuickJS/LVGL resource lifetime.
 * No files, user drafts or installed applications are changed. */
static const char qpk_capacity_test_js[] =
"var hs=[], timers=[], created=0, fired=0, checks=0;"
"var title=ui.text('Resource test running',20,20,20,ui.primary);"
"function mark(){ui.setText(title,'Resource test PASS: '+checks+' checks');}"
"function clicked(){checks++;mark();console.log('RESOURCE_CLICK_PASS');}"
"var build=setInterval(function(){"
" for(var j=0;j<24&&created<640;j++,created++){"
"  var h=created<512?ui.text('resource '+created,10,100,16,ui.primary):ui.button('Resource click',20,90,220,48,clicked);"
"  ui.hide(h);hs.push(h);"
" }"
" if(created!==640)return;clearInterval(build);"
" ui.setText(hs[0],'First handle still valid');ui.setText(hs[511],'Last text valid');"
" ui.show(hs[639]);"
" for(var i=0;i<640;i+=2)ui.remove(hs[i]);"
" for(var i=0;i<320;i++){var h=ui.text('replacement',10,150,16,ui.primary);ui.hide(h);}"
" for(var i=0;i<40;i++)(function(){var t=setInterval(function(){clearInterval(t);fired++;if(fired===40){checks++;mark();console.log('RESOURCE_POOLS_PASS widgets=641 events=128 timers=40');}},100);timers.push(t);})();"
"},50);";

static void qpk_capacity_test_start(void)
{
  lv_obj_t *card = qapp_page_prepare("快应用资源测试");
  if (!card) return;
  int ret = qpk_runtime_launch(card, "Resource test", "diagnostic.resources", "1", "resource-test.js",
    qpk_capacity_test_js, sizeof(qpk_capacity_test_js) - 1, zh_font, show_toast, qpk_show_dialog);
  if (!ret) qapp_page_commit(card);
  else lv_obj_delete(lv_obj_get_parent(card));
  desktop_ui_probe_print("qpk-resource-test launch=%d\n", ret);
}
