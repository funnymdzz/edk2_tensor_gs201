#include <Library/PcdLib.h>
#include <AsmMacroLib.h>
#include <AArch64/AArch64.h>

.text
.align 3

.set CTRL_SPAN_BIT, (1 << 23)

GCC_ASM_IMPORT (CEntryPoint)
GCC_ASM_IMPORT (SecondaryCEntryPoint)

GCC_ASM_IMPORT (ArmEnableInstructionCache)
GCC_ASM_IMPORT (ArmInvalidateTlb)

GCC_ASM_EXPORT (_ModuleEntryPoint)
GCC_ASM_EXPORT (SecondaryCpuEntry)

.global _StackBase
.global _StackSize

_StackBase:
  .quad FixedPcdGet32(PcdPrePiStackBase)

_StackSize:
  .quad FixedPcdGet32(PcdPrePiStackSize)

_ModuleEntryPoint:
  /* ------------------------------------------------------------------
   * BRING-UP PROBE + AUTO-RESET
   *
   * Two problems this solves:
   *  1. Reading the probe requires a WARM reset. A long-press power reboot
   *     is PO_RESET, which retrains DRAM and wipes ramoops. So we arm the
   *     cluster watchdog ourselves: if EDK2 hangs, the SoC resets itself
   *     after ~11s and the ramoops content survives.
   *  2. The probe must live in the pmsg half of ramoops (0xfd5ff000) so
   *     Android exposes it as /sys/fs/pstore/pmsg-ramoops-0.
   * ------------------------------------------------------------------ */

  /* probe: "GBED" | stage | 0 | 0 | "!END" */
  movz x11, #0xFD5F, lsl #16
  movk x11, #0xF000
  movz w12, #0x4247
  movk w12, #0x4445              /* 0x44454247 "GBED" */
  str  w12, [x11]
  movz w12, #0xE0                /* stage 0xE0 = assembly entry reached */
  str  w12, [x11, #4]
  str  wzr, [x11, #8]
  str  wzr, [x11, #12]
  movz w12, #0x4521
  movk w12, #0x444E              /* 0x444E4521 "!END" */
  str  w12, [x11, #16]
  /* Clear the ring slot counter so record sequence numbers start at 0 every
     boot. Without this the counter is whatever the previous boot left behind
     and the records cannot be ordered. */
  str  wzr, [x11, #0x440]        /* PM_CTR = base + 64 + 32*32 */
  dsb  sy

  /* Arm watchdog_cl0 with the LONGEST possible timeout.
   *
   * timeout = WTCNT / (clk / (prescaler + 1) / divisor)
   * With the previously used WTCON=0x2021 (prescaler 32, DIV16) that works out
   * to only 0.35-1.4 s depending on the watchdog clock - far too short for a
   * UEFI boot, so the board reset itself about a second in and every bring-up
   * attempt looked like a random hang at a different place.
   *
   * WTCON = prescaler 0xFF (bits 8..15) | DIV128 (bits 3..4) | enable (bit 5)
   *         | reset-enable (bit 0) = 0xFF39
   * giving 21 s at 100 MHz up to 87 s at 24.576 MHz. The bring-up probes write
   * WTCNT to keep petting it, so only a genuine hang ever reaches the timeout.
   */
  movz x13, #0x1006, lsl #16
  movz w14, #0xFFFF
  str  w14, [x13, #4]            /* WTDAT */
  str  w14, [x13, #8]            /* WTCNT */
  movz w14, #0xFF39
  str  w14, [x13]                /* WTCON */
  dsb  sy

  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  str x0, [x10] // oh my fdt

  /* stage 0xE1: FDT pointer stored */
  movz w12, #0xE1
  str  w12, [x11, #4]
  dsb  sy

  mov x0, #0
  mov x1, #0

  /* First ensure all interrupts are disabled */
  bl ASM_PFX(ArmDisableInterrupts)

  /* Ensure that the MMU and caches are off */
  bl ASM_PFX(ArmDisableCachesAndMmu)

  /* Initialize the aligned ring again with caches off. Clear stale slots so a
     short new run cannot be confused with records retained from an older run. */
  movz x11, #0xFD5F, lsl #16
  movk x11, #0xF000
  add x12, x11, #64
  add x13, x12, #1024
1:
  stp xzr, xzr, [x12], #16
  cmp x12, x13
  b.lo 1b
  str wzr, [x11, #0x440]
  dsb sy

  /* Invalide I-Cache */
  bl ASM_PFX(ArmInvalidateInstructionCache)

  /* Invalidate TLB */
  bl ASM_PFX(ArmInvalidateTlb)

  /*mrs x0, sctlr_el1
  orr x0, x0, #CTRL_SPAN_BIT
  msr sctlr_el1, x0*/

_SetupExceptionVector:
  ldr x0, =FixedPcdGet64(PcdCpuVectorBaseAddress)
  bl ArmWriteVBar

  mrs     x4, mpidr_el1
  and     x4, x4, #0xFFFF
  lsr     x4, x4, #8
  cbnz    x4, SecondaryCpuEntry

  ldr x1, dead
  add x2, x0, #0x800
  mov x3, x0

_FillVectors:
  stp     x1, x1, [x3], #16 /* Fill every 16 byte */
  cmp     x3, x2
  b.lt    _FillVectors

_DonNotTrap_VFP_SIMD:
  mrs x0, CPACR_EL1
  /* Bit 20 and 21 */
  orr x0, x0, #0x300000
  msr CPACR_EL1, x0
  isb sy

_SetupPrimaryCoreStack:
  ldr x0, _StackBase     /* Stack base arg0 */
  ldr x1, _StackSize     /* Stack size arg1 */

  /* Zero Init stack */
  add x2, x0, x1         /* End of Stack */
  mov x3, x0             /* Stack Base */

  mov v4.d[0], xzr
  mov v4.d[1], xzr
  mov v5.16b, v4.16b
  mov v6.16b, v4.16b
  mov v7.16b, v4.16b

_ClearStack:
  /* Assumes StackBase is 128-bit aligned, StackSize is a multiple of 64B */
  st4   {v4.16b, v5.16b, v6.16b, v7.16b}, [x3], #64  /* Fill every 64 bytes */
  cmp   x3, x2                                   /* Compare Size */
  b.lt  _ClearStack

  add sp, x2, xzr                                /* Initalize SP */

_EnableCache:
  /* BootShim enforces the Linux loader contract (MMU/D-cache off) before
   * relocation. No whole-cache set/way operation is safe or needed here.
   * Keep D-cache off until MemoryPeim installs the Normal-memory mappings. */
  bl ASM_PFX(ArmEnableInstructionCache)

_PrepareArguments:
  /* x0 = _StackBase and x1 = _StackSize */
  ldr x0, _StackBase     /* Stack base arg0 */
  ldr x1, _StackSize     /* Stack size arg1 */

  bl CEntryPoint

.align 3
dead:
  b dead                      /* We should never get here */

SecondaryCpuEntry:
  /* Keep the incoming index */
  mov x19, x4

  mov x5, #0x8
  sub x4, x5, x4

  ubfiz x2, x4, #15, #8

_SetupSecondaryCoreStack:
  ldr x3, _StackBase     /* Stack base arg0 */

  add x2, x3, x2
  sub x3, x2, #0x8000    /* Stack size arg1 */

  mov x4, xzr
  mov x5, xzr

_ClearStackSecondary:
  /* Assumes StackBase is 128-bit aligned, StackSize is a multiple of 16B */
  stp     x4, x5, [x3], #16
  cmp     x3, x2                                   /* Compare Size */
  b.lt    _ClearStackSecondary

  add sp, x2, xzr                                  /* Initalize SP */

  /* Secondary entry must also arrive with clean caches and M/C off.
   * Do not resurrect the removed whole-cache set/way operation. */

  /* Enter MpPark spin */
  mov x0, x19
  bl SecondaryCEntryPoint

secondarydead:
  wfe
  b secondarydead                      /* We should never get here */
