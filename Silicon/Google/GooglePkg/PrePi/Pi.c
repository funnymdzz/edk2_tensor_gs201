// Pi.c: Entry point for SEC(Security).

#include <PiPei.h>
#include <Pi/PiBootMode.h>

#include <Library/PrePiLib.h>
#include <Library/PrintLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/PlatformPrePiLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#include <Ppi/GuidedSectionExtraction.h>

#include <Library/MemoryMapHelperLib.h>

#include "Pi.h"

#define IS_XIP()  (((UINT64)FixedPcdGet64 (PcdFdBaseAddress) > mSystemMemoryEnd) ||\
                  ((FixedPcdGet64 (PcdFdBaseAddress) + FixedPcdGet32 (PcdFdSize)) <= FixedPcdGet64 (PcdSystemMemoryBase)))

UINT64  mSystemMemoryEnd = FixedPcdGet64 (PcdSystemMemoryBase) +
                           FixedPcdGet64 (PcdSystemMemorySize) - 1;

VOID EFIAPI ProcessLibraryConstructorList(VOID);

char* ptr = (char*)0xfd4ff000;
void printInRam(char* str){
	
  for(int i = 0; i < 3; i++){
    *ptr = *str;
  	ptr++;
  	str++;
  }
  *ptr = '\n';
  ptr++;
}

void testShit(){
  int* startAddress = (int*)0x80000000;
  int* endAddress = (int*)0x880000000;
  int size = 200;

  int black = 0x000000FF;

  void* location = 0x0;

  int** joso = (int**)0xfd4ff000;

  for(int* i = startAddress; i < endAddress; i+=size){
    int* newAddress = i;
    int eh = 1;
    for(int j = 0; j < size; j++){
      if(newAddress[j] != black) eh = 0;
    }
    if(eh) /**joso = newAddress;*/DEBUG((EFI_D_INFO, "Address is: %x", newAddress));
  }

}

void fillMe(char* adr, long size){
	for(int i = 0; i < size; i += 1){
		*(adr+i) = 0xAA;
	}
}

int ramTest(char* start, long size){
  int out = 0;
  char* end = start + size;
  
  DEBUG((EFI_D_INFO, "Testing %llx-%llx\n", start, end));

  for(char* i = start; i < end; i++){
    *i = 0;
  }
  
  for(char* i = start; i < end; i++){
    out |= (*i != 0);
  }
  
  DEBUG((EFI_D_INFO, "Result %d\n", out));
  return out; //0 for success
  
}

// Bring-up progress probe: writes a stage marker into the ramoops pmsg record
// so execution can be traced even when the display is dead.
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

// GS201 display handover diagnostic. Addresses are restricted to active
// DECON0/DPP0 blocks identified by the device DT and bootloader log. Only
// configuration/status registers are read here. (p53 later proved the DMA
// base 0xfac00000 is CPU-physical; see PantherPixelTest.h.)
STATIC
VOID
PantherDisplaySnapshot (VOID)
{
  STATIC CONST UINT32 Registers[] = {
    0x1c240000, 0x1c240004, 0x1c240020, 0x1c240030, // DECON0
    0x1c240200, 0x1c240800, 0x1c240820, 0x1c240830,
    0x1c275000, // DECON0 window 5 control: channel, enable, map-color
    0x1c0b0000, 0x1c0b0008, 0x1c0b000c, // DPP0 RDMA control
    0x1c0b0010, 0x1c0b0014, 0x1c0b0018, // source/image geometry
    0x1c0b0040, 0x1c0b0044, // DMA base Y8/C8, not proven physical
    0x1c0b0050, 0x1c0b0054, 0x1c0b0058, // strides
    0x1c0b0408, 0x1c0b0410, 0x1c0b0414, 0x1c0b0418,
    0x1c0b0440, 0x1c0b0444, 0x1c0b0450, 0x1c0b0454, // shadow
    // Do not probe SysMMUs: p50 stopped with FLPT (0x1c10000c)
    // pending and was watchdog-reset. Zero CTRL/CFG/STATUS is not bypass proof.
    // DT "win" base 0x1c250000, WIN_OFFSET(5) = 0x5000.
    // cal_9845 regs-decon.h: FUNC0/FUNC1, geometry, COLORMAP0/1.
    // Skip WIN_SECURE_CON; inspect only the active window's display settings.
    0x1c255004, 0x1c255008,
    0x1c25500c, 0x1c255010,
    0x1c255014, 0x1c255018
  };
  volatile UINT32 *Snapshot = (volatile UINT32 *)(UINTN)(PM_BASE + 0x500);
  UINTN Index;
  UINT64 Line;

  Snapshot[0] = 0x31505344; // "DSP1", outside the rotating probe slots
  Snapshot[1] = ARRAY_SIZE (Registers);
  Snapshot[2] = 0; // completed reads
  Snapshot[3] = 0; // pending MMIO address, zero means none
  Snapshot[4] = 0; // complete flag
  Snapshot[5] = 1; // format version
  Snapshot[6] = Snapshot[7] = 0;
  for (Index = 0; Index < ARRAY_SIZE (Registers); Index++) {
    Snapshot[3] = Registers[Index];
    __asm__ __volatile__ ("dsb sy; dc cvac, %0; dsb sy"
                          :: "r"((UINT64)(UINTN)Snapshot) : "memory");
    Snapshot[8 + Index * 2] = Registers[Index];
    Snapshot[9 + Index * 2] = *(volatile UINT32 *)(UINTN)Registers[Index];
    Line = (UINT64)(UINTN)&Snapshot[8 + Index * 2];
    __asm__ __volatile__ ("dsb sy; dc cvac, %0; dsb sy" :: "r"(Line) : "memory");
    Snapshot[2] = Index + 1;
  }

  Snapshot[3] = 0;
  Snapshot[4] = 1;
  __asm__ __volatile__ ("dsb sy; dc cvac, %0; dsb sy"
                        :: "r"((UINT64)(UINTN)Snapshot) : "memory");
}

// COL1 record at physical pmsg+0x700, outside DSP1 and rotating probes.
STATIC VOID
PantherColorLog (UINTN Index, UINT32 Value)
{
  volatile UINT32 *Record = (volatile UINT32 *)(UINTN)(PM_BASE + 0x700);
  Record[Index] = Value;
  __asm__ __volatile__ ("dsb sy; dc cvac, %0; dsb sy"
                        :: "r"((UINT64)(UINTN)&Record[Index]) : "memory");
}

#define CD_READ(Address) MmioRead32 (Address)
#define CD_WRITE(Address, Value) MmioWrite32 (Address, Value)
#define CD_DELAY(Us) MicroSecondDelay (Us)
#define CD_LOG(Index, Value) PantherColorLog (Index, Value)
#include "PantherColorTest.h"

// PIX1 record at physical pmsg+0x780 (32 words). pstore exposes 2048 data
// bytes after its 12-byte header, i.e. up to pmsg+0x80c.
STATIC VOID
PantherPixelLog (UINTN Index, UINT32 Value)
{
  volatile UINT32 *Record = (volatile UINT32 *)(UINTN)(PM_BASE + 0x780);
  Record[Index] = Value;
  __asm__ __volatile__ ("dsb sy; dc cvac, %0; dsb sy"
                        :: "r"((UINT64)(UINTN)&Record[Index]) : "memory");
}

#define PX_READ(Address) MmioRead32 (Address)
#define PX_WRITE(Address, Value) MmioWrite32 (Address, Value)
#define PX_DELAY(Us) MicroSecondDelay (Us)
#define PX_PET() MmioWrite32 (0x10060008, 0xFFFF)
#define PX_LOG(Index, Value) PantherPixelLog (Index, Value)
#define PX_FB_READ(Word) MmioRead32 (PX_FB + (UINTN)(Word) * 4)
#define PX_FB_WRITE(Word, Value) MmioWrite32 (PX_FB + (UINTN)(Word) * 4, Value)
#include "PantherPixelTest.h"

VOID
PrePiMain(
  IN VOID *StackBase,
  IN UINTN StackSize
  )
{

  EFI_HOB_HANDOFF_INFO_TABLE *HobList;
  EFI_STATUS                  Status;

  UINTN MemoryBase     = 0;
  UINTN MemorySize     = 0;
  UINTN UefiMemoryBase = 0;
  UINTN UefiMemorySize = 0;

  PSTAGE(1);

	//printInRam("HI!");

  if (FixedPcdGet32 (PcdDeconBaseAddress) == 0x1c240000) {
    // Preserve the GS201 bootloader display configuration until verified.
    PantherDisplaySnapshot ();
    if (FixedPcdGetBool (PcdPantherDisplayEnable)) {
      UINTN EnableIndex;
      for (EnableIndex = 0; EnableIndex < 32; EnableIndex++) {
        PantherPixelLog (EnableIndex, 0);
      }
      PantherPixelLog (0, 0x314e4544); // DEN1
      PantherPixelLog (1, 1);
      PantherPixelLog (31, 0x444E4521); // !END
      PantherDisplayEnable ();
    }
  } else {
    // Existing GS101 path; not used for Pixel 7.
    *(volatile UINT32 *)(FixedPcdGet32 (PcdDeconBaseAddress) + 0x30) = 0x1281;
  }

  PSTAGE(2);

  //fillMe((char*)0xFA000000, 0x11123800);

  // Architecture-specific initialization
  // Enable Floating Point
  ArmEnableVFP();

  if (ArmReadCurrentEL() == AARCH64_EL2) {
    // Trap General Exceptions. All exceptions that would be routed to EL1 are routed to EL2
    ArmWriteHcr(ARM_HCR_TGE);

    /* Enable Timer access for non-secure EL1 and EL0
       The cnthctl_el2 register bits are architecturally
       UNKNOWN on reset.
       Disable event stream as it is not in use at this stage
    */
    ArmWriteCntHctl(CNTHCTL_EL2_EL1PCTEN | CNTHCTL_EL2_EL1PCEN);
  }

  if ((FixedPcdGet32 (PcdDeconBaseAddress) == 0x1c240000) &&
      FixedPcdGetBool (PcdPantherColorTest)) {
    UINTN ColorIndex;
    for (ColorIndex = 0; ColorIndex < 64; ColorIndex++) {
      PantherColorLog (ColorIndex, 0);
    }
    PantherColorLog (0, 0x314c4f43); // COL1
    PantherColorLog (1, 1);
    PantherColorTest ();
  }

  if ((FixedPcdGet32 (PcdDeconBaseAddress) == 0x1c240000) &&
      FixedPcdGetBool (PcdPantherPixelTest)) {
    UINTN PixelIndex;
    for (PixelIndex = 0; PixelIndex < 32; PixelIndex++) {
      PantherPixelLog (PixelIndex, 0);
    }
    PantherPixelLog (0, 0x31584950); // PIX1
    PantherPixelLog (1, 1);
    PantherPixelLog (31, 0x444E4521); // !END
    PantherPixelTest ();
  }

  /* AArch64: no separate upstream branch-prediction enable API. */

  PSTAGE(3);

  ARM_MEMORY_REGION_DESCRIPTOR_EX DxeHeap;
  ARM_MEMORY_REGION_DESCRIPTOR_EX UefiStack;
  ARM_MEMORY_REGION_DESCRIPTOR_EX UefiFd;

  Status = LocateMemoryMapAreaByName("DXE Heap", &DxeHeap);
  ASSERT_EFI_ERROR (Status);

  Status = LocateMemoryMapAreaByName("UEFI Stack", &UefiStack);
  ASSERT_EFI_ERROR (Status);

  Status = LocateMemoryMapAreaByName("UEFI FD", &UefiFd);
  ASSERT_EFI_ERROR (Status);

  // Declare UEFI region
  MemoryBase     = FixedPcdGet32(PcdSystemMemoryBase);
  MemorySize     = FixedPcdGet32(PcdSystemMemorySize);
  UefiMemoryBase = DxeHeap.Address;
  UefiMemorySize = DxeHeap.Length;
  
  StackBase      = (VOID *)(UefiMemoryBase + UefiMemorySize - StackSize);

  DEBUG(
      (EFI_D_INFO | EFI_D_LOAD,
       "UEFI Memory Base = 0x%llx, Size = 0x%llx \n"
       "Stack Base = 0x%llx, Stack Size = 0x%llx \n",
       UefiMemoryBase, UefiMemorySize, StackBase, StackSize));

  // Set up HOB
  HobList = HobConstructor(
      (VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase,
      StackBase);

  PrePeiSetHobList (HobList);

  // Invalidate cache
  InvalidateDataCacheRange(
      (VOID *)(UINTN)PcdGet64(PcdFdBaseAddress), PcdGet32(PcdFdSize));

  PSTAGE(4);

  // Initialize MMU
  Status = MemoryPeim(UefiMemoryBase, UefiMemorySize);
  ASSERT_EFI_ERROR (Status);

  PSTAGE(5);
  
  // Add HOBs
  BuildStackHob ((UINTN)StackBase, StackSize);

  // TODO: Call CpuPei as a library
  BuildCpuHob (ArmGetPhysicalAddressBits (), PcdGet8 (PcdPrePiCpuIoSize));

  // Set the Boot Mode
  SetBootMode (BOOT_WITH_DEFAULT_SETTINGS);

  // Initialize Platform HOBs (CpuHob and FvHob)
  DEBUG((EFI_D_INFO, "PlatformPeim In \n"));
  Status = PlatformPeim();
  ASSERT_EFI_ERROR (Status);

  PSTAGE(6);

  // Now, the HOB List has been initialized, we can register performance information
  // PERF_START (NULL, "PEI", NULL, StartTimeStamp);

  // SEC phase needs to run library constructors by hand.
  ProcessLibraryConstructorList();

  // Assume the FV that contains the SEC (our code) also contains a compressed FV.
  DEBUG((EFI_D_INFO, "DecompressFirstFv In \n"));
  PSTAGE(7);
  Status = DecompressFirstFv();
  ASSERT_EFI_ERROR (Status);

  PSTAGE(8);

  // Load the DXE Core and transfer control to it
  DEBUG((EFI_D_INFO, "LoadDxeCoreFromFv In \n"));
  PSTAGE(9);
  Status = LoadDxeCoreFromFv(NULL, 0);
  ASSERT_EFI_ERROR (Status);
  PSTAGE(10);
  DEBUG((EFI_D_INFO, "AAAAAAAAAAAAAA\n"));
}

VOID
CEntryPoint(
  IN VOID *StackBase,
  IN UINTN StackSize
  )
{
  PSTAGE(0x10);   // reached CEntryPoint (assembly prologue all OK)

  // Do platform specific initialization here
  PlatformInitialize();

  PSTAGE(0x11);   // PlatformInitialize returned (UartInit + DisableWDT OK)

  // Goto primary Main.
  PrePiMain(StackBase, StackSize);

  // DXE Core should always load and never return
  ASSERT(FALSE);
}

VOID
SecondaryCEntryPoint(
  IN  UINTN  MpId
  )
{
  // We must never get into this function on UniCore system
  ASSERT(FALSE);
}
