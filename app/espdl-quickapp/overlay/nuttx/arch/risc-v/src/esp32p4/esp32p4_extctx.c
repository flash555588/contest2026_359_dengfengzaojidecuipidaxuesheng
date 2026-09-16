/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/sched.h>
#include <arch/irq.h>
#include <string.h>
void riscv_initial_extctx_state(struct tcb_s *tcb)
{
  /* New tasks start with both coprocessors disabled. The ESP-DL worker enables
   * them explicitly after it starts. The initialstate path already zeroes regs. */
  memset(tcb->xcp.regs+REG_INT_CTX_NDX+1,0,CONFIG_ARCH_RISCV_INTXCPT_EXTREGS*4);
}
