"""Create a new cumulative release with active BLE scans and device names."""
from pathlib import Path
import difflib
import hashlib
import shutil

root = Path(__file__).resolve().parent.parent
base = root / '04-v3-20260913/ble-startup-fix'
delivery = root / '04-v3-20260913/ble-device-names'
assert hashlib.sha256((base / 'nuttx.bin').read_bytes()).hexdigest() == '314fc2168eb0873082018edc08a6e1f3fee5d58f18662f4458642273cccd14ee'
shutil.copytree(base / 'overlay', delivery / 'overlay', dirs_exist_ok=True)
(delivery / 'evidence').mkdir(exist_ok=True)
shutil.copyfile(base / 'resolved.config', delivery / 'resolved.config')
patch = []

def write(relative, before, after):
    path = delivery / 'overlay' / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(after, encoding='utf-8', newline='\n')
    patch.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                    'a/' + relative if before else '/dev/null', 'b/' + relative))

header = (root / 'diagnostics/ui-baseline/glass_ble.h').read_text(encoding='utf-8')
new_header = header.replace('#define GLASS_BLE_LIMIT 12', '#define GLASS_BLE_LIMIT 32\n#define GLASS_BLE_NAME_MAX 63')
new_header = new_header.replace('  int rssi;', '  int rssi;\n  bool name_complete;\n  char name[GLASS_BLE_NAME_MAX + 1];')
for component in ['apps/system/desktop', 'apps/wireless/bluetooth/nimble']:
    write(component + '/glass_ble.h', header, new_header)
relative = 'apps/wireless/bluetooth/nimble/glass_ble.c'
before = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
after = before.replace('#include "glass_ble.h"', '#include "glass_ble.h"\n#include "glass_ble_name.h"', 1)
after = after.replace('static int gap_event(', '''/* Sort once discovery ends, keeping list order stable during the scan.
 * Named devices come first; within each group show stronger signals first.
 */
static void sort_devices(void)
{
  for (unsigned i = 1; i < g_state.count; i++)
    {
      struct glass_ble_device device = g_state.devices[i];
      unsigned j = i;
      while (j > 0)
        {
          const struct glass_ble_device *previous = &g_state.devices[j - 1];
          bool named = device.name[0] != 0;
          bool previous_named = previous->name[0] != 0;
          if (!(named && !previous_named) &&
              !(named == previous_named && device.rssi > previous->rssi)) break;
          g_state.devices[j] = *previous;
          j--;
        }
      g_state.devices[j] = device;
    }
}

static int gap_event(''', 1)
after = after.replace('      case BLE_GAP_EVENT_DISC_COMPLETE:\n',
                      '      case BLE_GAP_EVENT_DISC_COMPLETE:\n        sort_devices();\n', 1)
after = after.replace('                g_state.devices[i].rssi = event->disc.rssi;',
                      '                g_state.devices[i].rssi = event->disc.rssi;\n'
                      '                glass_ble_update_name(&g_state.devices[i],\n'
                      '                                      event->disc.data, event->disc.length_data);')
after = after.replace('params.passive = 1;', 'params.passive = 0; /* Request names carried in scan responses. */')
after = after.replace('if (command == 1) { g_state.count = 0; g_state.scanning = true; }',
                      '''if (command == 1)
        {
          memset(g_state.devices, 0, sizeof(g_state.devices));
          g_state.count = 0;
          g_state.scanning = true;
        }''')
assert after != before and 'glass_ble_update_name' in after
write(relative, before, after)
write('apps/wireless/bluetooth/nimble/glass_ble_name.h', '',
      (root / 'diagnostics/ble-name-source/glass_ble_name.h').read_text(encoding='utf-8'))
relative = 'apps/system/desktop/glass_ble.inc'
before = (base / 'overlay' / relative).read_text(encoding='utf-8')
start = before.index('      lv_obj_clean(g_ble_list);')
end = before.index('\n    }\n  for (unsigned i', start)
after = before[:start] + '''      /* Preserve row objects and scroll position while RSSI/names update. */
      if (lv_obj_get_child_count(g_ble_list) > state.count)
        lv_obj_clean(g_ble_list);
      for (unsigned i = 0; i < state.count; i++)
        {
          const struct glass_ble_device *device = &state.devices[i];
          const uint8_t *a = device->address;
          const char *name = device->name[0] ? device->name : "未命名设备";
          lv_obj_t *row = lv_obj_get_child(g_ble_list, i);
          if (!row)
            {
              row = glass_button(g_ble_list, name, 4, i * 72, 816,
                                 theme_surface(), glass_ble_pick,
                                 (void *)(uintptr_t)i);
              lv_obj_set_height(row, 68);
              lv_obj_t *title = lv_obj_get_child(row, 0);
              lv_obj_set_style_text_font(title, zh_font(20), 0);
              lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
              lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 5);
              lv_obj_set_size(title, 788, 28);
              lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
              glass_text(row, "", 12, 38, 788, 16, theme_secondary());
            }
          lv_label_set_text(lv_obj_get_child(row, 0), name);
          snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X  %d dBm",
                   a[5], a[4], a[3], a[2], a[1], a[0], device->rssi);
          lv_label_set_text(lv_obj_get_child(row, 1), text);
        }''' + before[end:]
write(relative, before, after)
(delivery / 'bluetooth-names.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
print('Prepared active scanning, merged names and stable two-line rows in', delivery.name)
