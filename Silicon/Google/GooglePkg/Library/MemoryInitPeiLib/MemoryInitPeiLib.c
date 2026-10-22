/** @file

  Copyright (c) 2011-2015, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#define FDT_DIRECT
#include <PiPei.h>
// ===== TEMPORARY BRING-UP PROBE (panther port) =====
// Ring of 32 records so the ORDER of steps survives a reset. Old single-slot
// probes here overwrote slot 0 and shrank the header to 64 bytes, which hid
// every later record.
#define PM_BASE      0xfd5ff000ULL
#define PM_DATA      (PM_BASE + 64)
#define PM_CTR       (PM_DATA + 32 * 32)

#define MPSTAGE(n, v) do {                                                \
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
    _d[2] = (UINT32)(v);                                                 \
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


#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/FdtParserLib.h>
#include <Library/PlatformMemoryMapLib.h>

#define SIZE_KB ((UINTN)(1024))
#define SIZE_MB ((UINTN)(SIZE_KB * 1024))
#define SIZE_GB ((UINTN)(SIZE_MB * 1024))
#define SIZE_MB_BIG(_Size,_Value) ((_Size) > ((_Value) * SIZE_MB))
#define SIZE_MB_SMALL(_Size,_Value) ((_Size) < ((_Value) * SIZE_MB))
#define SIZE_MB_IN(_Min,_Max,_Size) \
  if (SIZE_MB_BIG((MemoryTotal), (_Min)) && SIZE_MB_SMALL((MemoryTotal), (_Max)))\
    Mem = Mem##_Size##G, MemGB = _Size

extern UINT64 mSystemMemoryEnd;


VOID BuildMemoryTypeInformationHob(VOID);


STATIC
VOID InitMmu(IN ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable)
{

  VOID *        TranslationTableBase;
  UINTN         TranslationTableSize;
  RETURN_STATUS Status;

  // Note: Because we called PeiServicesInstallPeiMemory() before
  // to call InitMmu() the MMU Page Table resides in
  // RAM (even at the top of DRAM as it is the first permanent memory
  // allocation)
  Status = ArmConfigureMmu(
      MemoryTable, &TranslationTableBase, &TranslationTableSize);

  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Error: Failed to enable MMU: %r\n", Status));
  }
}

STATIC
VOID AddHob(PARM_MEMORY_REGION_DESCRIPTOR_EX Desc)
{
  BuildResourceDescriptorHob(
      Desc->ResourceType, Desc->ResourceAttribute, Desc->Address, Desc->Length);

  if (Desc->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY ||
      Desc->MemoryType == EfiRuntimeServicesData)
  {
    BuildMemoryAllocationHob(Desc->Address, Desc->Length, Desc->MemoryType);
  }
}

/*++

Routine Description:



Arguments:

  FileHandle  - Handle of the file being invoked.
  PeiServices - Describes the list of possible PEI Services.

Returns:

  Status -  EFI_SUCCESS if the boot mode could be set

--*/
EFI_STATUS
EFIAPI
MemoryPeim(IN EFI_PHYSICAL_ADDRESS UefiMemoryBase, IN UINT64 UefiMemorySize)
{

  PARM_MEMORY_REGION_DESCRIPTOR_EX MemoryDescriptorEx =
      GetPlatformMemoryMap();
  ARM_MEMORY_REGION_DESCRIPTOR
        MemoryDescriptor[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
  UINTN Index = 0;
  UINTN Node = 0;
  UINTN MemoryBase = 0;
  UINTN MemorySize = 0;
  UINTN MemoryTotal = 0;
  DeviceMemoryAddHob Mem = Mem4G;
  UINT8 MemGB = 4;
  fdt *Fdt;

  Fdt = GetFdt();

  MPSTAGE (0x50, (UINTN)Fdt);


  ASSERT(Fdt != NULL);

  while (fdt_get_memory(Fdt, (int)Node, (uint64_t*)&MemoryBase, (uint64_t*)&MemorySize)) {
    MemoryTotal += MemorySize;
    DEBUG((
      EFI_D_INFO,
      "FDT Memory %-2d: 0x%016llx - 0x%016llx (0x%016llx)\n",
      Node, MemoryBase, (MemoryBase + MemorySize), MemorySize
    ));
    Node++;
  }

  // Memory   Min    Max   Config
  SIZE_MB_IN (3072,  4608, 4);
  SIZE_MB_IN (5120,  6656, 6);
  SIZE_MB_IN (7168,  8704, 8);
  SIZE_MB_IN (9216, 10752, 10);
  SIZE_MB_IN (11520, 12488, 12);

  MPSTAGE (0x52, MemoryTotal);
  MPSTAGE (0x53, (UINTN)Node);


  DEBUG((EFI_D_INFO, "FDT Memory Total: 0x%016lx (%d GiB)\n", MemoryTotal, MemoryTotal / SIZE_GB));
  DEBUG((EFI_D_INFO, "Select Config: %d GiB\n", MemGB));

  // Run through each memory descriptor
  while (MemoryDescriptorEx->Length != 0) {
    if (MemoryDescriptorEx->MemoryType == EfiConventionalMemory)
      MemoryTotal += MemoryDescriptorEx->Length;
    switch (MemoryDescriptorEx->HobOption) {
    case Mem4G:
    case Mem6G:
    case Mem8G:
    case Mem10G:
    case Mem12G:
      if (MemoryDescriptorEx->HobOption != Mem) {
        MemoryDescriptorEx++;
        continue;
      }
      // fallthrough
    case AddMem:
    case AddDev:
    case HobOnlyNoCacheSetting:
      AddHob(MemoryDescriptorEx);
      break;
    case NoHob:
    default:
      goto update;
    }

    if (MemoryDescriptorEx->HobOption == HobOnlyNoCacheSetting) {
      MemoryDescriptorEx++;
      continue;
    }

  update:
    ASSERT(Index < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT);

    MemoryDescriptor[Index].PhysicalBase = MemoryDescriptorEx->Address;
    MemoryDescriptor[Index].VirtualBase  = MemoryDescriptorEx->Address;
    MemoryDescriptor[Index].Length       = MemoryDescriptorEx->Length;
    MemoryDescriptor[Index].Attributes   = MemoryDescriptorEx->ArmAttributes;

    Index++;
    MemoryDescriptorEx++;
  }

  // Last one (terminator)
  ASSERT(Index < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT);
  MemoryDescriptor[Index].PhysicalBase = 0;
  MemoryDescriptor[Index].VirtualBase  = 0;
  MemoryDescriptor[Index].Length       = 0;
  MemoryDescriptor[Index].Attributes   = 0;

  MPSTAGE (0x54, Index);


  // Build Memory Allocation Hob
  DEBUG((EFI_D_INFO, "\nConfigure MMU In \n"));
  InitMmu(MemoryDescriptor);

  MPSTAGE (0x55, 0);   /* InitMmu returned */

  DEBUG((EFI_D_INFO, "Configure MMU Out \n"));

  if (FeaturePcdGet(PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob();
  }

  return EFI_SUCCESS;
}
