"""One-time source update for streaming integration and configuration UI."""
from pathlib import Path
ws = Path(__file__).resolve().parent.parent
src = ws / '04-v3-20260913/espdl-quickapp/overlay/apps/system'
def edit(path, pairs):
    text = path.read_text(encoding='utf-8')
    for old, new in pairs:
        assert old in text, (path, old)
        text = text.replace(old, new)
    path.write_text(text, encoding='utf-8')
edit(src / 'desktop/glass_portal_config.c', [
    ('"max_tokens",512', '"max_tokens",16384'), ('"timeout_ms",30000', '"timeout_ms",300000'),
    ('min=1; max=8192;', 'min=1; max=384000;'), ('min=5000; max=60000;', 'min=5000; max=3600000;')])
edit(src / 'desktop/portal/index.html', [
    ('max="8192"', 'max="384000"'),
    ('超时（毫秒）', '无数据等待（毫秒）'),
    ('min="5000" max="60000"', 'min="300000" max="3600000"'),
    ('<div class="form-footer"><span class="saved"></span><button type="submit" class="primary">保存 AI 配置',
     '<p>持续收到数据时不会因总时长而中断。只有连续无数据才计时；最低等待 5 分钟。输出上限须受模型支持，最高可填 384,000。</p><div class="form-footer"><span class="saved"></span><button type="submit" class="primary">保存 AI 配置')])
edit(src / 'desktop/portal/app.js', [('if(el)el.value=value;', "if(el)el.value=id==='ai'&&key==='timeout_ms'?Math.max(300000,value):value;")])
edit(ws / 'diagnostics/qpk_test_build.py', [("overlay / 'espclaw/port/espclaw_qpk.c']", "overlay / 'espclaw/port/espclaw_qpk.c', overlay / 'espclaw/port/claw_stream.c']")])
edit(ws / 'diagnostics/test_chat.py', [("'-I' + str(src),", "'-I' + str(src), '-I' + str(src.parent / 'espclaw/include'),")])
edit(ws / 'diagnostics/chat-tests/core_test.c', [("static atomic_int behavior", "void glass_chat_stream(uint32_t id, const struct claw_stream_event *event) { (void)id; (void)event; }\n\nstatic atomic_int behavior")])
edit(ws / 'diagnostics/test_shared_https.py', [("assert self.headers.get('Accept') == 'application/json'", "assert self.headers.get('Accept') in ('application/json', 'text/event-stream, application/json')")])
