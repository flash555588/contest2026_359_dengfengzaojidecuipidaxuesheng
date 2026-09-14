#!/usr/bin/env python3
"""Generate the built-in Home Assistant Quick App C resource."""

from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parents[1] / "quickapp" / "homeassistant" / "app.js"
OUTPUT = ROOT / "homeassistant_resource.c"


def main() -> None:
    data = SOURCE.read_bytes()
    tokens = []
    for value in data:
        if value == 10:
            tokens.append(r"\n")
        elif value == 13:
            tokens.append(r"\r")
        elif value == 9:
            tokens.append(r"\t")
        elif value in (34, 92):
            tokens.append("\\" + chr(value))
        elif 32 <= value <= 126:
            tokens.append(chr(value))
        else:
            tokens.append(f"\\{value:03o}")

    rows = []
    row = ""
    for token in tokens:
        if row and len(row) + len(token) > 76:
            rows.append(f'  "{row}"')
            row = ""
        row += token
    rows.append(f'  "{row}"')
    OUTPUT.write_text(
        "/* Auto-generated from homeassistant/app.js. */\n\n#include <stddef.h>\n\n"
        "static const char g_homeassistant_app_js[] =\n"
        + "\n".join(rows)
        + ";\n\nconst char *homeassistant_get_app_js(unsigned int *len)\n{\n"
          "  if (len != NULL)\n    {\n      *len = sizeof(g_homeassistant_app_js) - 1;\n    }\n\n"
          "  return (const char *)g_homeassistant_app_js;\n}\n",
        encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
