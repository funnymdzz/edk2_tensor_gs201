/** @file
  Read-only inventory for Linux EFI-stub and Windows ARM64 bring-up.
  Does not test ExitBootServices, runtime VA conversion, or OS boot success.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Guid/Fdt.h>
#include <Guid/SmBios.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadFile2.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleNetwork.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

STATIC VOID CountProtocol (CHAR16 *Name, EFI_GUID *Guid) {
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0;
  EFI_STATUS Status = gBS->LocateHandleBuffer (ByProtocol, Guid, NULL, &Count, &Handles);
  Print (L"%-18s %u handles (%r)\n", Name, (UINT32)Count, Status);
  if (Handles != NULL) FreePool (Handles);
}

STATIC EFI_STATUS PrintMemoryMap (VOID) {
  EFI_MEMORY_DESCRIPTOR *Map = NULL, *D;
  UINTN Size = 0, Key, Stride = 0, Capacity, Offset, Attempt;
  UINT32 Version;
  UINT64 RamPages = 0, ReservedPages = 0, RuntimePages = 0;
  EFI_STATUS Status = gBS->GetMemoryMap (&Size, NULL, &Key, &Stride, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL || Stride < sizeof (*Map)) return EFI_DEVICE_ERROR;
  for (Attempt = 0; Attempt < 4; Attempt++) {
    if (Size > MAX_UINTN - 8 * Stride) return EFI_OUT_OF_RESOURCES;
    Capacity = Size + 8 * Stride;
    Map = AllocatePool (Capacity);
    if (Map == NULL) return EFI_OUT_OF_RESOURCES;
    Size = Capacity;
    Status = gBS->GetMemoryMap (&Size, Map, &Key, &Stride, &Version);
    if (Status != EFI_BUFFER_TOO_SMALL) break;
    FreePool (Map);
    Map = NULL;
  }
  if (EFI_ERROR (Status)) {
    if (Map != NULL) FreePool (Map);
    return Status;
  }
  if (Stride < sizeof (*Map) || Size % Stride != 0) {
    FreePool (Map);
    return EFI_COMPROMISED_DATA;
  }
  Print (L"EFI map: %u descriptors, stride %u version %u\n",
         (UINT32)(Size / Stride), (UINT32)Stride, Version);
  Print (L"Type PhysicalStart    Pages            Attributes\n");
  for (Offset = 0; Offset < Size; Offset += Stride) {
    D = (VOID *)((UINT8 *)Map + Offset);
    Print (L"%02u   %016lx %016lx %016lx\n", D->Type,
           D->PhysicalStart, D->NumberOfPages, D->Attribute);
    if ((D->Type == EfiConventionalMemory || D->Type == EfiLoaderCode ||
         D->Type == EfiLoaderData || D->Type == EfiBootServicesCode ||
         D->Type == EfiBootServicesData) && (D->Attribute & EFI_MEMORY_WB))
      RamPages += D->NumberOfPages;
    if (D->Type == EfiReservedMemoryType) ReservedPages += D->NumberOfPages;
    if (D->Attribute & EFI_MEMORY_RUNTIME) RuntimePages += D->NumberOfPages;
  }
  Print (L"WB reclaim candidates=%lu MiB, Reserved=%lu MiB, Runtime=%lu pages\n",
         RamPages / 256, ReservedPages / 256, RuntimePages);
  FreePool (Map);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI UefiMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = NULL;
  UINTN Size = 0;
  VOID *Table;
  EFI_TIME Time;
  (VOID)ImageHandle;
  Print (L"Panther OS foundations inventory (WIP, not an OS certification)\n");
  Print (L"UEFI revision %08x, firmware %s\n", SystemTable->Hdr.Revision,
         SystemTable->FirmwareVendor);
  Status = EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, &Table);
  Print (L"ACPI 2+ RSDP: %r\n", Status);
  Status = EfiGetSystemConfigurationTable (&gEfiAcpiTableGuid, &Table);
  Print (L"ACPI 1 RSDP:  %r\n", Status);
  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Table);
  Print (L"FDT table:    %r (presence is not DT correctness)\n", Status);
  Status = EfiGetSystemConfigurationTable (&gEfiSmbios3TableGuid, &Table);
  Print (L"SMBIOS 3:     %r (legacy records require audit)\n", Status);
  CountProtocol (L"Filesystem", &gEfiSimpleFileSystemProtocolGuid);
  CountProtocol (L"Block IO", &gEfiBlockIoProtocolGuid);
  CountProtocol (L"LoadFile2", &gEfiLoadFile2ProtocolGuid);
  CountProtocol (L"Simple Network", &gEfiSimpleNetworkProtocolGuid);
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  Print (L"GOP: %r\n", Status);
  if (!EFI_ERROR (Status) && Gop->Mode != NULL) {
    Status = Gop->QueryMode (Gop, Gop->Mode->Mode, &Size, &Info);
    if (!EFI_ERROR (Status) && Info != NULL) {
      Print (L"GOP %ux%u stride %u format %u FB %lx/%lu\n",
             Info->HorizontalResolution, Info->VerticalResolution,
             Info->PixelsPerScanLine, Info->PixelFormat,
             Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
      FreePool (Info);
    }
    Info = NULL;
    Status = Gop->QueryMode (Gop, Gop->Mode->MaxMode, &Size, &Info);
    Print (L"GOP invalid QueryMode: %r (expect Invalid Parameter)\n", Status);
    if (Info != NULL) FreePool (Info);
  }
  Status = gRT->GetTime (&Time, NULL);
  Print (L"GetTime: %r", Status);
  if (!EFI_ERROR (Status))
    Print (L" %04u-%02u-%02u %02u:%02u:%02u (wall clock NOT verified)",
           Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second);
  Print (L"\nMemory inventory: %r\n", PrintMemoryMap ());
  Print (L"No disk/variable writes or register probes performed.\n");
  Print (L"No runtime VA/EBS/PSCI/storage test performed. Watchdog stays armed.\n");
  return EFI_SUCCESS;
}
