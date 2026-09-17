"""Repair only the known misplaced include block from the first integration."""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'apps/system/desktop/Makefile'
original = path.read_text()
block = '''ifeq ($(CONFIG_SYSTEM_C6_DESKTOP),y)
CFLAGS += -I$(APPDIR)/system/c6probe
endif
'''
anchor = 'include $(APPDIR)/Make.defs'
if not original.startswith(block) or original.count(block) != 1 or original.count(anchor) != 1:
    raise SystemExit('Unexpected Makefile; not modified')
path.write_text(original[len(block):].replace(anchor, anchor + '\n' + block, 1))
