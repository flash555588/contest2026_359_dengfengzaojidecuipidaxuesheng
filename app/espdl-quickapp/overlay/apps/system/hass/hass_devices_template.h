/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Fixed read-only registry query through HA's documented /api/template.
 * Caller supplies no template text. IDs are actual registry device IDs. */
static const char g_hass_devices_template[] =
  "{\"template\":\"{% set ns=namespace(ids=[],rows=[]) %}"
  "{% for s in states %}{% set d=device_id(s.entity_id) %}"
  "{% if d and d not in ns.ids %}{% set ns.ids=ns.ids+[d] %}"
  "{% set ns.rows=ns.rows+[[d,device_attr(d,'name_by_user') or "
  "device_attr(d,'name') or '',area_name(d) or '',device_entities(d),"
  "device_attr(d,'model') or '',device_attr(d,'manufacturer') or '']] %}"
  "{% endif %}{% endfor %}{{ ns.rows|tojson }}\"}";
