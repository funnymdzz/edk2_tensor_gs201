// SPDX-License-Identifier: BSD-2-Clause-Patent
// Diagnostic only. Register facts checked against GS201 cal_9845/9855.
// Independent implementation; CD_* hooks allow host testing without MMIO.
#define CD_GLOBAL  0x1c240020U
#define CD_TRIGGER 0x1c240030U
#define CD_UPDATE  0x1c240050U
#define CD_CONTROL 0x1c275000U
#define CD_FUNC    0x1c255004U
#define CD_START   0x1c25500cU
#define CD_END     0x1c255010U
#define CD_COLOR0  0x1c255014U
#define CD_COLOR1  0x1c255018U
#define CD_REQUEST 0x80000020U

STATIC BOOLEAN
PantherColorWaitUpdate (VOID)
{
  UINTN Index;
  for (Index = 0; Index < 200; Index++) {
    if ((CD_READ (CD_UPDATE) & CD_REQUEST) == 0) {
      return TRUE;
    }
    CD_DELAY (1000); // bounded 200ms, provided the MMIO access itself returns
  }
  return FALSE;
}

STATIC VOID
PantherColorTest (VOID)
{
  UINT32 Global, Trigger, Control, Color0, Color1;
  BOOLEAN Applied, Restored;
  UINTN Index;

  CD_LOG (2, 1); // started; log storage initialized by caller
  Global = CD_READ (CD_GLOBAL);
  Trigger = CD_READ (CD_TRIGGER);
  Control = CD_READ (CD_CONTROL);
  Color0 = CD_READ (CD_COLOR0);
  Color1 = CD_READ (CD_COLOR1);
  CD_LOG (4, Global);
  CD_LOG (5, Trigger);
  CD_LOG (6, Control);
  CD_LOG (7, Color0);
  CD_LOG (8, Color1);

  // Fail closed unless this is the exact quiescent p51 handover state.
  // COPY blending is retained, not rewritten. No geometry or DMA writes.
  if ((Global != 0x133U) || (Trigger != 0x3070U) ||
      (Control != 1U) || (CD_READ (CD_FUNC) != 0x102U) ||
      (CD_READ (CD_START) != 0U) || (CD_READ (CD_END) != 0x095f0437U) ||
      (Color0 != 0U) || (Color1 != 0U) || (CD_READ (CD_UPDATE) != 0U)) {
    CD_LOG (3, 1); // guard mismatch, zero display writes
    CD_LOG (2, 2);
    return;
  }

  CD_LOG (2, 3); // about to modify; durable before the first write
  // Opaque green in current 8bpc mode; preserve every reserved bit.
  CD_WRITE (CD_COLOR0, (Color0 & ~0x00ff03ffU) | 0x00ff0000U);
  CD_WRITE (CD_COLOR1, (Color1 & ~0x03ff03ffU) | 0x00ff0000U);
  CD_WRITE (CD_CONTROL, Control | 2U);
  CD_WRITE (CD_UPDATE, CD_REQUEST);
  CD_WRITE (CD_TRIGGER, (Trigger & ~0x11U) | 1U); // HW TE unmask
  Applied = PantherColorWaitUpdate ();
  CD_LOG (9, CD_READ (CD_UPDATE));
  CD_LOG (10, Applied);
  CD_LOG (2, 4);
  if (Applied) {
    for (Index = 0; Index < 30; Index++) {
      CD_DELAY (100000); // 3 seconds; no watchdog feed in this test
    }
  }

  // Always attempt restoration, including on a shadow-update timeout.
  // Mask first, restore window settings, submit, then restore trigger state.
  CD_LOG (2, 5);
  CD_WRITE (CD_TRIGGER, Trigger);
  CD_WRITE (CD_COLOR0, Color0);
  CD_WRITE (CD_COLOR1, Color1);
  CD_WRITE (CD_CONTROL, Control);
  CD_WRITE (CD_UPDATE, CD_REQUEST);
  CD_WRITE (CD_TRIGGER, (Trigger & ~0x11U) | 1U);
  Restored = PantherColorWaitUpdate ();
  CD_WRITE (CD_TRIGGER, Trigger);
  CD_LOG (11, CD_READ (CD_UPDATE));
  CD_LOG (12, Restored);
  CD_LOG (13, CD_READ (CD_CONTROL));
  CD_LOG (14, CD_READ (CD_TRIGGER));
  CD_LOG (15, CD_READ (CD_COLOR0));
  CD_LOG (16, CD_READ (CD_COLOR1));
  CD_LOG (3, !Restored ? 3U : (!Applied ? 2U : 0U));
  CD_LOG (2, 6); // finished, NOT proof that pixels reached the panel
}
