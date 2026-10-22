// SPDX-License-Identifier: BSD-2-Clause-Patent
// Diagnostic only: test whether DPP0's inherited DMA base is CPU-physical.
// Runs in PrePi with the MMU off, so every access reaches DRAM directly.
// PX_* hooks allow host testing without MMIO or DRAM access.
#define PX_GLOBAL   0x1c240020U
#define PX_TRIGGER  0x1c240030U
#define PX_UPDATE   0x1c240050U
#define PX_CONTROL  0x1c275000U
#define PX_FUNC     0x1c255004U
#define PX_START    0x1c25500cU
#define PX_END      0x1c255010U
#define PX_COLOR0   0x1c255014U
#define PX_COLOR1   0x1c255018U
#define PX_DPP_CTRL 0x1c0b0008U
#define PX_DPP_SIZE 0x1c0b0010U
#define PX_DPP_BASE 0x1c0b0040U
#define PX_DPP_STRD 0x1c0b0050U
#define PX_REQUEST  0x80000020U

// Exact inherited layout: BGRA8888, 1080x2400, packed (stride 4320 bytes).
#define PX_FB       0xfac00000U
#define PX_WIDTH    1080U
#define PX_HEIGHT   2400U
#define PX_WORDS    (PX_WIDTH * PX_HEIGHT)
#define PX_BANDS    5U
#define PX_BAND_ROWS (PX_HEIGHT / PX_BANDS)

// Top to bottom: white, then exactly one byte set per band. p53 observed
// white / blue / green / red / black: byte0=B, byte1=G, byte2=R, byte3 is
// ignored (COPY blend), i.e. UEFI PixelBlueGreenRedReserved8BitPerColor.
STATIC CONST UINT32 mPixelBands[PX_BANDS] = {
  0xffffffffU, 0x000000ffU, 0x0000ff00U, 0x00ff0000U, 0xff000000U
};

STATIC BOOLEAN
PantherPixelWaitUpdate (VOID)
{
  UINTN Index;
  for (Index = 0; Index < 200; Index++) {
    if ((PX_READ (PX_UPDATE) & PX_REQUEST) == 0) {
      return TRUE;
    }
    PX_DELAY (1000); // bounded 200ms
  }
  return FALSE;
}

// Reads and logs the inherited display state; TRUE only for the exact
// p51-p53 handover state. Never writes anything.
STATIC BOOLEAN
PantherPixelGuard (OUT UINT32 *Trigger)
{
  UINT32 Global, Control, Base, Ctrl, Size;

  PX_LOG (2, 1); // reading guards
  Global = PX_READ (PX_GLOBAL);
  *Trigger = PX_READ (PX_TRIGGER);
  Control = PX_READ (PX_CONTROL);
  Base = PX_READ (PX_DPP_BASE);
  Ctrl = PX_READ (PX_DPP_CTRL);
  Size = PX_READ (PX_DPP_SIZE);
  PX_LOG (4, Global);
  PX_LOG (5, *Trigger);
  PX_LOG (6, Control);
  PX_LOG (7, Base);
  PX_LOG (8, Ctrl);
  PX_LOG (9, Size);

  if ((Global != 0x133U) || (*Trigger != 0x3070U) || (Control != 1U) ||
      (Base != PX_FB) || (Ctrl != 0xff400000U) || (Size != 0x09600438U) ||
      (PX_READ (PX_DPP_STRD) != 0U) || (PX_READ (PX_FUNC) != 0x102U) ||
      (PX_READ (PX_START) != 0U) || (PX_READ (PX_END) != 0x095f0437U) ||
      (PX_READ (PX_COLOR0) != 0U) || (PX_READ (PX_COLOR1) != 0U) ||
      (PX_READ (PX_UPDATE) != 0U)) {
    PX_LOG (3, 1);
    PX_LOG (2, 2); // skipped: no DRAM or display writes
    return FALSE;
  }
  return TRUE;
}

// Persistent display enable (DEN1). After the guard, request one shadow
// update of the unchanged configuration and leave the HW TE trigger
// unmasked, so DECON re-transfers the framebuffer on every panel TE and
// CPU writes (after cache clean) become visible. Window/DPP untouched.
STATIC VOID
PantherDisplayEnable (VOID)
{
  UINT32 Trigger;
  BOOLEAN Applied;

  if (!PantherPixelGuard (&Trigger)) {
    return;
  }
  PX_LOG (2, 5);
  PX_WRITE (PX_UPDATE, PX_REQUEST);
  PX_WRITE (PX_TRIGGER, (Trigger & ~0x11U) | 1U);
  Applied = PantherPixelWaitUpdate ();
  PX_LOG (25, PX_READ (PX_UPDATE));
  PX_LOG (26, Applied);
  PX_LOG (28, PX_READ (PX_TRIGGER));
  PX_LOG (3, Applied ? 0U : 2U);
  PX_LOG (2, 8); // enabled; trigger intentionally left unmasked
}

STATIC VOID
PantherPixelTest (VOID)
{
  UINT32 Trigger, Word, Xor;
  UINT32 Nonzero, Band[PX_BANDS], Mismatch;
  BOOLEAN Applied, Restored;
  UINTN Index, Row;

  if (!PantherPixelGuard (&Trigger)) {
    return;
  }

  // Read-only census first. Durable stage before the first DRAM access so a
  // bus abort here is distinguishable from one during writes.
  PX_LOG (2, 3);
  Nonzero = Xor = 0;
  for (Index = 0; Index < PX_BANDS; Index++) {
    Band[Index] = 0;
  }
  for (Row = 0; Row < PX_HEIGHT; Row++) {
    for (Index = 0; Index < PX_WIDTH; Index++) {
      Word = PX_FB_READ (Row * PX_WIDTH + Index);
      Xor ^= Word;
      if (Word != 0) {
        Nonzero++;
        Band[Row / PX_BAND_ROWS]++;
      }
    }
    if ((Row & 63) == 0) {
      PX_PET ();
    }
  }
  PX_LOG (10, Nonzero);
  PX_LOG (11, Xor);
  for (Index = 0; Index < PX_BANDS; Index++) {
    PX_LOG (12 + Index, Band[Index]);
  }
  for (Index = 0; Index < 8; Index++) {
    PX_LOG (17 + Index, PX_FB_READ ((150 + 300 * Index) * PX_WIDTH + 540));
  }

  PX_LOG (2, 4); // census done; about to write the test pattern
  for (Row = 0; Row < PX_HEIGHT; Row++) {
    Word = mPixelBands[Row / PX_BAND_ROWS];
    for (Index = 0; Index < PX_WIDTH; Index++) {
      PX_FB_WRITE (Row * PX_WIDTH + Index, Word);
    }
    if ((Row & 63) == 0) {
      PX_PET ();
    }
  }
  Mismatch = 0;
  for (Row = 0; Row < PX_HEIGHT; Row++) {
    Word = mPixelBands[Row / PX_BAND_ROWS];
    for (Index = 0; Index < PX_WIDTH; Index++) {
      if (PX_FB_READ (Row * PX_WIDTH + Index) != Word) {
        Mismatch++;
      }
    }
    if ((Row & 63) == 0) {
      PX_PET ();
    }
  }
  PX_LOG (29, Mismatch);

  // Request one shadow update of unchanged settings and unmask the HW TE
  // trigger (the sequence proven by p52), then hold with refresh enabled.
  PX_LOG (2, 5);
  PX_WRITE (PX_UPDATE, PX_REQUEST);
  PX_WRITE (PX_TRIGGER, (Trigger & ~0x11U) | 1U);
  Applied = PantherPixelWaitUpdate ();
  PX_LOG (25, PX_READ (PX_UPDATE));
  PX_LOG (26, Applied);
  for (Index = 0; Index < 50; Index++) {
    PX_DELAY (100000); // 5 seconds
    PX_PET ();
  }

  // Restore the original masked trigger; the command-mode panel keeps the
  // last transferred frame. No window or DPP register was modified.
  PX_LOG (2, 6);
  PX_WRITE (PX_TRIGGER, Trigger);
  Restored = (PX_READ (PX_TRIGGER) == Trigger);
  PX_LOG (27, Restored);
  PX_LOG (28, PX_READ (PX_TRIGGER));
  PX_LOG (30, PX_READ (PX_UPDATE));
  PX_LOG (3, !Restored ? 3U : (!Applied ? 2U : 0U));
  PX_LOG (2, 7); // finished; visibility requires user observation
}
