/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Registers the in-process smart home MCP tools on the board server.
 * Compiled only with CONFIG_SYSTEM_HASS; called once at desktop startup. */
void glass_board_home_register(void);
