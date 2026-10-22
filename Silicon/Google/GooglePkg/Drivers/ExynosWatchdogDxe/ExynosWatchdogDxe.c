/** @file
  Periodically reloads the Exynos/Tensor cluster-0 watchdog counter.

  PrePi programs WTCON (prescaler 0xff, divider 128, reset enabled) and the
  board-level ABL leaves it armed. Nothing in DXE/BDS/Shell would otherwise
  reload it, so an idle but healthy UEFI (e.g. sitting at "Shell>") was reset
  after tens of seconds. The reload runs from a TPL_NOTIFY timer event: if
  interrupts or the timer stop, reloads stop and the board still resets.

  PcdWatchdogReloadSeconds bounds the reloads during bring-up: after that
  many seconds the driver stops, so a board left in UEFI without input
  returns to the normal OS by itself (0 = reload forever).

  The watchdog is intentionally left armed across ExitBootServices: an OS
  that neither drives nor disables it will be reset, which is the safe
  failure mode during bring-up.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define WT_CNT_OFFSET   0x8
#define WT_RELOAD       0xFFFFU
#define WT_PERIOD_100NS (10 * 1000 * 1000) // 1 second

STATIC EFI_EVENT  mWatchdogEvent;
STATIC UINT32     mWatchdogTicks;

STATIC
VOID
ExynosWatchdogReload (
  VOID
  )
{
  MmioWrite32 (FixedPcdGet32 (PcdWatchdogBase) + WT_CNT_OFFSET, WT_RELOAD);
}

STATIC
VOID
EFIAPI
ExynosWatchdogTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT32  Limit;

  Limit = FixedPcdGet32 (PcdWatchdogReloadSeconds);
  if ((Limit != 0) && (mWatchdogTicks >= Limit)) {
    gBS->SetTimer (mWatchdogEvent, TimerCancel, 0);
    DEBUG ((DEBUG_ERROR, "ExynosWatchdogDxe: reload limit reached, board will reset\n"));
    return;
  }
  mWatchdogTicks++;
  ExynosWatchdogReload ();
}

EFI_STATUS
EFIAPI
ExynosWatchdogDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  if (FixedPcdGet32 (PcdWatchdogBase) == 0) {
    return EFI_UNSUPPORTED;
  }

  ExynosWatchdogReload ();

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  ExynosWatchdogTick,
                  NULL,
                  &mWatchdogEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ExynosWatchdogDxe: CreateEvent: %r\n", Status));
    return Status;
  }

  Status = gBS->SetTimer (mWatchdogEvent, TimerPeriodic, WT_PERIOD_100NS);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ExynosWatchdogDxe: SetTimer: %r\n", Status));
    gBS->CloseEvent (mWatchdogEvent);
    return Status;
  }

  return EFI_SUCCESS;
}
