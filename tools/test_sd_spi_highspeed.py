"""Run the actual CMD6 negotiator against bounded SPI/card responses."""
import argparse,json,subprocess
from pathlib import Path
from prepare_sd_spi_highspeed import FUNCTION,transform

p=argparse.ArgumentParser();p.add_argument('--nuttx',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output;out.mkdir(parents=True,exist_ok=True)
source=(a.nuttx/'drivers/mmcsd/mmcsd_spi.c').read_text()
assert transform(transform(source))==transform(source)
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_ESPCLAW_SD_SPI_HIGHSPEED 1
#define FAR
#define OK 0
#define MMCSD_CMDRESP_R1 1
#define MMCSD_SPIR1_OK 0
#define IS_SDV2(t) ((t)==2)
#define LOG_INFO 0
#define syslog(...) ((void)0)
struct spi_dev_s {int unused;};
struct mmcsd_slot_s {int type;struct spi_dev_s *spi;uint32_t spispeed,fast_read_clock;};
struct mmcsd_cmdinfo_s {unsigned cmd,response,crc;};
static unsigned commands,blocks,clocks,scenario;static uint32_t clock_values[2];
static int mmcsd_sendcmd(struct mmcsd_slot_s *s,const struct mmcsd_cmdinfo_s *c,uint32_t arg){
 (void)s;assert(c->cmd==0x46);assert(commands<2);
 assert(arg==(commands?0x80fffff1u:0x00fffff1u));commands++;
 return (scenario==1&&commands==1)||(scenario==5&&commands==2)?4:0;
}
static int mmcsd_recvblock(struct mmcsd_slot_s *s,uint8_t *b,int n){
 (void)s;assert(n==64);blocks++;memset(b,0,n);
 if(scenario==2)return -1;
 b[13]=scenario==3?0:2;b[29]=scenario==4?2:0;
 b[16]=scenario==6?0:1;return 0;
}
static uint32_t frequency(struct spi_dev_s *s,uint32_t hz){
 (void)s;assert(clocks<2);clock_values[clocks++]=hz;return scenario==7?50000000:hz;
}
#define SPI_SETFREQUENCY(s,hz) frequency(s,hz)
'''+FUNCTION+r'''
int main(void){
 struct spi_dev_s spi;struct mmcsd_slot_s s={.type=1,.spi=&spi,.spispeed=20000000,.fast_read_clock=123};
 assert(!mmcsd_fast_read_setup(&s)&&!s.fast_read_clock&&!commands);
 s.type=2;
 for(scenario=0;scenario<=7;scenario++){
  commands=blocks=clocks=0;s.fast_read_clock=123;
  int ret=mmcsd_fast_read_setup(&s);
  if(scenario==0){assert(!ret&&s.fast_read_clock==40000000&&commands==2&&blocks==2&&clocks==2);}
  else if(scenario==1||scenario==3||scenario==4){assert(!ret&&!s.fast_read_clock&&commands==1&&!clocks);}
  else assert(ret==-EIO&&!s.fast_read_clock);
  if(clocks)assert(clock_values[0]==40000000&&clock_values[1]==20000000);
 }
 return 0;
}
'''
(out/'test.c').write_text(code)
subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(out/'test.c'),'-o',str(out/'test')],check=True)
subprocess.run([str(out/'test')],check=True)
(out/'RESULT.json').write_text(json.dumps({'passed':True,'physical_card_tested':False,'cases':8,'write_clock_retained':True,'read_retry_added':False},indent=2))
print('SD_SPI_HIGHSPEED_NEGOTIATION_OK')
