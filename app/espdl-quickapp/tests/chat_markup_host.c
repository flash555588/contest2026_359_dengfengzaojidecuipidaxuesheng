/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_chat_markup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned parts(const char *text,bool code,char *out,size_t capacity)
{
  struct chat_markup it={.text=text,.cursor=text,.line=1};struct chat_markup_part token;
  unsigned count=0;size_t used=0;
  while(chat_markup_next(&it,&token)){
    assert(token.bytes&&token.bytes<=CHAT_MARKUP_BYTES);
    assert(((unsigned char)token.text[0]&0xc0)!=0x80);
    if(token.code==code){assert(used+token.bytes<capacity);memcpy(out+used,token.text,token.bytes);used+=token.bytes;count++;}
  }
  out[used]=0;return count;
}
int main(void)
{
  char out[20000];
  assert(parts("你好，世界。",false,out,sizeof(out))==1&&!strcmp(out,"你好，世界。"));
  const char *reply="说明\n```javascript\nconst s = \"你好\";\nconst marker = '```';\n```\n完成。";
  assert(parts(reply,true,out,sizeof(out))==1&&!strcmp(out,"const s = \"你好\";\nconst marker = '```';\n"));
  parts(reply,false,out,sizeof(out));assert(!strcmp(out,"说明\n完成。"));
  struct chat_markup it={.text=reply,.cursor=reply,.line=1};struct chat_markup_part p;
  assert(chat_markup_next(&it,&p)&&!p.code);assert(chat_markup_next(&it,&p)&&p.code&&p.line==1&&!strcmp(p.language,"javascript"));
  assert(!p.open);
  assert(parts("```ja",true,out,sizeof(out))==0);
  assert(parts("```js\n",true,out,sizeof(out))==0);
  assert(parts("```js\nconst x = 1;",true,out,sizeof(out))==1&&!strcmp(out,"const x = 1;"));
  const char *indent="  ```c\nint main(void) {}\n  ```";
  assert(parts(indent,true,out,sizeof(out))==1&&!strcmp(out,"int main(void) {}\n"));
  const char *wide="````text\n```\nkeep\n````\n";
  parts(wide,true,out,sizeof(out));assert(!strcmp(out,"```\nkeep\n"));
  char long_text[15000];long_text[0]=0;for(unsigned n=0;n<1900;n++)strcat(long_text,"中文\n");
  parts(long_text,false,out,sizeof(out));assert(!strcmp(long_text,out));
  char source[10000];strcpy(source,"```c\n");for(unsigned i=0;i<100;i++)strcat(source,"puts(\"ok\");\n");strcat(source,"```\n");
  it=(struct chat_markup){.text=source,.cursor=source,.line=1};unsigned expected=1,blocks=0;
  while(chat_markup_next(&it,&p)){assert(p.code&&p.line==expected);for(size_t i=0;i<p.bytes;i++)if(p.text[i]=='\n')expected++;blocks++;}
  assert(expected==101&&blocks==5);
  /* Every streamed prefix is safe, including boundaries in opening tags. */
  size_t n=strlen(reply);for(size_t end=0;end<=n;end++){
    char *prefix=strndup(reply,end);assert(prefix);
    it=(struct chat_markup){.text=prefix,.cursor=prefix,.line=1};unsigned guard=0;
    while(chat_markup_next(&it,&p)){assert(++guard<100);assert(p.text>=prefix&&p.text+p.bytes<=prefix+end);}
    free(prefix);
  }
  puts("CHAT_MARKUP_OK: fences, partial tags, UTF-8 chunk boundaries, literals, indentation, long text, line numbering, all streamed prefixes");
  const char *markdown="# 计划\n\n先写 **入口** 和 `main`。\n- 开始\n- 暂停\n> 先预览\n---\n";
  size_t skip;unsigned level;
  assert(chat_md_prefix("## 标题",&skip,&level)==CHAT_MD_HEADING&&skip==3&&level==2);
  assert(chat_md_prefix("- 项目",&skip,&level)==CHAT_MD_LIST&&skip==2&&!level);
  assert(chat_md_prefix("12. 项目",&skip,&level)==CHAT_MD_LIST&&level==1);
  assert(chat_md_prefix("---\n",&skip,&level)==CHAT_MD_RULE);
  assert(chat_md_prefix("#普通文本",&skip,&level)==CHAT_MD_TEXT);
  const char *inline_text="**bold** `main` *em* ~~old~~ [docs](https://example.org) \\*literal";
  const char *cursor=inline_text;struct chat_md_inline inline_part;unsigned strong=0,mono=0,links=0;
  while(chat_md_inline_next(&cursor,&inline_part)){strong+=inline_part.style==CHAT_MD_STRONG;mono+=inline_part.style==CHAT_MD_CODE;links+=inline_part.style==CHAT_MD_LINK;}
  assert(strong==1&&mono==1&&links==1);
  for(size_t end=0;end<=strlen(markdown);end++){
    char *prefix=strndup(markdown,end);assert(prefix);
    it=(struct chat_markup){.text=prefix,.cursor=prefix,.line=1,.markdown=true};unsigned guard=0;
    while(chat_markup_next(&it,&p)){assert(++guard<100);char block[CHAT_MARKUP_BYTES+1];memcpy(block,p.text,p.bytes);block[p.bytes]=0;
      cursor=block;unsigned spans=0;while(chat_md_inline_next(&cursor,&inline_part)){assert(++spans<1000);assert(inline_part.text>=block&&inline_part.text+inline_part.bytes<=block+p.bytes);}}
    free(prefix);
  }
  puts("CHAT_MARKDOWN_OK: headings, lists, quotes, rules, balanced inline styles, visible link destinations, every streamed prefix");
  return 0;
}
