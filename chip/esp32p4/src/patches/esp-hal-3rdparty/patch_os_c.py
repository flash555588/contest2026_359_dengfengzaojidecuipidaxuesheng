#!/usr/bin/env python3
"""Patch HAL nuttx/src/platform/os.c for openvela/dev APIs."""

from pathlib import Path
import sys

p = Path(sys.argv[1])
text = p.read_text()

if "#include <fcntl.h>" not in text:
    text = text.replace(
        "#include <unistd.h>\n",
        "#include <unistd.h>\n#include <fcntl.h>\n#include <nuttx/kthread.h>\n",
    )

struct = (
    "\nstruct intr_adapter_to_nuttx\n"
    "{\n"
    "  esp_os_intr_handler_t handler;\n"
    "  FAR void *arg;\n"
    "};\n"
)

if "struct intr_adapter_to_nuttx\n{" not in text:
    needle = "typedef struct non_shared_isr_arg_t non_shared_isr_arg_t;\n"
    if needle not in text:
        raise SystemExit("could not insert struct")
    text = text.replace(needle, needle + struct, 1)

start = text.find("  tcb = kmm_zalloc(sizeof(struct tcb_s));")
if start < 0:
    raise SystemExit("could not find tcb alloc")
end = text.find("  nxtask_activate(tcb);\n  return 0;", start)
if end < 0:
    raise SystemExit("could not find nxtask_activate")
end = text.find("\n", end + len("  nxtask_activate(tcb);\n  return 0;")) + 1

new_init = (
    "  ret = kthread_create(name, priority, stack_size,"
    " task_wrapper_entry, argv);\n"
    "  if (ret < 0)\n"
    "    {\n"
    "      kmm_free(wrapper_args);\n"
    "      return -1;\n"
    "    }\n"
    "\n"
    "  if (task_notify_register(ret) == NULL)\n"
    "    {\n"
    "      _warn(\"esp_os_create_task: out of memory for notify"
    " entry '%s'\\n\",\n"
    "            name);\n"
    "    }\n"
    "\n"
    "  if (task_handle != NULL)\n"
    "    {\n"
    "      *task_handle = (esp_os_task_handle_t)ret;\n"
    "    }\n"
    "\n"
    "  return 0;\n"
)

text = text[:start] + new_init + text[end:]
p.write_text(text)
print("patched", p)
