/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <string.h>
#define QPK_CLI_PACKAGE "ai.claude_control"
#define QPK_CLI_ORIGIN "builtin:/espclaw/cli.js"
static inline bool qpk_builtin_cli_origin(const char *package,const char *filename)
{
  return package&&filename&&!strcmp(package,QPK_CLI_PACKAGE)&&!strcmp(filename,QPK_CLI_ORIGIN);
}
