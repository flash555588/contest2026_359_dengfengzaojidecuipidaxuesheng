#!/usr/bin/env python3
"""Short, real P4 music/lyrics/cover test using the existing music credentials."""
import argparse,json,re,time
from pathlib import Path
from test_jizura_device import Console

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);proof=a.output/'verification.json'
    result={'passed':False,'runs':[]}
    def save():proof.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    save()
    with (a.output/'console.log').open('w',encoding='utf-8') as log:
        c=Console(log)
        def command(text):return c.command('desktop ui '+text)
        def music():
            for _ in range(4):
                s=command('music-status');m=re.search(r'music state=(\d+) error=(-?\d+) search=(\d+) search_error=(-?\d+) result=(\d+).*?position_ms=(\d+)',s)
                if m:return dict(zip(['state','error','search','search_error','result','position_ms'],map(int,m.groups())))
            raise AssertionError('Incomplete read-only status')
        def ready():
            deadline=time.monotonic()+40
            while time.monotonic()<deadline:
                s=music();assert s['state']!=6,s
                raw=command('music-metadata-status');m=re.search(r'music-metadata lyrics=(\d+) lines=(\d+) artwork=(\d+) cover_ready=(\d+)',raw)
                if s['state']==2 and s['position_ms']>0 and m and int(m[1])==2 and int(m[2])>0 and m[3]=='1' and m[4]=='1':
                    return dict(zip(['lyrics','lines','artwork','cover_ready'],map(int,m.groups())))
                c.collect(.2)
            raise AssertionError('Timed lyrics and decoded cover did not both become ready')
        try:
            # Boot verification precedes asynchronous Wi-Fi/DHCP completion.
            # Check readiness before testing credentials; never confuse a
            # pre-DHCP host-unreachable response with a rejected music key.
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                network=c.command('ifconfig')
                wireless=network.split('wlan0',1)[-1] if 'wlan0' in network else ''
                if re.search(r'inet addr:(?!0\.0\.0\.0)\d+\.\d+\.\d+\.\d+',wireless):break
                c.collect(.3)
            else:raise AssertionError('Wi-Fi address is not ready')
            command('music-stop');command('home');command('music');command('music-search:海阔天空')
            deadline=time.monotonic()+25
            while time.monotonic()<deadline:
                s=music()
                if not s['search']:
                    assert not s['search_error'] and s['result'],s;break
                c.collect(.2)
            else:raise AssertionError('Search timed out')
            for repeat in range(2):
                began=time.monotonic();command('music-play-result');metadata=ready()
                result['runs'].append({'repeat':repeat,'ready_seconds_including_console':round(time.monotonic()-began,3),'metadata':metadata})
                save();command('music-stop')
            result['cache_directory']=c.command('ls /sdcard/music/cache')
            result['heap']=c.command('free');result['passed']=True;save()
        except Exception as error:result['error']=repr(error);save();raise
        finally:
            try:command('music-stop')
            finally:c.close()
    print('MUSIC_METADATA_DEVICE_OK',flush=True)
if __name__=='__main__':main()
