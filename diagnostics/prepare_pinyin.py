from pathlib import Path
import hashlib
import unicodedata
import urllib.request

ws = Path(__file__).resolve().parent.parent
vendor = ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop/music_vendor'
for name in ('pinyin.txt', 'LICENSE'):
    target = vendor/('pinyin-' + name)
    if not target.exists():
        target.write_bytes(urllib.request.urlopen('https://raw.githubusercontent.com/mozillazg/pinyin-data/master/'+name, timeout=30).read())
    print(target.name, hashlib.sha256(target.read_bytes()).hexdigest())
dictionary = {}
for line in (vendor/'pinyin-pinyin.txt').read_text(encoding='utf-8').splitlines():
    if not line.startswith('U+'): continue
    code, pronunciations = line.split(':',1)
    c = chr(int(code[2:],16))
    if not '\u4e00' <= c <= '\u9fff': continue
    # Restrict candidates to Simplified GB2312 where the display font is complete.
    try: c.encode('gb2312')
    except UnicodeEncodeError: continue
    for pron in pronunciations.split('#')[0].strip().split(','):
        pron = pron.replace('ü','v').replace('ǖ','v').replace('ǘ','v').replace('ǚ','v').replace('ǜ','v')
        key = ''.join(x for x in unicodedata.normalize('NFD',pron) if x.isascii() and x.isalpha()).lower()
        if key and len(key) <= 6 and c not in dictionary.setdefault(key,[]): dictionary[key].append(c)
common = '的一是不了人我在有他这为之大来以个中上们到说国和地也子时道出而要于就下得可你年生自会那后能对着事其里所去行过家十用发天如然作方成者多日都三小军二无同么经法当起与好看学进种将还分此心前面又定见只主没公从知全工使情明性两民月声想实问高最把机给正每女现些向回长儿相很意动点白音乐周杰伦晴夜稻香告气球薛谦陈奕迅邓紫棋林俊五月天许嵩孙燕姿爱喜欢收藏搜索歌曲朋友海阔空梦红豆再青春花雨雪风水电网络密码文件新建目录重命名'
rows=[]
for key, chars in sorted(dictionary.items()):
    chars.sort(key=lambda c: (common.find(c) if c in common else 10000, ord(c)))
    rows.append('  {"'+key+'", "'+''.join(chars)+'"},')
(vendor.parent/'glass_pinyin_dict.inc').write_text('/* Generated from mozillazg/pinyin-data; see music_vendor/pinyin-LICENSE. */\nstatic const struct pinyin_entry pinyin_dictionary[] = {\n'+'\n'.join(rows)+'\n};\n',encoding='utf-8')
print('Syllables:',len(rows),'candidate entries:',sum(map(len,dictionary.values())))
