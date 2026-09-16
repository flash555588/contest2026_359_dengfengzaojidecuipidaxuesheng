from pathlib import Path
ws=Path(__file__).resolve().parent.parent
rel=Path('nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src/p4_microphone.c')
target=ws/'04-v3-20260913/espdl-quickapp/overlay'/rel
source=Path('/tmp/v3-desktop-espdl-20260915')/rel
if not target.exists():
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(source.read_bytes())
text=target.read_text()
if 'board_music_amplifier' not in text:
    text+='''
/* ES32-P4 Function EV Board, matching Espressif BSP_POWER_AMP_IO.
 * Called by the owner of the shared ES8311 playback reservation only. */
#include "espressif/esp_gpio.h"
void board_music_amplifier(bool enabled)
{
  esp_gpiowrite(53, enabled);
  esp_configgpio(53, OUTPUT);
}
'''
    target.write_text(text)
