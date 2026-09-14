# ES8311 Microphone Linked Candidate

Target: ESP32-P4 Function EV Board v3. Not flashed or recording-tested.
ES8311 input registration is now called from board bringup instead of generic
I2S device registration when ESP32P4_BOARD_MICROPHONE is enabled.

I2C0 uses SDA7/SCL8; I2S0 uses BCLK12, WS10, DIN11, DOUT9 and MCLK13.
Initial I2S configuration: 16-bit, 16000 Hz, master, RX and TX enabled.
The ES8311 driver configures both directions even for input. Codec address is
0x18 at 400 kHz; actual board ACK and recording still require verification.
Registration probes the reset register before codec initialization, retains
one lower half on registration failure and rejects a changed bus binding.

Compatibility adaptation adds missing mutex/semaphore/workqueue includes and
six unsigned I2S ABI wrappers preserving existing return bits. It does not
change the public kernel interface or claim to fix underlying DMA behavior.
The existing ES8311 reset path does not propagate every I2C write error.

Full RISC-V build and image generation passed. ELF symbols checked:
board_microphone_initialize, board_microphone_register, es8311_initialize.
Registration fixture tests previously passed probe failure, retry, duplicate
registration and ownership mismatch. No real samples have been captured.

BIN: 3940940 bytes, SHA-256:
bd9300f067134e1deabd4cbd57785a1428398edc4216409209196dad265338f8.
At offset 0x2000, 245172 bytes remain before /data at 0x400000.
Resolved configuration and build log are included.
Reproduction adapters: espclaw-port/tools/integrate_microphone.py and
espclaw-port/tools/adapt_audio_compat.py. AUDIO_I2S must also be enabled because
the existing common board build compiles its generic registration unit.

Remaining: recording API and PCM/WAV capture, buffer stop/drain testing,
actual codec clocks/gain/channel validation, voice transcription and ESP-Claw
voice request integration. This is not a working voice-input acceptance image.
