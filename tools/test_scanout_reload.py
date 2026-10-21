#!/usr/bin/env python3
"""Test production reload control/ISR with actual P4 register layouts."""
import argparse,shutil,subprocess
from pathlib import Path
from test_display_flash import function

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--nuttx',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[1]
common=repo/'app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/common/espressif'
sdk=a.nuttx/'arch/risc-v/src/esp32p4/esp-hal-3rdparty/components'
shutil.copyfile(sdk/'soc/esp32p4/register/hw_ver3/soc/dw_gdma_struct.h',out/'actual-registers.h')
ll=(sdk/'esp_hal_dma/esp32p4/include/hal/dw_gdma_ll.h').read_text()
names=['channel_enable_intr_generation','channel_clear_intr','channel_set_src_addr','channel_set_dst_addr',
 'channel_set_block_markers','channel_enable','channel_set_src_multi_block_type','channel_set_dst_multi_block_type',
 'lli_set_src_addr','lli_set_src_master_port','lli_set_block_markers','channel_set_link_list_master_port','channel_set_link_list_head_addr']
(out/'actual-ll.inc').write_text('\n'.join(function(ll,'dw_gdma_ll_'+name,'static inline void ') for name in names)+'\n'+function(ll,'dw_gdma_ll_channel_get_intr_status','static inline uint32_t ')+'\n')
source=(common/'esp_mipi_dsi.c').read_text()
(out/'actual-isr.inc').write_text(function(source,'esp_mipi_dsi_dma_isr')+'\n')
(out/'actual-restart.inc').write_text(function(source,'esp_mipi_dsi_dma_restart','static void IRAM_ATTR ')+'\n')
(out/'actual-pan.inc').write_text(function(source,'esp_mipi_dsi_swap_framebuffer_ungated','static int ')+'\n'+function(source,'esp_mipi_dsi_swap_framebuffer','int ')+'\n')
(out/'nuttx').mkdir(exist_ok=True)
(out/'nuttx/signal.h').write_text('int nxsig_usleep(unsigned int us);\n')
exe=out/'reload'
subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-O1','-g','-pthread',
 '-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+str(out),'-I'+str(common),
 str(repo/'app/espdl-quickapp/tests/scanout_reload_host.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
