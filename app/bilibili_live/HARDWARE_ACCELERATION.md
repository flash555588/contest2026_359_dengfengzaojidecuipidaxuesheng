# ESP32-P4 / C6 playback acceleration assessment

Checked 2026-10-13 against the current local v3 source tree and Espressif
documentation. PPA has now been integrated and measured on the board.
Selected image: `8dfe65ddef454c28383edcee33090570e1c5494628592efa962a7206fdea84eb`.
Evidence: local `output/media-v3-ppa-irq/RESULTS.txt` and `COMPARISON.json`.
JPEG/C6 offload below remains a feasibility assessment.

## PPA: preserve H264 and accelerate the display path

PPA SRM supports scaling and color conversion, including YUV420 and RGB565.
The current `local_video_ui.c:frame()` and live fit-mode callbacks attempt
PPA first and fall back to software on error. The decoder remains OpenH264
and the existing application UI is preserved.
It cannot replace H264 entropy decoding, motion compensation, or reference
picture reconstruction. Any reduction must be reported as display processing
time, separately from the existing decoder timing.

The original tree had HAL files but no integrated NuttX PPA driver. The new
`esp32p4_ppa.c` uses the low-level register/descriptor helpers rather than
ESP-IDF's FreeRTOS queues. Board bringup registers `/dev/ppa0`; Kconfig,
Make and CMake entries were added (only the Make build was executed).

The board-specific synchronous prototype serializes jobs, uses aligned
buffers/descriptors, maintains caches, and waits for DMA RX IRQ completion
with a 250ms timeout/reset before returning buffers to the caller.
It owns DMA2D exclusively and is not a general shared-engine implementation.
It leaves peripheral clocks enabled after first use; idle power management
has not been implemented. Do not combine it with another DMA2D driver.

OpenH264's padded I420 planes are packed into UYVY with 32-bit stores.
PPA handles color conversion and representable scaling; fits outside the
local HAL's 1/16 coefficient steps are mapped during packing to preserve
the viewport. All packing/cache/wait costs are included in render timing.
Two rounds of grey/colored stripe and padded planar tests passed for
360p/480p/720p dimensions, including output guards and invalid arguments.
Interpolation boundaries were not tested for bit equality to nearest-neighbor
software scaling. Two full 360p H264/AAC local plays submitted all80 frames.

Same-firmware local480p software/PPA/PPA/software runs reduced mean render
time from1795 to1370ms for60 samples (23.7%). Decode elapsed rose from5355
to5510ms, and summed measured stages fell from7265 to6900ms (5.0%).
Submitted frames stayed31.5 per60 samples in both modes. No stable480p10fps
playback or comparative live FPS improvement is claimed.

Source: [PPA programming guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html).

## JPEG: another local-file playback route

P4 has a JPEG hardware decoder. A local MP4 containing MJPEG video and AAC
audio can decode JPEG frames on the board and then display RGB565, using
PPA where scaling is needed. The application already has an MJPEG MP4 path,
but `qpk_mjpeg.c` currently calls the software `qpk_jd_*` decoder. Hardware
JPEG support is therefore not active in the current selected image.

The tree contains `esp_hal_jpeg` and `upper_hal_jpeg`; driver integration,
DMA alignment, padded output dimensions and JPEG format coverage remain
to be tested. Current MJPEG UI limits are 640x360; 480p requires reviewing
the buffers and scaling path, not merely lifting the resolution check.
Hardware JPEG performance tables from ESP-IDF are isolated codec tests,
not measurements of this NuttX player. They cannot promise full-player FPS.

Existing MJPEG files need no runtime computer processing. A native H264
stream cannot be fed to the JPEG engine. Converting H264 to MJPEG on P4
would still require H264 software decoding first, so it does not remove
the current live decoder bottleneck.

Source: [JPEG programming guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/jpeg.html).

## H264 hardware block

P4's H264 hardware block is an encoder. Espressif's codec documentation
distinguishes the P4 hardware encoder from its software H264 decoder.
It cannot be substituted for the OpenH264 decoder used here.

Sources: [P4 datasheet](https://documentation.espressif.com/esp32-p4_datasheet_en.pdf),
[ESP H264 component](https://docs.espressif.com/projects/esp-adf/en/latest/basic-components/video-codec/esp-h264.html).

## C6: keep wireless work off the P4

C6 is already used as the ESP-Hosted WiFi/BLE coprocessor. Its HP CPU is
160MHz with 512KiB HP SRAM. A visible 848x480 I420 picture alone needs
610560 bytes (596.25KiB), before reference frames and decoder/WiFi memory.
This makes it a poor candidate for a separate 480p H264 decoder.
This conclusion is a memory/architecture assessment, not a C6 codec test.

Small task offload, such as AAC or transport processing, is conceivable,
but would require a new C6 protocol/firmware and SDIO traffic measurements.
The current board has no evidence that this would outperform the tested
P4 CPU split. Prioritize PPA for native H264 display and JPEG for prepared
local MJPEG files before changing the working wireless firmware.

Source: [C6 datasheet](https://documentation.espressif.com/esp32-c6_datasheet_en.html).
