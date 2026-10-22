/** @file
  Pixel 7 (panther / GS201) keypad backend.

  GS201 moved the ALIVE pin controllers that carry the volume keys, so the
  shared GS101 implementation cannot be reused: reading its addresses on this
  SoC raises a synchronous external abort. All register facts and the gesture
  mapping are documented in PantherKeypadLogic.h.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/KeypadDeviceImplLib.h>
#include <Protocol/KeypadDevice.h>

STATIC KEYPAD_RETURN_API  *mKeypadReturnApi;

STATIC
VOID
PantherKeypadPush (
  IN EFI_KEY_DATA  *KeyData
  )
{
  if (mKeypadReturnApi != NULL) {
    mKeypadReturnApi->PushEfikeyBufTail (mKeypadReturnApi, KeyData);
  }
}

#define KP_READ(Address)  MmioRead32 (Address)
#define KP_PUSH(KeyData)  PantherKeypadPush (KeyData)
#include "PantherKeypadLogic.h"

RETURN_STATUS
EFIAPI
KeypadDeviceImplConstructor (
  VOID
  )
{
  PantherKeypadResetState ();
  return RETURN_SUCCESS;
}

EFI_STATUS
EFIAPI
KeypadDeviceImplReset (
  KEYPAD_DEVICE_PROTOCOL  *This
  )
{
  PantherKeypadResetState ();
  return EFI_SUCCESS;
}

EFI_STATUS
KeypadDeviceImplGetKeys (
  KEYPAD_DEVICE_PROTOCOL  *This,
  KEYPAD_RETURN_API       *KeypadReturnApi,
  UINT64                  Delta
  )
{
  mKeypadReturnApi = KeypadReturnApi;
  PantherKeypadPoll (Delta);
  return EFI_SUCCESS;
}
