// NetSurf browser shell. The page view itself is the native module in
// native/module.elf; the system loads it on launch and then exposes
// system.browser. This script only draws the toolbar and forwards input.

var TOOLBAR = 56;
var HOME = "about:welcome";
var size = ui.getSize();
var W = size.width;

var opened = false;
var urlButton = 0;
var statusText = 0;
var lastUrl = "";
var lastStatus = "";
var inputOpen = false;

ui.rect(0, 0, W, TOOLBAR, 0xf2f3f5);
ui.rect(0, TOOLBAR - 1, W, 1, 0xd0d3d9);

function api() {
  return system.browser;
}

function shortUrl(url) {
  if (!url) return "点此输入网址";
  return url.length > 64 ? url.substring(0, 61) + "..." : url;
}

function openAddress() {
  if (!opened || inputOpen) return;
  var st = api().state();
  inputOpen = true;
  prompt.input({ title: "打开网址", placeholder: "例如 www.example.com", value: st.url, maxLength: 512 },
    function (value) {
      inputOpen = false;
      if (value) api().go(value);
    });
}

function openFormInput() {
  if (inputOpen) return;
  inputOpen = true;
  prompt.input({ title: "输入文字", placeholder: "提交后发送到网页输入框", maxLength: 256, notifyCancel: true },
    function (value) {
      inputOpen = false;
      api().type(value || "", !!value);
    });
}

// Buttons pad their label by 20 px per side; two 20 px CJK glyphs need
// 40 px, so a two-character button must be at least 80 px wide.
var BW = 88;
ui.button("后退", 8, 8, BW, 40, function () { if (opened) api().back(); }, 0xffffff);
ui.button("前进", 8 + (BW + 6), 8, BW, 40, function () { if (opened) api().forward(); }, 0xffffff);
ui.button("刷新", 8 + 2 * (BW + 6), 8, BW, 40, function () { if (opened) api().reload(); }, 0xffffff);
ui.button("主页", 8 + 3 * (BW + 6), 8, BW, 40, function () { if (opened) api().go(HOME); }, 0xffffff);
var URL_X = 8 + 4 * (BW + 6);
urlButton = ui.button(shortUrl(""), URL_X, 8, W - URL_X - 8 - 120, 40, openAddress, 0xffffff);
statusText = ui.text("正在加载浏览器…", W - 116, 18, 16, 0x5f6368);

ui.onBack(function () {
  if (!opened) return false;
  var st = api().state();
  if (st.canBack) {
    api().back();
    return true;
  }
  return false;
});

function refresh() {
  if (!opened) {
    if (system.nativeError) {
      ui.setText(statusText, "加载失败");
      ui.setText(urlButton, system.nativeError);
      return;
    }
    if (system.nativeLoading || !system.browser) return;
    opened = api().open(HOME, TOOLBAR);
    var first = api().state();
    if (!opened && first.error) {
      ui.setText(statusText, "打开失败");
      ui.setText(urlButton, first.error);
      return;
    }
  }
  var st = api().state();
  if (st.url !== lastUrl) {
    lastUrl = st.url;
    ui.setText(urlButton, shortUrl(st.url));
  }
  var status = st.loading ? "加载中…" : "";
  if (status !== lastStatus) {
    lastStatus = status;
    ui.setText(statusText, status);
  }
  if (st.wantsInput) openFormInput();
}

setInterval(refresh, 250);
