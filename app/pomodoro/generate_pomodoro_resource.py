#!/usr/bin/env python3
"""Generate the built-in Pomodoro Quick App C resource."""

from pathlib import Path


ROOT = Path(__file__).resolve().parent
QUICKAPP = ROOT.parents[1] / "quickapp" / "pomodoro"
ENGINE = QUICKAPP / "engine.js"
FACE = QUICKAPP / "face.js"
SOURCE = QUICKAPP / "app.js"
OUTPUT = ROOT / "pomodoro_resource.c"


def format_bytes(data: bytes) -> str:
    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    data = ENGINE.read_bytes() + b"\n" + FACE.read_bytes() + b"\n" + SOURCE.read_bytes()
    output = f"""/* Auto-generated from pomodoro/engine.js + face.js + app.js. */

#include <stddef.h>

static const unsigned char g_pomodoro_app_js[] =
{{
{format_bytes(data)}
}};

const char *pomodoro_get_app_js(unsigned int *len)
{{
  if (len != NULL)
    {{
      *len = sizeof(g_pomodoro_app_js);
    }}

  return (const char *)g_pomodoro_app_js;
}}
"""
    OUTPUT.write_text(output, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
