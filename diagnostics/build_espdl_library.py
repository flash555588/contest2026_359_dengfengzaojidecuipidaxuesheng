"""Build official ESP-DL 3.2.0 with a bounded NuttX portability layer."""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess

ws = Path(__file__).resolve().parent.parent
root = Path('/tmp/v3-desktop-espdl-20260915')
delivery = ws / '04-v3-20260913/espdl-quickapp'
port = delivery / 'overlay/apps/system/desktop/espdl_port'
upstream = ws / 'diagnostics/espdl-reference/esp-dl'
revision = 'dc380d450835d42f92777121a0cd4fc67d7c3a8c'
assert subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip() == revision
src = root / 'espdl-source'
build = root / 'espdl-build'
build.mkdir(exist_ok=True)

# Reproduce local adaptations from the pinned, unmodified dependency.
if not src.exists():
    shutil.copytree(upstream / 'esp-dl', src)
for source in (upstream / 'esp-dl').rglob('*'):
    if source.is_file() and source.suffix in ['.cpp', '.hpp', '.h', '.S']:
        target = src / source.relative_to(upstream / 'esp-dl')
        text = source.read_text()
        # Avoid GNU iostream global initialization (the inference API uses printf).
        text = text.replace('#include <iostream>', '')
        if source.name == 'dl_image_preprocessor.cpp':
            # The pinned image SIMD helpers differ from the scalar reference on
            # this P4/toolchain (see face-simd-9.json). Only image conversion is
            # scalar; all neural-network operators keep their P4 ISA kernels.
            text = text.replace('.transform()', '.transform<false>()')
        if source.name == 'dl_esp32p4_color_common.S':
            # Espressif backported the broadcast-load immediate fix to GCC 14.
            # Upstream's GCC-major heuristic then advances an 8-bit table by
            # four times the intended distance. Zero increment + scalar add is
            # unambiguous with both assembler versions and retains SIMD loads.
            end = text.index('#endif') + len('#endif')
            text = r'''.macro vldbc_8_ip q, a, n
    esp.vldbc.8.ip \q, \a, 0
    addi \a, \a, \n
.endm
.macro vldbc_16_ip q, a, n
    esp.vldbc.16.ip \q, \a, 0
    addi \a, \a, \n
.endm
''' + text[end:]
        if source.name == 'dl_model_base.hpp':
            text = text.replace('    Model() {}', '''    // Cooperative cancellation between operators; state belongs to one worker.
    bool run_cancellable(bool (*cancelled)())
    {
        for (auto *module : m_execution_plan) {
            if (cancelled()) return false;
            module->forward(m_model_context, RUNTIME_MODE_SINGLE_CORE);
        }
        return !cancelled();
    }
    Model() {}''')
        if source.name == 'dl_model_base.cpp':
            text = text.replace('#include <format>', '#include <algorithm>')
            text = text[:text.index('static std::string gen_sep_str')] + '\n} // namespace dl\n'
        if source.name == 'dl_define_private.hpp':
            text = text.replace('#if CONFIG_IDF_TARGET_ESP32P4\n',
                                '#if CONFIG_IDF_TARGET_ESP32P4 && !CONFIG_ESPDL_PORTABLE_KERNELS\n')
        if source.name == 'dl_module_base.hpp':
            begin = text.index('typedef struct {', text.index('class Module'))
            end = text.index('#pragma GCC diagnostic pop', begin) + len('#pragma GCC diagnostic pop')
            text = text[:begin] + '''// NuttX initial port: callers explicitly request SINGLE_CORE.
static inline void module_forward_dual_core(Module *op, void *a, void *b)
{
    op->forward_args(a);
    op->forward_args(b);
}
''' + text[end:]
        if source.name == 'dl_module_creator.hpp':
            # Operators used by MobileNetV2, MSR/MNP, Pico and the MNIST MLP.
            # The backend checks the entire graph before constructing Model.
            allowed = {'Conv','Add','GlobalAveragePool','Concat','PRelu','Gemm',
                       'Flatten','Transpose','RequantizeLinear'}
            text = '\n'.join(line for line in text.splitlines()
                             if 'this->register_module(' not in line or
                             re.search(r'register_module\("([^"]+)"', line)[1] in allowed) + '\n'
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_text() != text:
            target.write_text(text)

incdirs = ['dl','dl/tool/include','dl/tensor/include','dl/base','dl/base/isa','dl/base/isa/esp32p4',
           'dl/math/include','dl/model/include','dl/module/include','fbs_loader/include',
           'vision/image','vision/image/isa','vision/detect','vision/classification']
tc = Path('/home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin')
flags = ['-Os','-march=rv32imafc_zicsr_zifencei','-mabi=ilp32f','-std=gnu++20',
         '-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti',
         '-fno-threadsafe-statics','-fno-use-cxa-atexit','-D__NuttX__','-include','assert.h',
         '-isystem',str(root / 'nuttx/include'),'-I'+str(port / 'include'),'-I'+str(port.parent)]
flags += ['-I'+str(src / directory) for directory in incdirs]
hal=root/'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components'
flags += ['-I'+str(hal/d) for d in ['soc/esp32p4/include','soc/include',
          'esp_common/include','esp_hw_support/include','riscv/include']]
cache_header=next((hal/'soc/esp32p4').rglob('cache_reg.h'))
flags += ['-I'+str(cache_header.parent.parent)]
files = []
for directory in ['dl/tool/src','dl/tensor/src','dl/base','dl/math/src',
                  'dl/model/src','dl/module/src','vision/image',
                  'vision/detect','vision/classification']:
    for source in sorted((src / directory).glob('*.cpp')):
        if source.name in ['dl_image_jpeg.cpp', 'dl_image_ppa.cpp', 'dl_image_bmp.cpp']:
            continue
        files.append(source)
files += sorted(p for p in port.glob('*.cpp') if p.name != 'digit_backend.cpp')
files += sorted(port.glob('*.S'))
for directory in ['dl/tool/isa/esp32p4','dl/base/isa/esp32p4','vision/image/isa/esp32p4']:
    files += sorted((src/directory).glob('*.S'))
signature = hashlib.sha256((' '.join(flags) + '\n' + '\n'.join(
    str(p) + hashlib.sha256(p.read_bytes()).hexdigest() for p in
    sorted(list(src.rglob('*.hpp')) + list(src.rglob('*.S')) + list(port.glob('*.h')) +
           list((port / 'include').rglob('*')))
    if p.is_file())).encode()).hexdigest()
sigfile = build / 'headers.sha256'
unchanged = sigfile.exists() and sigfile.read_text() == signature

def compile_one(source):
    obj = build / (source.stem + '.o')
    if unchanged and obj.exists() and obj.stat().st_mtime >= source.stat().st_mtime:
        return obj, ''
    build_flags = flags if source.suffix != '.S' else [f for f in flags if not f.startswith('-std=')]
    if source.suffix == '.S':
        build_flags = [f for f in build_flags if f not in ['-include','assert.h']]
        build_flags += ['-march=rv32imafc_zicsr_zifencei_xespv_xesploop']
    result = subprocess.run([str(tc / 'riscv32-esp-elf-g++'), *build_flags, '-c', str(source), '-o', str(obj)],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return (obj if result.returncode == 0 else None), result.stdout

log = delivery / 'evidence/espdl-build.log'
print('Compiling', len(files), 'ESP-DL sources', flush=True)
objects = []
with log.open('w') as out, concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    for source, (obj, output) in zip(files, pool.map(compile_one, files)):
        out.write(str(source) + '\n' + output)
        if obj:
            objects.append(obj)
if len(objects) != len(files):
    lines = log.read_text().splitlines()
    errors = [i for i, line in enumerate(lines) if 'error:' in line or 'fatal error:' in line]
    for i in errors[:12]:
        print('\n'.join(lines[max(0, i-1):i+4]))
    print('Failed compilation units:', len(files) - len(objects), '; full log:', log)
    raise SystemExit(1)
archive = build / 'libqpk_espdl.a'
if archive.exists():
    archive.unlink()
subprocess.run([str(tc / 'riscv32-esp-elf-ar'), 'rcs', str(archive), *map(str, objects)], check=True)
sigfile.write_text(signature)
(delivery / 'evidence/espdl-build.json').write_text(json.dumps({
    'upstream': revision, 'abi': 'rv32imafc/ilp32f', 'portable_kernels': False,
    'pie_context_protected': True, 'hardware_loop_context_protected': True,
    'source_count': len(files), 'archive_bytes': archive.stat().st_size,
    'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
}, indent=2) + '\n')
print('ESP-DL library built:', archive, archive.stat().st_size, flush=True)
