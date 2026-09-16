"""Remove the app-size quotas across authoring, transport, installer and launch."""
from pathlib import Path
root=Path(__file__).resolve().parent.parent/'04-v3-20260913/espdl-quickapp/overlay/apps/system'
def edit(path, changes):
    p=root/path; s=p.read_text(encoding='utf-8')
    for old,new in changes:
        assert old in s, (path,old)
        s=s.replace(old,new)
    p.write_text(s,encoding='utf-8')
edit('desktop/qpk_limits.h', [('/* One common source limit for AI drafts, web installation and desktop launch.\n * Buffers grow to actual input size; this does not reserve 1 MiB per app. */\n#define QPK_SOURCE_MAX (1024u * 1024u)\n\n','')])
edit('desktop/glass_qpk_builder.h',[('#define QPK_DRAFT_SOURCE_MAX QPK_SOURCE_MAX\n','')])
edit('desktop/glass_qpk_builder.c',[
    ('#define DRAFT_DISK_MAX (QPK_DRAFT_SOURCE_MAX * 6 + 4096)\n',''),
    ('if (!json || strlen(json) > DRAFT_DISK_MAX) return error_json("invalid_arguments", -EFBIG, NULL);','if (!json) return error_json("invalid_arguments", -EINVAL, NULL);'),
    ('DRAFT_DISK_MAX','SIZE_MAX - 1'),('QPK_DRAFT_SOURCE_MAX','SIZE_MAX - 1'),
    ('full UTF-8 source <= 1 MiB','complete UTF-8 source'),
    ('(size_t)st.st_size > maximum','(uintmax_t)st.st_size > maximum')])
edit('desktop/desktop_main.c',[('size > QPK_SOURCE_MAX','(uintmax_t)size >= SIZE_MAX')])
edit('desktop/glass_portal_files.c',[('#include "qpk_limits.h"\n',''),('||st.st_size>QPK_SOURCE_MAX','')])
edit('espclaw/include/claw_qpk.h', [('/* Bounded by available RAM; enough for a 1 MiB source and JSON escaping. */\n#define CLAW_QPK_TOOL_BUDGET (8 * 1024 * 1024)\n',''),('  size_t bytes;\n','')])
p=root/'espclaw/port/espclaw_qpk.c'; s=p.read_text(encoding='utf-8')
start=s.index('  /* Reads can be measured'); end=s.index('  cJSON *args =',start)
s=s[:start]+s[end:]
s=s.replace('  session->bytes += length;\n','').replace('  session->bytes = 0;\n','')
start=s.index('  if (session->bytes + strlen(*out)'); end=s.index('  cJSON *result =',start)
s=s[:start]+s[end:]; p.write_text(s,encoding='utf-8')
edit('espclaw/port/claw_stream.c', [('#define EVENT_LIMIT (8u * 1024 * 1024)','#define EVENT_LIMIT (SIZE_MAX - 1)'),('#define TOOL_LIMIT (6u * 1024 * 1024 + 4096)','#define TOOL_LIMIT (SIZE_MAX - 1)'),('if (count > limit - b->size)','if (b->size > limit || count > limit - b->size)'),('while (capacity < need) capacity *= 2;','while (capacity < need) { if (capacity > SIZE_MAX / 2) { capacity = need; break; } capacity *= 2; }')])
edit('espclaw/port/http_webclient.c', [('#define RESPONSE_LIMIT (16 * 1024 * 1024)\n#define REQUEST_LIMIT (16 * 1024 * 1024)\n',''),('if (count > RESPONSE_LIMIT - buffer->size) return -EFBIG;','if (count > SIZE_MAX - 1 - buffer->size) return -EOVERFLOW;'),('strlen(req->body) > REQUEST_LIMIT || ',''),('#include <errno.h>','#include <errno.h>\n#include <stdint.h>')])
edit('desktop/glass_portal.h',[('#define PORTAL_MAX_FILE (2 * 1024 * 1024)\n#define PORTAL_MAX_FILES 128\n','')])
edit('desktop/glass_portal_http.c',[
    ('static unsigned install_count; static size_t install_bytes;\n',''),
    ('||n>PORTAL_MAX_FILE','||n>SIZE_MAX'),
    ('    if(install_count>=PORTAL_MAX_FILES || install_bytes+size>PORTAL_MAX_FILE) { result(fd,-E2BIG,NULL); return; }\n',''),
    ('    if(!ret) { install_count++; install_bytes+=size; } result(fd,ret,NULL); return;','    result(fd,ret,NULL); return;'),
    (' if(!ret) { install_count=0; install_bytes=0; }',''),
    ('if(w<=0) { ret=-EIO; break; }','if(w<=0) { ret=w<0?-errno:-EIO; break; }')])
edit('desktop/portal/app.js',[
    ("if(f.size>2097152)throw Error(f.name+' 超过 2 MB');",''),
    ('if(count>160||off+', 'if(off+'),('||n>2097152',''),
    ('包包含加密、重复或过大的文件','包包含加密或重复的文件'),
    ("if(off!==end||entries.length>128||total>2097152)throw Error('解压后最多 2 MB / 128 个文件');","if(off!==end)throw Error('安装包目录损坏');"),
    ("if(f.size>3145728)throw Error('压缩包不能超过 3 MB');",''),
    ('||files[entry].length>131072',''),
    ('包需要有效名称、包名和不超过 128 KB 的 JavaScript 入口','包需要有效名称、包名和 JavaScript 入口')])
print('Removed fixed application-size and tool-context quotas')
