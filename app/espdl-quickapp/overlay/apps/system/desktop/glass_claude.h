/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Nonblocking, one-flight LAN bridge. Result ownership transfers to poll caller. */
int glass_claude_request(const char *base,const char *token,const char *action,const char *json);
char *glass_claude_poll(unsigned id);
void glass_claude_abandon(void);
