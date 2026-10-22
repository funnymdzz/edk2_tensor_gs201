#include <Library/PcdLib.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HobLib.h>
#include <Library/SerialPortLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PlatformPrePiLib.h>

#include "PlatformUtils.h"

// Bring-up probe: raw write into the ramoops pmsg record, no dependencies.
// Read back on Android: cat /sys/fs/pstore/pmsg-ramoops-0
#define PM_BASE      0xfd5ff000ULL
#define PM_DATA      (PM_BASE + 64)
#define PM_CTR       (PM_DATA + 32 * 32)

#define PSTAGE(n) do {                                                \
    volatile UINT32 *_hdr = (volatile UINT32 *)(UINTN)PM_BASE;                 \
    volatile UINT32 *_ctr = (volatile UINT32 *)(UINTN)PM_CTR;                  \
    UINT32 _seq = *_ctr;                                                       \
    UINT64 _rec = (UINT64)(UINTN)(PM_DATA + (_seq & 31) * 32);                 \
    volatile UINT32 *_d = (volatile UINT32 *)(UINTN)_rec;                      \
    _hdr[0] = 0x43474244;                                                      \
    _hdr[1] = 0;                                                               \
    _hdr[2] = 2048;                                                            \
    _d[0] = 0x44454247;                                                        \
    _d[1] = (UINT32)(n);                                               \
    _d[2] = (UINT32)(0);                                                 \
    _d[3] = _seq;                                                              \
    _d[4] = 0;                                                                 \
    _d[5] = 0;                                                                 \
    _d[6] = 0;                                                                 \
    _d[7] = 0x444E4521;                                                        \
    *_ctr = _seq + 1;                                                          \
    __asm__ __volatile__ ("dsb sy" ::: "memory");                              \
    /* Clean the record line AND the header line to DRAM. The region is        \
       WRITE_BACK, so without this a watchdog reset loses the newest records. */ \
    __asm__ __volatile__ ("dc cvac, %0" :: "r"(_rec) : "memory");             \
    __asm__ __volatile__ ("dc cvac, %0" :: "r"((UINT64)(UINTN)PM_BASE) : "memory"); \
    __asm__ __volatile__ ("dsb sy" ::: "memory");                              \
    *(volatile UINT32 *)(UINTN)0x10060008 = 0xFFFF;   /* pet watchdog */       \
  } while (0)

VOID InitializeSharedUartBuffers(VOID)
{
  PSTAGE(0x20);
  // A zero base means display is deliberately disabled during bring-up.
  // Do not derive a cursor address from dimensions alone in that case.
  if (FixedPcdGet32(PcdMipiFrameBufferAddress) == 0) {
    return;
  }
  INTN* pFbConPosition = (INTN*)(FixedPcdGet32(PcdMipiFrameBufferAddress) + (FixedPcdGet32(PcdMipiFrameBufferWidth) *
                                                                              FixedPcdGet32(PcdMipiFrameBufferHeight) *
                                                                              FixedPcdGet32(PcdMipiFrameBufferPixelBpp) / 8));
  PSTAGE(0x21);
  *(pFbConPosition + 0) = 0;
  PSTAGE(0x22);
  *(pFbConPosition + 1) = 0;
  PSTAGE(0x23);
}

VOID DisableWDT(VOID)
{
	UINT32 wtcon;
	PSTAGE(0x30);
	// disable cl0
	wtcon = MmioRead32(0x10060000);
	PSTAGE(0x31);
	wtcon &= ~((1 << 5) | (1 << 0));
	MmioWrite32(0x10060000, wtcon);
	PSTAGE(0x32);
	// disable cl1
	wtcon = MmioRead32(0x10070000);
	PSTAGE(0x33);
	wtcon &= ~((1 << 5) | (1 << 0));
	MmioWrite32(0x10070000, wtcon);
	PSTAGE(0x34);
}

VOID UartInit(VOID)
{
  PSTAGE(0x40);
  SerialPortInitialize();
  PSTAGE(0x41);

  InitializeSharedUartBuffers();
  PSTAGE(0x42);

  PSTAGE(0x43);   // before DEBUG (DEBUG writes to the serial backend)
  DEBUG((EFI_D_INFO, "\nRenegade Project edk2-msm (AArch64)\n"));
  PSTAGE(0x44);
  DEBUG(
      (EFI_D_INFO, "Firmware version %s built %a %a\n\n",
       (CHAR16 *)PcdGetPtr(PcdFirmwareVersionString), __TIME__, __DATE__));
  PSTAGE(0x45);
}

VOID SetWatchdogState(BOOLEAN Enable)
{
  MmioWrite32(APSS_WDT_BASE + APSS_WDT_ENABLE_OFFSET, Enable);
}

VOID PlatformInitialize()
{
  PSTAGE(0x50);
  UartInit();
  PSTAGE(0x51);

  // Disable WatchDog Timer
  // BRING-UP: intentionally NOT called. The watchdog armed in ModuleEntryPoint
  // is what lets a hung boot reset itself with a WARM reset, preserving the
  // ramoops probe. Re-enable this once UEFI boots reliably.
  // DisableWDT();
  PSTAGE(0x52);
}
