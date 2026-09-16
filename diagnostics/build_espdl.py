"""Build and audit ESP-DL firmware with an 8/4/4 MiB program/model/data layout."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
from prepare_tls import prepare_tls, TLS_THREAD_CFLAGS

ws=Path(__file__).resolve().parent.parent
delivery=ws/'04-v3-20260913/espdl-quickapp'
validation=delivery/'evidence/firmware-validation.json'
if validation.exists():
    previous=json.loads(validation.read_text())
    previous['build_in_progress']=True
    validation.write_text(json.dumps(previous,indent=2)+'\n')
root=Path('/tmp/v3-desktop-espdl-20260915')
tc=Path('/home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin')
env=os.environ.copy()
env['PATH']=f'{tc}:/home/streetartist/.local/bin:'+env['PATH']
overlay=delivery/'overlay'

# Generate the actual JS resource compiled into the image.
desktop=overlay/'apps/system/desktop'
from prepare_qpk_guide import prepare_qpk_guide
prepare_qpk_guide(overlay/'apps/system')
from prepare_portal import prepare_portal
prepare_portal(desktop)
from prepare_chat import prepare_chat
prepare_chat(overlay/'apps/system/espclaw')
js=(desktop/'espdl/app.js').read_text()
resource='/* Generated from espdl/app.js. SPDX-License-Identifier: Apache-2.0 */\n'
resource+='static const char g_espdl_app_js[] =\n'
resource+='\n'.join(json.dumps(line,ensure_ascii=False) for line in js.splitlines(keepends=True))+';\n'
resource+='const char *espdl_get_app_js(unsigned int *len) { *len=sizeof(g_espdl_app_js)-1; return g_espdl_app_js; }\n'
(desktop/'espdl_resource.c').write_text(resource)
ca=(desktop/'weather_root_ca.pem').read_text(encoding='ascii')
assert 'BEGIN CERTIFICATE' in ca
(desktop/'weather_root_ca.inc').write_text(
    '/* Generated from the verified public TLS root. */\n'
    'static const unsigned char weather_root_ca[] =\n' +
    '\n'.join(json.dumps(line) for line in ca.splitlines(keepends=True)) + ';\n')
# The built-in camera app is embedded the same way, so both app sources in this
# tree are always the ones compiled into the image.
camera_js=(desktop/'camera/app.js').read_text()
camera_resource='/* Generated from camera/app.js. SPDX-License-Identifier: Apache-2.0 */'
camera_resource+='\nstatic const char g_camera_app_js[] =\n'
camera_resource+='\n'.join(json.dumps(line,ensure_ascii=False) for line in camera_js.splitlines(keepends=True))
camera_resource+=';\nconst char *camera_get_app_js(unsigned int *len) { *len=sizeof(g_camera_app_js)-1; return g_camera_app_js; }\n'
(desktop/'camera_resource.c').write_text(camera_resource)

# The OuO mood console is embedded from its own source too.
ouo_js=(desktop/'ouo/app.js').read_text()
ouo_resource='/* Generated from ouo/app.js. SPDX-License-Identifier: Apache-2.0 */'
ouo_resource+='\nstatic const char g_ouo_app_js[] =\n'
ouo_resource+='\n'.join(json.dumps(line,ensure_ascii=False) for line in ouo_js.splitlines(keepends=True))
ouo_resource+=';\nconst char *ouo_get_app_js(unsigned int *len) { *len=sizeof(g_ouo_app_js)-1; return g_ouo_app_js; }\n'
(desktop/'ouo_resource.c').write_text(ouo_resource)

recorder_js = (desktop/'recorder/app.js').read_text()
recorder_resource = '/* Generated from recorder/app.js. SPDX-License-Identifier: Apache-2.0 */\n'
recorder_resource += 'static const char g_recorder_app_js[] =\n'
recorder_resource += '\n'.join(json.dumps(line, ensure_ascii=False) for line in recorder_js.splitlines(keepends=True))
recorder_resource += ';\nconst char *recorder_get_app_js(unsigned int *len) { *len=sizeof(g_recorder_app_js)-1; return g_recorder_app_js; }\n'
(desktop/'recorder_resource.c').write_text(recorder_resource)

for source in overlay.rglob('*'):
    if source.is_file():
        target=root/source.relative_to(overlay)
        target.parent.mkdir(parents=True,exist_ok=True)
        data=source.read_bytes()
        if source.name.startswith('Kconfig'): data=data.replace(b'\r\n',b'\n')
        if not target.exists() or target.read_bytes()!=data: target.write_bytes(data)
config=(root/'nuttx/.config').read_text()
# Hardware APIs use existing NuttX drivers and a small, explicit extension-pin
# map. I2C0/audio/display/C6 pins remain owned by their system drivers.
import re
hardware_config = {
    'CONFIG_ESPRESSIF_I2C1': 'y', 'CONFIG_ESPRESSIF_I2C1_MASTER_MODE': 'y',
    'CONFIG_ESPRESSIF_I2C1_SDAPIN': '1', 'CONFIG_ESPRESSIF_I2C1_SCLPIN': '2',
    'CONFIG_ESPRESSIF_SPI2': 'y', 'CONFIG_SPI_DRIVER': 'y', 'CONFIG_SPI_EXCHANGE': 'y',
    'CONFIG_ESPRESSIF_SPI_SWCS': 'y',
    'CONFIG_ESPRESSIF_SPI2_MODE_MASTER': 'y', 'CONFIG_ESPRESSIF_SPI2_CSPIN': '0',
    'CONFIG_ESPRESSIF_SPI2_CLKPIN': '3', 'CONFIG_ESPRESSIF_SPI2_MOSIPIN': '4', 'CONFIG_ESPRESSIF_SPI2_MISOPIN': '5',
    # The on-board microSD socket is the SD1 pin group. Use the independent
    # SPI3 host because SDMMC slot 1 is continuously owned by the C6 radio.
    'CONFIG_ESPRESSIF_SPI3': 'y', 'CONFIG_ESPRESSIF_SPI3_MODE_MASTER': 'y',
    'CONFIG_ESPRESSIF_SPI3_CSPIN': '42', 'CONFIG_ESPRESSIF_SPI3_CLKPIN': '43',
    'CONFIG_ESPRESSIF_SPI3_MOSIPIN': '44', 'CONFIG_ESPRESSIF_SPI3_MISOPIN': '39',
    'CONFIG_ESPRESSIF_UART1': 'y', 'CONFIG_ESPRESSIF_UART1_TXPIN': '4', 'CONFIG_ESPRESSIF_UART1_RXPIN': '5',
    'CONFIG_SERIAL_TERMIOS': 'y', 'CONFIG_ESPRESSIF_LEDC': 'y',
}
hardware_trace = os.environ.get('QPK_HARDWARE_TRACE') == '1'
for subsystem in ('AUDIO', 'I2S'):
    hardware_config[f'CONFIG_DEBUG_{subsystem}'] = 'y'
    hardware_config[f'CONFIG_DEBUG_{subsystem}_ERROR'] = 'y'
    hardware_config[f'CONFIG_DEBUG_{subsystem}_WARN'] = 'y'
    # I2S INFO also dumps samples on some versions; never enable it.
    hardware_config[f'CONFIG_DEBUG_{subsystem}_INFO'] = 'y' if hardware_trace and subsystem == 'AUDIO' else 'n'
for channel, pin in enumerate((0, 3, 4, 6)):
    hardware_config[f'CONFIG_ESPRESSIF_LEDC_TIMER{channel}'] = 'y'
    hardware_config[f'CONFIG_ESPRESSIF_LEDC_TIMER{channel}_CHANNELS'] = '1'
    hardware_config[f'CONFIG_ESPRESSIF_LEDC_TIMER{channel}_RESOLUTION'] = '13'
    hardware_config[f'CONFIG_ESPRESSIF_LEDC_CHANNEL{channel}_PIN'] = str(pin)
for key, value in hardware_config.items():
    config = re.sub(r'^(?:' + key + r'=.*|# ' + key + r' is not set)\n?', '', config, flags=re.M)
    config += key + '=' + value + '\n'
# The SMART flash translation layer reports which accounting value went wrong
# only when the file-system error level is compiled in.  Keep it on by default
# so a storage failure on the device is diagnosable; QPK_FS_TRACE=0 turns it
# off for a smaller image.
fs_trace = os.environ.get('QPK_FS_TRACE') != '0'
# The default syslog channel is the low-level UART, which is not the NSH
# console on this board.  Route debug output to /dev/console so a storage
# failure is visible on the console the user actually reads.
for key in ('CONFIG_DEBUG_FS', 'CONFIG_DEBUG_FS_ERROR', 'CONFIG_DEBUG_FS_WARN',
            'CONFIG_SYSLOG_CONSOLE'):
    config = re.sub(r'^(?:' + key + r'=.*|# ' + key + r' is not set)\n?', '', config, flags=re.M)
    config += (key + '=y' if fs_trace else '# ' + key + ' is not set') + '\n'
config=config.replace('# CONFIG_LV_USE_QRCODE is not set','CONFIG_LV_USE_QRCODE=y')
# A listening server needs the backlog: browsers open several parallel
# connections for one page, and without it the extra SYNs are dropped.
config=config.replace('# CONFIG_NET_TCPBACKLOG is not set','CONFIG_NET_TCPBACKLOG=y')
# A browser uses several connections while loading a page. Keep a small boot
# pool and allow bounded, reclaimable growth alongside weather/AI requests.
import re
for key,value in [('CONFIG_NET_TCP_ALLOC_CONNS','1'),('CONFIG_NET_TCP_MAX_CONNS','32')]:
    if re.search(r'^'+key+r'=',config,re.M):
        config=re.sub(r'^'+key+r'=.*$',key+'='+value,config,flags=re.M)
    else:
        config+='\n'+key+'='+value+'\n'
# TCP read-ahead shares one small global IOB pool. Bound every connection so
# a paused or cancelled media stream cannot starve DNS and concurrent HTTPS.
for key, value in [('CONFIG_NET_RECV_BUFSIZE', '3072'),
                   ('CONFIG_NET_MAX_RECV_BUFSIZE', '3072')]:
    config = re.sub(r'^(?:' + key + r'=.*|# ' + key + r' is not set)\n?', '', config, flags=re.M)
    config += key + '=' + value + '\n'
# The socket has no wired card-detect/write-protect signals. SPI status treats
# the boot-time card as present; FAT supports Windows-compatible UTF-8 names.
sd_config = {
    'CONFIG_MMCSD_SPI': 'y', 'CONFIG_MMCSD_HAVE_CARDDETECT': 'n',
    'CONFIG_MMCSD_HAVE_WRITEPROTECT': 'n',
    'CONFIG_NSH_MMCSDSPIPORTNO': '3', 'CONFIG_NSH_MMCSDMINOR': '0',
    'CONFIG_NSH_MMCSDSLOTNO': '0', 'CONFIG_FS_FAT': 'y',
    'CONFIG_FAT_COMPUTE_FSINFO': 'y', 'CONFIG_FAT_LCNAMES': 'y',
    'CONFIG_FAT_LFN': 'y', 'CONFIG_FAT_LFN_ALIAS_HASH': 'y',
    'CONFIG_FAT_LFN_UTF8': 'y', 'CONFIG_FS_FATTIME': 'y',
}
for key, value in sd_config.items():
    config = re.sub(r'^(?:' + key + r'=.*|# ' + key + r' is not set)\n?', '', config, flags=re.M)
    config += (key + '=' + value if value != 'n' else '# ' + key + ' is not set') + '\n'
# Signed CDN media URLs exceed the weather-only client's 100-byte path limit.
# The webclient work object is private; this changes no public struct ABI.
for key, value in [('CONFIG_WEBCLIENT_MAXFILENAME', '2048'),
                   ('CONFIG_WEBCLIENT_MAXHOSTNAME', '128'),
                   ('CONFIG_WEBCLIENT_MAXHTTPLINE', '1024')]:
    import re
    config = re.sub(r'^' + key + r'=.*$', key + '=' + value, config, flags=re.M)
if (root/'nuttx/.config').read_text() != config:
    (root/'nuttx/.config').write_text(config)
    subprocess.run(['make', 'olddefconfig'], cwd=root/'nuttx', env=env, check=True,
                   stdout=subprocess.DEVNULL)
    (root/'apps/netutils/webclient/webclient.c').touch()
# Application.mk does not rebuild objects when per-file CFLAGS change.
qr_stamp=root/'espdl-build/portal-qrcode-enabled'
if not qr_stamp.exists():
    for source in (root/'apps/graphics/lvgl/lvgl/src/libs/qrcode').glob('*.c'):
        source.touch()
    qr_stamp.parent.mkdir(parents=True,exist_ok=True)
    qr_stamp.touch()
# Invalidate only the two JPEG units when their optimization flags change.
jpeg_flags='\n'.join(line for line in (desktop/'Makefile').read_text().splitlines()
                     if line.startswith(('qpk_tjpgd.c_CFLAGS','qpk_mjpeg.c_CFLAGS')))
jpeg_stamp=root/'espdl-build/jpeg-cflags.txt'
if not jpeg_stamp.exists() or jpeg_stamp.read_text()!=jpeg_flags:
    for name in ['qpk_tjpgd.c','qpk_mjpeg.c']:
        (root/'apps/system/desktop'/name).touch()
for key in ['CONFIG_ARCH_FPU=y','CONFIG_HAVE_CXX=y','CONFIG_LIBCXXTOOLCHAIN=y']:
    assert key in config,key
for name in ['apps/graphics/lvgl/lvgl','apps/wireless/bluetooth/nimble/mynewt-nimble',
             'apps/netutils/cjson/cJSON','apps/interpreters/quickjs/quickjs']:
    (root/name).touch()
tls=prepare_tls(root,ws/'diagnostics/downloads')
tls_build=root/'tls-build-espdl'
abi=['-march=rv32imafc_zicsr_zifencei','-mabi=ilp32f']
includes=f'-I{tls}/include -I{tls}/tf-psa-crypto/include -I{tls}/tf-psa-crypto/drivers/builtin/include '+ ' '.join(TLS_THREAD_CFLAGS)
log=delivery/'evidence/build.log'
with log.open('w') as out:
    # olddefconfig removes generated headers. TLS uses these same target types
    # and must be compiled only after the NuttX configuration is materialized.
    subprocess.run(['make','context',f'CROSSDEV={tc}/riscv32-esp-elf-',
                    f'ESPCLAW_TLS_CFLAGS={includes}'],cwd=root/'nuttx',env=env,
                   check=True,stdout=out,stderr=subprocess.STDOUT)
    commands=[]
    flags=' '.join(['-Os',*abi,'-D__NuttX__','-Dunix','-isystem',str(root/'nuttx/include'),
                   '-ffunction-sections','-fdata-sections',*TLS_THREAD_CFLAGS])
    tls_stamp=tls_build/'firmware-cflags.txt'
    if not (tls_build/'library/libmbedtls.a').exists() or not tls_stamp.exists() or tls_stamp.read_text()!=flags:
        commands += [['cmake','-S',str(tls),'-B',str(tls_build),'-DCMAKE_SYSTEM_NAME=Generic',
                      f'-DCMAKE_C_COMPILER={tc}/riscv32-esp-elf-gcc','-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
                      f'-DCMAKE_C_FLAGS={flags}','-DENABLE_TESTING=OFF','-DENABLE_PROGRAMS=OFF',
                      '-DMBEDTLS_FATAL_WARNINGS=OFF','-DGEN_FILES=ON','-DDISABLE_PACKAGE_CONFIG_AND_INSTALL=ON'],
                     ['cmake','--build',str(tls_build),'-j8']]
    for command in commands:
        result=subprocess.run(command,env=env,stdout=out,stderr=subprocess.STDOUT)
        if result.returncode:
            out.flush();print('\n'.join(log.read_text(errors='replace').splitlines()[-45:]))
            raise SystemExit(result.returncode)
    tls_stamp.write_text(flags)
    # Redirect only the vendor model library's allocation/logging API symbols.
    # ELF architecture and ABI flags are never rewritten or ignored.
    fbs=root/'espdl-build/libfbs_model_nuttx.a'
    original=root/'espdl-source/fbs_loader/lib/esp32p4/libfbs_model.a'
    rename={'heap_caps_malloc':'qpk_dl_malloc','heap_caps_free':'qpk_dl_free','esp_log_write':'qpk_dl_log'}
    command=[str(tc/'riscv32-esp-elf-objcopy')]
    command += [f'--redefine-sym={a}={b}' for a,b in rename.items()]
    subprocess.run(command+[str(original),str(fbs)],check=True,stdout=out,stderr=subprocess.STDOUT)
    libraries=[root/'espdl-build/libqpk_espdl.a',fbs,tls_build/'library/libmbedtls.a',
               tls_build/'library/libmbedx509.a',tls_build/'tf-psa-crypto/core/libtfpsacrypto.a']
    for name in ['libstdc++.a','libsupc++.a','libgcc.a']:
        libraries.append(Path(subprocess.check_output([str(tc/'riscv32-esp-elf-g++'),*abi,
                                                        '-print-file-name='+name],text=True).strip()))
    print('Building ESP-DL firmware; log:',log,flush=True)
    result=subprocess.run(['make','-j8',f'CROSSDEV={tc}/riscv32-esp-elf-',
                           f'ESPCLAW_TLS_CFLAGS={includes}',
                           'EXTRA_LIBS='+' '.join(map(str,libraries)),*sys.argv[1:]],
                          cwd=root/'nuttx',env=env,stdout=out,stderr=subprocess.STDOUT)
print('\n'.join(log.read_text(errors='replace').splitlines()[-55:]))
if result.returncode: raise SystemExit(result.returncode)
jpeg_stamp.write_text(jpeg_flags)
if sys.argv[1:]: raise SystemExit(0)
for name,target in [('nuttx','nuttx.elf'),('nuttx.bin','nuttx.bin'),('nuttx.map','nuttx.map')]:
    shutil.copyfile(root/'nuttx'/name,delivery/target)
shutil.copyfile(root/'nuttx/.config', delivery/'resolved.config')
image=(delivery/'nuttx.bin').read_bytes()
symbols=subprocess.check_output([str(tc/'riscv32-esp-elf-nm'),'-C',str(delivery/'nuttx.elf')],text=True)
symbol_addresses={fields[2]:int(fields[0],16) for line in symbols.splitlines()
                  if len(fields:=line.split())==3 and re.fullmatch(r'[0-9a-fA-F]+',fields[0])}
idle_cpus=int(re.search(r'^CONFIG_SMP_NCPUS=(\d+)$',config,re.M).group(1))
idle_stack=int(re.search(r'^CONFIG_IDLETHREAD_STACKSIZE=(\d+)$',config,re.M).group(1))
startup_end=symbol_addresses['_ebss']+idle_cpus*((idle_stack+15)&~15)
assert startup_end<=0x4ffc0000,'P4 BSS/idle stacks exceed the internal SRAM protection boundary'
# Inspect the linked addresses, not source annotations: the vendor HAL relies
# on linker placement for these helpers. A flash-resident resume routine only
# works while its instruction happens to remain in L1 and fails intermittently.
cache_critical = ['esp_cache_suspend_ext_mem_cache', 'esp_cache_resume_ext_mem_cache',
                  'cache_hal_suspend', 'cache_hal_resume',
                  'nuttx_enter_critical', 'nuttx_exit_critical', 'nxsched_unlock',
                  'spi_flash_op_block_func',
                  'spi_flash_disable_interrupts_caches_and_other_cpu',
                  'spi_flash_enable_interrupts_caches_and_other_cpu']
cache_critical_addresses = {name: symbol_addresses[name] for name in cache_critical}
assert all(symbol_addresses['_iram_text_start'] <= addr < symbol_addresses['_iram_text_end']
           for addr in cache_critical_addresses.values()), cache_critical_addresses
required=['qpk_dl_backend_run','qpk_dl_track_update','qpk_dl_start','espdl_get_app_js','recorder_get_app_js','qpk_audio_list','riscv_fpuconfig',
          'riscv_savefpu','riscv_restorefpu','dl::Model::build',
          'dl_esp32p4_s8_conv2d_11cn','dl_esp32p4_s8_depthwise_conv2d',
          'riscv_initial_extctx_state', 'glass_weather_fetch', 'glass_weather_parse',
          'glass_weather_start', 'mbedtls_ssl_handshake', 'mbedtls_x509_crt_verify_restartable',
          'devurandom_register', 'music_pcm_open', 'music_search', 'glass_ime_attach',
          'glass_voice_start', 'glass_voice_stop', 'glass_voice_get', 'portal_voice_load',
          'glass_chat_start', 'glass_chat_request', 'glass_chat_store_save', 'glass_chat_tls_initialize',
          'glass_https_tls_ops', 'mbedtls_mutex_lock', 'claw_qpk_attach',
          'glass_qpk_guide', 'glass_qpk_read_example', 'glass_qpk_write_draft', 'glass_qpk_save_async',
          'qpk_hw_submit', 'qpk_hw_poll', 'qpk_hw_audio_execute', 'esp_i2cbus_initialize',
          'esp_spibus_initialize', 'esp_ledc_init', 'glass_ble_connect']
header=subprocess.check_output([str(tc/'riscv32-esp-elf-readelf'),'-h',str(delivery/'nuttx.elf')],text=True)
report={'bytes':len(image),'sha256':hashlib.sha256(image).hexdigest(),
        'flash_start':0x2000,'flash_end':len(image)+0x2000,'program_limit':0x800000,
        'models_start':0x800000,'models_limit':0xc00000,'data_start':0xc00000,'data_bytes':0x400000,
        'fits_program_partition':len(image)+0x2000<=0x800000,
        'symbols':{name:name in symbols for name in required},'single_float_abi':'single-float ABI' in header,
        'internal_startup_end':startup_end,'internal_startup_limit':0x4ffc0000,
        'cache_critical_iram':cache_critical_addresses,
        'tcp_connections':{'preallocated':8,'dynamic_batch':1,'maximum':32},
        'hardware_trace':hardware_trace,
        'hardware_tested':False}
(delivery/'evidence/firmware-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
assert all(report['symbols'].values()) and report['single_float_abi']
assert report['fits_program_partition'],'Firmware must not overlap models'
assert 'CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET=0xc00000' in config
assert 'CONFIG_ESPRESSIF_STORAGE_MTD_SIZE=0x400000' in config
