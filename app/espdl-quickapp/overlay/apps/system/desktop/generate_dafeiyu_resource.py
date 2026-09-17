#!/usr/bin/env python3
"""Generate the built-in Dafeiyu Quick App C resource."""

from pathlib import Path


ROOT = Path(__file__).resolve().parent
ENGINE = ROOT / "dafeiyu" / "engine.js"
SOURCE = ROOT / "dafeiyu" / "app.js"
OUTPUT = ROOT / "dafeiyu_resource.c"


def format_bytes(data: bytes) -> str:
    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    data = ENGINE.read_bytes() + b"\n" + SOURCE.read_bytes()
    standalone = ROOT / "dafeiyu" / "qpk"
    standalone.mkdir(exist_ok=True)
    (standalone / "app.js").write_bytes(data)
    (standalone / "manifest.json").write_bytes((ROOT / "dafeiyu" / "manifest.json").read_bytes())
    output = f"""/* Auto-generated from dafeiyu/engine.js + app.js. */

#include <stddef.h>

static const unsigned char g_dafeiyu_app_js[] =
{{
{format_bytes(data + bytes([0]))}
}};

const char *dafeiyu_get_app_js(unsigned int *len)
{{
  if (len != NULL)
    {{
      *len = sizeof(g_dafeiyu_app_js) - 1;
    }}

  return (const char *)g_dafeiyu_app_js;
}}
"""
    OUTPUT.write_text(output, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
