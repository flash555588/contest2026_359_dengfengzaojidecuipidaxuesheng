#!/usr/bin/env python3
"""Align Apache esp-hal-3rdparty nuttx/src/platform/os.c with openvela ABI.

openvela nxtask_init takes 7 args (entry + spawn attr), not Apache's 9-arg
(priority, stack, entry). Queue helpers also need POSIX fcntl flags.
"""
from pathlib import Path

OS_C = Path(
    "/home/flash/vela-p4/nuttx/arch/risc-v/src/chip/"
    "esp-hal-3rdparty/nuttx/src/platform/os.c"
).resolve()

OLD_INIT = """  ret = nxtask_init(tcb, name, priority,
                    NULL, stack_size,
                    task_wrapper_entry, argv, NULL, NULL);"""

NEW_INIT = """  memset(&attr, 0, sizeof(attr));
  attr.priority  = priority;
  attr.stacksize = stack_size;

  ret = nxtask_init(tcb, name, task_wrapper_entry, NULL, &attr, argv, NULL);"""

OLD_LOCALS = """  FAR struct tcb_s *tcb;
  FAR struct task_wrapper_args_s *wrapper_args;
  FAR char *argv[2];
  char ptr_buf[32];
  int ret;"""

NEW_LOCALS = """  FAR struct tcb_s *tcb;
  FAR struct task_wrapper_args_s *wrapper_args;
  FAR char *argv[2];
  char ptr_buf[32];
  posix_spawnattr_t attr;
  int ret;"""


def ensure_include(text: str, header: str) -> str:
    needle = f"#include <{header}>"
    if needle in text:
        return text
    anchor = "#include <nuttx/sched.h>\n"
    if anchor not in text:
        raise SystemExit(f"missing include anchor for {header}")
    return text.replace(anchor, anchor + needle + "\n", 1)


def main() -> None:
    text = OS_C.read_text(encoding="utf-8")
    orig = text
    text = ensure_include(text, "fcntl.h")
    text = ensure_include(text, "spawn.h")
    if "#include <string.h>" not in text:
        text = ensure_include(text, "string.h")

    if OLD_LOCALS in text:
        text = text.replace(OLD_LOCALS, NEW_LOCALS, 1)
    elif "posix_spawnattr_t attr;" not in text:
        raise SystemExit("could not insert posix_spawnattr_t local")

    if OLD_INIT in text:
        text = text.replace(OLD_INIT, NEW_INIT, 1)
    elif "attr.stacksize = stack_size;" in text and "nxtask_init(tcb, name, task_wrapper_entry" in text:
        print("nxtask_init already converted")
    else:
        raise SystemExit("could not rewrite nxtask_init call")

    if text != orig:
        OS_C.write_text(text, encoding="utf-8")
        print("patched", OS_C)
    else:
        print("already patched", OS_C)


if __name__ == "__main__":
    main()
