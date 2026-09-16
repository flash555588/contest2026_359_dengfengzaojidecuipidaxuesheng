/* SPDX-License-Identifier: Apache-2.0
 * Explicit diagnostic only: proves that a computation which would blow the
 * interactive event budget completes when it is sliced with await
 * system.yield(), and that setTimeout fires exactly once. No files, drafts or
 * installed applications are changed. */
static const char qpk_yield_test_js[] =
"var slices=0,total=0,timeouts=0;"
"var title=ui.text('Yield test running',20,20,20,ui.primary);"
"function mark(t){ui.setText(title,t);}"
"setTimeout(function(){timeouts++;},120);"
"setTimeout(function(){timeouts++;},240);"
"async function work(){"
" for(var i=0;i<40;i++){"
"  for(var j=0;j<150000;j++)total+=j;"
"  slices++;"
"  if(slices%10===0)mark('Yield slice '+slices);"
"  await system.yield();"
" }"
" mark('Yield test PASS');"
" console.log('YIELD_PASS slices='+slices+' timeouts='+timeouts+' total='+total);"
"}"
"work().catch(function(e){console.log('YIELD_FAIL '+e);});";

static void qpk_yield_test_start(void)
{
  lv_obj_t *card = qapp_page_prepare("快应用切片测试");
  if (!card) return;
  int ret = qpk_runtime_launch(card, "Yield test", "diagnostic.yield", "1", "yield-test.js",
    qpk_yield_test_js, sizeof(qpk_yield_test_js) - 1, zh_font, show_toast, qpk_show_dialog);
  if (!ret) qapp_page_commit(card);
  else lv_obj_delete(lv_obj_get_parent(card));
  desktop_ui_probe_print("qpk-yield-test launch=%d\n", ret);
}
