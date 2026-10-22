// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Pixel 7 (panther / GS201) keypad logic.
//
// Register facts, from the device DT (device-collect/panther.dts):
//   gpio_keys/button@1 "KEY_VOLUMEDOWN" = <&gpa10 1 0xf>   (active low)
//   gpio_keys/button@2 "KEY_VOLUMEUP"   = <&gpa8  5 0xf>   (active low)
//   gpa10 is the 8th bank of pinctrl@180D0000 (ALIVE)      -> +0xe0
//   gpa8  is the 3rd bank of pinctrl@180E0000 (FAR_ALIVE)  -> +0x40
//   Samsung bank layout: CON +0x0, DAT +0x4, PUD +0x8, DRV +0xc (0x20/bank).
// Both blocks are in the always-on ALIVE domain and inside the mapped
// peripheral window. GS101 (raven) uses 0x174d/0x174e instead; reading those
// on GS201 raised a synchronous external abort, which is why the keypad
// drivers were disabled for panther until now.
//
// There is no power-key GPIO on panther: the DT declares it under
// s2mpg12-keys, i.e. behind the main PMIC, reachable only through the ACPM
// mailbox. So Enter/Esc are gestures on the two volume keys:
//
//   tap Vol Up / Vol Down        -> SCAN_UP / SCAN_DOWN (reported on release)
//   hold Vol Up / Vol Down       -> auto-repeat after 500 ms, every 150 ms
//   squeeze both, release <1.5 s -> Enter
//   squeeze both, release >=1.5s -> Esc
//
// The individual keys are suppressed for the whole press cycle in which a
// combination was seen, so a squeeze never also moves the selection.
//
// Hooks (so the algorithm can be host-tested without MMIO):
//   KP_READ(Address)   -> UINT32
//   KP_PUSH(KeyDataPtr)

#define KP_VOLDOWN_DAT (0x180D0000U + 0xE0U + 0x04U)
#define KP_VOLDOWN_BIT 1U
#define KP_VOLUP_DAT   (0x180E0000U + 0x40U + 0x04U)
#define KP_VOLUP_BIT   5U

#define KP_MS(Ms)          ((UINT64)(Ms) * 1000000ULL)
#define KP_REPEAT_DELAY_MS 500
#define KP_REPEAT_MS       150
#define KP_COMBO_MIN_MS    60   // shorter than this is contact bounce
#define KP_COMBO_ESC_MS    1500

typedef struct {
  BOOLEAN  Pressed;
  BOOLEAN  Suppressed; // a combination used this press cycle
  BOOLEAN  Repeating;
  UINT64   HeldNs;
  UINT64   RepeatNs;
} PANTHER_KEY_STATE;

STATIC PANTHER_KEY_STATE  mKeyUp;
STATIC PANTHER_KEY_STATE  mKeyDown;
STATIC BOOLEAN            mComboActive;
STATIC UINT64             mComboNs;

STATIC EFI_KEY_DATA  mKeyDataUp    = { { SCAN_UP, CHAR_NULL }, { 0, 0 } };
STATIC EFI_KEY_DATA  mKeyDataDown  = { { SCAN_DOWN, CHAR_NULL }, { 0, 0 } };
STATIC EFI_KEY_DATA  mKeyDataEnter = { { SCAN_NULL, CHAR_CARRIAGE_RETURN }, { 0, 0 } };
STATIC EFI_KEY_DATA  mKeyDataEsc   = { { SCAN_ESC, CHAR_NULL }, { 0, 0 } };

STATIC VOID
PantherKeypadResetState (
  VOID
  )
{
  ZeroMem (&mKeyUp, sizeof (mKeyUp));
  ZeroMem (&mKeyDown, sizeof (mKeyDown));
  mComboActive = FALSE;
  mComboNs     = 0;
}

STATIC VOID
PantherKeyTick (
  IN OUT PANTHER_KEY_STATE  *Key,
  IN     BOOLEAN            IsDown,
  IN     UINT64             DeltaNs,
  IN     EFI_KEY_DATA       *KeyData
  )
{
  if (IsDown) {
    if (!Key->Pressed) {
      Key->Pressed   = TRUE;
      Key->Repeating = FALSE;
      Key->HeldNs    = 0;
      Key->RepeatNs  = 0;
    }

    Key->HeldNs += DeltaNs;
    if (Key->Suppressed) {
      return;
    }

    if (!Key->Repeating) {
      if (Key->HeldNs >= KP_MS (KP_REPEAT_DELAY_MS)) {
        KP_PUSH (KeyData);
        Key->Repeating = TRUE;
        Key->RepeatNs  = 0;
      }
    } else {
      Key->RepeatNs += DeltaNs;
      if (Key->RepeatNs >= KP_MS (KP_REPEAT_MS)) {
        KP_PUSH (KeyData);
        Key->RepeatNs = 0;
      }
    }

    return;
  }

  if (Key->Pressed) {
    // A tap is reported on release, so that a squeeze (which suppresses the
    // key) never emits a stray cursor movement first.
    if (!Key->Suppressed && !Key->Repeating) {
      KP_PUSH (KeyData);
    }

    Key->Pressed    = FALSE;
    Key->Suppressed = FALSE;
    Key->Repeating  = FALSE;
    Key->HeldNs     = 0;
    Key->RepeatNs   = 0;
  }
}

STATIC VOID
PantherKeypadPoll (
  IN UINT64  DeltaNs
  )
{
  BOOLEAN  UpDown;
  BOOLEAN  DownDown;

  // Active low: a cleared bit means the button is held.
  UpDown   = (KP_READ (KP_VOLUP_DAT) & (1U << KP_VOLUP_BIT)) == 0;
  DownDown = (KP_READ (KP_VOLDOWN_DAT) & (1U << KP_VOLDOWN_BIT)) == 0;

  if (UpDown && DownDown) {
    mComboActive         = TRUE;
    mComboNs            += DeltaNs;
    mKeyUp.Suppressed    = TRUE;
    mKeyDown.Suppressed  = TRUE;
  } else if (mComboActive) {
    // Decide on release, so one gesture can mean either Enter or Esc.
    if (mComboNs >= KP_MS (KP_COMBO_ESC_MS)) {
      KP_PUSH (&mKeyDataEsc);
    } else if (mComboNs >= KP_MS (KP_COMBO_MIN_MS)) {
      KP_PUSH (&mKeyDataEnter);
    }

    mComboActive = FALSE;
    mComboNs     = 0;
  }

  PantherKeyTick (&mKeyUp, UpDown, DeltaNs, &mKeyDataUp);
  PantherKeyTick (&mKeyDown, DownDown, DeltaNs, &mKeyDataDown);
}
