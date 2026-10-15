/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Fixed, read-only HA query, independent of device/favorite limits.
 * Bound both the number of records and their strings before serialization. */
static const char g_hass_scenes_template[] =
  "{\"template\":\"{% set ns=namespace(rows=[]) %}"
  "{% set all=(states.scene|list)+(states.script|list) %}"
  "{% for s in all[:128] %}"
  "{% set ns.rows=ns.rows+[{'entity_id':s.entity_id,'state':s.state[:128],"
  "'attributes':{'friendly_name':s.name[:64],"
  "'last_triggered':((s.attributes.get('last_triggered') or '')|string)[:128]}}] %}"
  "{% endfor %}{{ {'items':ns.rows,'total':all|count}|tojson }}\"}";
