/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define CHAT_MARKUP_BYTES 768
struct chat_markup {
  const char *text,*cursor;
  bool code,markdown;
  unsigned line,fence;
  char language[25];
};
enum chat_md_kind {CHAT_MD_TEXT,CHAT_MD_HEADING,CHAT_MD_LIST,CHAT_MD_QUOTE,CHAT_MD_RULE};
static inline enum chat_md_kind chat_md_prefix(const char *p,size_t *skip,unsigned *level)
{
  *skip=0;*level=0;const char *s=p;
  while(*s==' '&&s-p<3)s++;
  if(*s=='#') {const char *q=s;while(*q=='#'&&q-s<6)q++;
    if(*q==' '){*skip=q-p+1;*level=q-s;return CHAT_MD_HEADING;}}
  if(*s=='>'&&(!s[1]||s[1]==' ')){*skip=s-p+1+(s[1]==' ');return CHAT_MD_QUOTE;}
  if((*s=='-'||*s=='*'||*s=='_')&&s[1]==*s&&s[2]==*s){
    const char *q=s;while(*q==*s||*q==' '||*q=='\r')q++;
    if(!*q||*q=='\n')return CHAT_MD_RULE;
  }
  if((*s=='-'||*s=='*'||*s=='+')&&s[1]==' '){*skip=s-p+2;return CHAT_MD_LIST;}
  const char *q=s;while(*q>='0'&&*q<='9'&&q-s<9)q++;
  if(q>s&&(*q=='.'||*q==')')&&q[1]==' '){*level=1;return CHAT_MD_LIST;}
  return CHAT_MD_TEXT;
}
enum chat_md_inline_style {CHAT_MD_NORMAL,CHAT_MD_STRONG,CHAT_MD_EM,CHAT_MD_CODE,CHAT_MD_LINK,CHAT_MD_STRIKE};
struct chat_md_inline {const char *text;size_t bytes;enum chat_md_inline_style style;};
/* Balanced inline constructs only. Incomplete streamed delimiters stay literal.
 * URLs remain visible text; rendering never opens or fetches model-provided URLs. */
static inline bool chat_md_inline_next(const char **cursor,struct chat_md_inline *out)
{
  const char *p=*cursor;if(!*p)return false;
  *out=(struct chat_md_inline){.text=p,.bytes=1,.style=CHAT_MD_NORMAL};
  if(*p=='\\'&&p[1]&&strchr("\\`*_[]()~#>",p[1])){out->text=p+1;*cursor=p+2;return true;}
  unsigned marker=0;const char *end=NULL;
  if(*p=='`'){marker=1;end=strchr(p+1,'`');out->style=CHAT_MD_CODE;}
  else if((*p=='*'||*p=='_')&&p[1]==*p){marker=2;char close[3]={*p,*p,0};end=strstr(p+2,close);out->style=CHAT_MD_STRONG;}
  else if(*p=='*'){marker=1;end=strchr(p+1,'*');out->style=CHAT_MD_EM;}
  else if(*p=='~'&&p[1]=='~'){marker=2;end=strstr(p+2,"~~");out->style=CHAT_MD_STRIKE;}
  else if(*p=='['){const char *label=strchr(p+1,']');
    if(label&&label[1]=='('){end=strchr(label+2,')');if(end){
      /* Preserve destination as text on the next iteration. */
      out->text=p+1;out->bytes=label-p-1;out->style=CHAT_MD_LINK;*cursor=label+1;return true;
    }}
  }
  if(marker&&end&&end>p+marker&&!memchr(p,'\n',end-p)){
    out->text=p+marker;out->bytes=end-out->text;*cursor=end+marker;return true;
  }
  out->style=CHAT_MD_NORMAL;
  size_t n=1;while(p[n]&&!strchr("\\`*_[~",p[n]))n++;
  out->bytes=n;*cursor=p+n;return true;
}
struct chat_markup_part {
  const char *text;
  size_t bytes;
  bool code,open;
  unsigned line;
  char language[25];
};
static inline bool chat_markup_fence(const char *p,bool code,unsigned minimum,
                                     size_t *bytes,unsigned *ticks,char language[25])
{
  const char *start=p;unsigned spaces=0;
  while(*p==' '&&spaces<3){p++;spaces++;}
  *ticks=0;while(p[*ticks]=='`')(*ticks)++;
  if(*ticks<3||(code&&*ticks<minimum))return false;
  p+=*ticks;
  if(code){while(*p==' '||*p=='\t'||*p=='\r')p++;if(*p&&*p!='\n')return false;}
  else {
    unsigned n=0;
    for(;*p&&*p!='\n';p++){
      unsigned char c=*p;
      if(c=='`')return false;
      if(n<24&&((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='+'||c=='#'||c=='-'||c=='_'))language[n++]=c;
    }
    language[n]=0;
    /* A streamed opening marker is incomplete until its newline arrives. */
    if(!*p)return false;
  }
  *bytes=(size_t)(p-start)+(*p=='\n');return true;
}
static inline bool chat_markup_next(struct chat_markup *it,struct chat_markup_part *out)
{
  while(*it->cursor){
    const char *p=it->cursor;size_t marker=0;unsigned ticks=0;char language[25]={0};
    bool at_line=p==it->text||p[-1]=='\n';
    if(at_line&&chat_markup_fence(p,it->code,it->fence,&marker,&ticks,language)){
      it->cursor+=marker;it->code=!it->code;
      if(it->code){it->line=1;it->fence=ticks;memcpy(it->language,language,sizeof(language));}
      continue;
    }
    /* Keep a partial opener out of the live view; it is parsed on next delta. */
    if(at_line&&!it->code&&!strncmp(p,"```",3)&&!strchr(p,'\n'))return false;
    size_t n=0,last_line=0;unsigned lines=0;
    size_t prefix;unsigned heading;
    enum chat_md_kind block=chat_md_prefix(p,&prefix,&heading);
    while(p[n]&&n<CHAT_MARKUP_BYTES){
      if(n&&p[n-1]=='\n'){
        size_t ignored;unsigned count;char tag[25]={0};
        if(chat_markup_fence(p+n,it->code,it->fence,&ignored,&count,tag))break;
        if(!it->code&&!strncmp(p+n,"```",3)&&!strchr(p+n,'\n'))break;
        if(it->markdown&&!it->code&&(block!=CHAT_MD_TEXT||
           chat_md_prefix(p+n,&prefix,&heading)!=CHAT_MD_TEXT))break;
      }
      if(p[n++]=='\n'){last_line=n;if(it->code&&++lines==20)break;}
    }
    if(n==CHAT_MARKUP_BYTES&&p[n]){
      if(it->code&&last_line)n=last_line;
      else while(n&&(((unsigned char)p[n]&0xc0)==0x80))n--;
    }
    if(!n)return false;
    memset(out,0,sizeof(*out));out->text=p;out->bytes=n;out->code=it->code;out->line=it->line;
    memcpy(out->language,it->language,sizeof(out->language));
    for(size_t i=0;i<n;i++)if(p[i]=='\n')it->line++;
    it->cursor+=n;
    size_t ignored;unsigned count;char tag[25]={0};
    out->open=it->code&&!*it->cursor;
    if(it->code&&chat_markup_fence(it->cursor,true,it->fence,&ignored,&count,tag))out->open=false;
    return true;
  }
  return false;
}
