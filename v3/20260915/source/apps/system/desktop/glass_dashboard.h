/* In-memory HA snapshot bridge. No URL, token or entity payload is retained. */
#pragma once

/* Must only be called on the LVGL / QuickJS owner thread, never the HA worker.
 * Temperature is integral Celsius (-999 = missing); humidity is percent
 * (-1 = missing); PM2.5 is ug/m3 (-1 = missing). Counts describe HA entities,
 * not physical devices. After the HA page closes these are LAST SYNC values,
 * not background real-time readings. No persistence is performed here.
 */
void glass_dashboard_publish(int temperature, int humidity, int pm25,
                             int available, int total);
