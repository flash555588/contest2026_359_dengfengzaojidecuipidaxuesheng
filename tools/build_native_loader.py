"""Compile the generic native module loader into a firmware archive."""
from pathlib import Path
import hashlib,json,shlex,subprocess,shutil,sys

NAMES=['native_host.c','native_nuttx.c','native_async.c','native_cache.c','native_exports.c','native_text_heap.c']

def build(root,out,env):
 root=Path(root);out=Path(out);desktop=root/'apps/system/desktop';native=desktop/'native'
 # Generated table must match exports.txt; never build a stale symbol set.
 subprocess.run([sys.executable,str(Path(__file__).with_name('generate_native_exports.py')),'--native',str(native),'--check'],check=True)
 flags=subprocess.check_output(['make','-s','-C',str(desktop),'APPDIR='+str(root/'apps'),
  'TOPDIR='+str(root/'nuttx'),'CROSSDEV='+str(Path(shutil.which('riscv32-esp-elf-gcc',path=env.get('PATH'))).parent/'riscv32-esp-elf-'),
  '--eval=native-flags:;$(info NATIVE_CC=$(CC))$(info NATIVE_CFLAGS=$(CFLAGS))','native-flags'],env=env,text=True)
 cc=shlex.split(next(x.split('=',1)[1] for x in flags.splitlines() if x.startswith('NATIVE_CC=')))
 opts=shlex.split(next(x.split('=',1)[1] for x in flags.splitlines() if x.startswith('NATIVE_CFLAGS=')))
 sdk=root/'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty'
 opts += ['-I'+str(native),'-include','nuttx/compiler.h']
 opts += ['-I'+str(sdk/'components'/x) for x in ['esp_mm/include','esp_common/include','esp_hw_support/include','hal/include']]
 opts += ['-I'+str(sdk/'nuttx/esp32p4/include')]
 folder=out/'native-loader';folder.mkdir(exist_ok=True);objects=[]
 for name in NAMES:
  target=folder/(name+'.o')
  with (folder/(name+'.log')).open('w') as log:
   subprocess.run([*cc,*opts,'-c',str(native/name),'-o',str(target)],env=env,cwd=desktop,stdout=log,stderr=subprocess.STDOUT,check=True)
  objects.append(target)
 archive=folder/'libnative-loader.a';archive.unlink(missing_ok=True)
 subprocess.run([shutil.which('riscv32-esp-elf-ar',path=env.get('PATH')),'rcs',str(archive),*map(str,objects)],env=env,check=True)
 (folder/'BUILD.json').write_text(json.dumps(dict(archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),sources={name:hashlib.sha256((native/name).read_bytes()).hexdigest() for name in NAMES}),indent=2)+'\n')
 return archive
