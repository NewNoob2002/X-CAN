/* SPDX-License-Identifier: Apache-2.0 */
/* ES0431 Rev9 2.2.7, method 2. No stack or SRAM writes before these reads.
 * The measured board has SRAM_PE=1 (parity disabled). Do not change OPTR.
 * Fail closed on another option-byte setup: parity-enabled startup needs
 * the separate method specified by ST, not blind reads of uninitialized RAM.
 */
.syntax unified
.thumb
.section .text.soc_early_reset_hook,"ax",%progbits
.global soc_early_reset_hook
.type soc_early_reset_hook,%function
.thumb_func
soc_early_reset_hook:
    ldr r0, =0x40022020
    ldr r1, [r0]
    tst r1, #0x01000000
    beq xcan_sram_parity_unsupported
    ldr r0, =0x20000000
    ldr r1, [r0]
    ldr r0, =0x20004000
    ldr r1, [r0]
    ldr r0, =0x10000000
    ldr r1, [r0]
    bx lr
.global xcan_sram_parity_unsupported
.type xcan_sram_parity_unsupported,%function
.thumb_func
xcan_sram_parity_unsupported:
    b xcan_sram_parity_unsupported
.size soc_early_reset_hook, .-soc_early_reset_hook
