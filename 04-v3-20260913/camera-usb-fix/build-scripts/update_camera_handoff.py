"""Record the current camera state without stale unplug requirements."""
from pathlib import Path
import hashlib

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/camera-usb-fix'
digest = hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest()
(ws / 'diagnostics/CAMERA-RESUME.md').write_text(f'''# Camera status, 2026-09-15

Current board/delivery: revision 11, SHA256 `{digest}`, 3,965,660 bytes at 0x2000, erase end 0x3cb000. COM23, 303A:1001, E8:F6:0A:E3:A9:5F. /data preserved. Board is running USB preview, no active serial or OpenOCD client after toolbar-camera-11 suite completed.

User's latest issue: revision 10 made the three bottom controls acceptable, but the bottom fifth of the VIDEO still had flickering black lines. Revision 11 fixes a confirmed root cause: CONFIG_ARCH_DCACHE is absent, so up_clean_dcache() is a no-op even though ESP32-P4 has 256KB PSRAM L2 cache. The USB worker now calls esp_mipi_dsi_flush_framebuffer() in 16KB chunks and aborts on error before presenting. Actual ELF call verified in cache-writeback-validation.json. Prior chunking of the generic macro did nothing. A cached CPU screenshot can look correct while DMA reads stale physical memory; do not use the revision 10 screenshot as proof flicker is fixed.

An asynchronous question asks whether the video black lines are gone on revision 11. No reply recorded at handoff generation. This is a request for visual observation, not permission to continue. Do not require physical unplug. User already confirmed the earlier USB preview worked and the revision 10 toolbar improved.

Validation revision 11: build, image checks, all 88 overlay files and actual binary/ELF/map vs isolated tree passed. Incremental build, not full clean build. toolbar-camera-11.json passed preview, photo=2 (/data/photos/camera-00000002.jpg, 18,220 bytes), stop/idle/reopen; display underruns=0, pan_errors=0, generation=1 throughout. Board left previewing. The previous revision 10 JPEG camera-00000001.jpg was independently read and decoded at 640x480 (29,111 bytes). Parser/decoder sanitizers and reserved toolbar row test passed on revision 10, and these parts are unchanged in revision 11.

Source: 04-v3-20260913/camera-usb-fix/overlay. Worker qpk_camera_usb.inc; generator diagnostics/integrate_camera_app.py reproduces qpk_runtime.c and resources. It preserves the new BAR_TOP=500 and bar_blit return value. No sub-agents authorized. Do not rerun prepare_camera_work.py, integrate_camera_host.py or harden_camera_host.py; they overwrite later fixes. WSL original /home/streetartist/nuttxspace remains untouched; build uses /tmp/v3-desktop-camera-usb-20260914 with approved wsl -d Ubuntu --exec prefix and require_escalated. Never run simultaneous serial clients or OpenOCD instances. No old hotspot reconnect or /data formatting.

History: revision 03 streamed but crashed on stop. SMP synchronization fixes followed. Revision 09 configured proper P4 UTMI16/TOCAL5 but first start still timed out; later generation changed 1 to 2 (trigger unrecorded) and three actual start/stop cycles passed. User confirmed video worked. Revision 10 isolated toolbar and checked actual page flip but still called an empty cache macro. Revision 11 corrects the actual cache call. The old IRQ crash addresses must not be symbolized against newer ELFs.

Current camera: HD Web Camera 05a3:9331, CSI0/USB1; UVC1.0 Probe26, six MJPEG modes 640x360 through 1920x1080, interval333333. Keep GET_LEN workaround. Verified stream is 640x480; displayed rate around6fps, device capability30fps. Supported transport: HS iso IN bInterval1, 1-3 transactions; limited bulk payload multiple of MPS, 1 hub/four USB slots/one active preview. Full-speed iso, other intervals, YUYV/H264 and camera audio not supported.

Remaining original scope: actual high-resolution switching, hot unplug/multiple USB devices/CSI hardware, photo cancellation and long run. The inherited DWC2 halt still has bounded polling with no explicit failure result; no halt failure or panic seen in later cycles, but review before asserting cancellation under every hardware fault. CSI path still contains generic cache calls inherited from the baseline; do not assume CSI validated while disconnected. No claim of BLE pairing/audio or new Wi-Fi regression from this camera work.

Package README/CURRENT-TASKS updated to revision11 with visual result pending; run package_camera_usb.py then verify_camera_package.py after evidence or documentation changes. Keep historical failures attributed to their revisions.
''', encoding='utf-8')
print('Updated camera handoff', digest)
