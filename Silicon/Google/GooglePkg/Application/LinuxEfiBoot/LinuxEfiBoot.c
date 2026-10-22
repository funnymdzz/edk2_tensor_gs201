/** @file
  Experimental Panther Linux EFI-stub launcher; see README.md for ownership.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/Fdt.h>
#include <Guid/FileInfo.h>
#include <Guid/LinuxEfiInitrdMedia.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/LoadFile2.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

// BaseFdtLib includes this libfdt API but FdtLib.h has no wrapper for it.
// ARM64: libfdt int == INT32 and size_t == UINTN; intentionally NOT EFIAPI.
INT32 fdt_check_full (CONST VOID *Fdt, UINTN BufferSize);

#define MAX_DTB      SIZE_2MB
#define MAX_INITRD   SIZE_512MB
#define MAX_CMDLINE  4096

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} INITRD_PATH;
#pragma pack ()

STATIC INITRD_PATH  mPath = {
  { { MEDIA_DEVICE_PATH, MEDIA_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    LINUX_EFI_INITRD_MEDIA_GUID },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};
STATIC VOID   *mInitrd;
STATIC UINTN  mInitrdSize;
STATIC EFI_LOAD_FILE2_PROTOCOL  mLoadFile;

STATIC
EFI_STATUS
EFIAPI
ReadInitrd (
  IN EFI_LOAD_FILE2_PROTOCOL *This,
  IN EFI_DEVICE_PATH_PROTOCOL *FilePath,
  IN BOOLEAN BootPolicy,
  IN OUT UINTN *BufferSize,
  IN VOID *Buffer OPTIONAL
  )
{
  if ((This != &mLoadFile) || (FilePath == NULL) || (BufferSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  if (BootPolicy) {
    return EFI_UNSUPPORTED;
  }
  // Only the four-byte, entire-end remainder is accepted; no unbounded walk.
  if (!IsDevicePathEnd (FilePath) ||
      (DevicePathNodeLength (FilePath) != END_DEVICE_PATH_LENGTH)) {
    return EFI_NOT_FOUND;
  }
  if ((mInitrd == NULL) || (mInitrdSize == 0)) {
    return EFI_NOT_FOUND;
  }
  if ((Buffer == NULL) || (*BufferSize < mInitrdSize)) {
    *BufferSize = mInitrdSize;
    return EFI_BUFFER_TOO_SMALL;
  }
  CopyMem (Buffer, mInitrd, mInitrdSize);
  *BufferSize = mInitrdSize;
  return EFI_SUCCESS;
}

// Read exactly the reported size, detect truncation/growth, always close file.
STATIC
EFI_STATUS
ReadFile (
  IN EFI_FILE_PROTOCOL *Root,
  IN CHAR16 *Name,
  IN UINTN Maximum,
  IN EFI_MEMORY_TYPE Type,
  OUT VOID **Data,
  OUT UINTN *Size
  )
{
  EFI_FILE_PROTOCOL *File;
  EFI_STATUS Status;
  EFI_STATUS CloseStatus;
  struct { EFI_FILE_INFO Info; CHAR16 ExtraName[256]; } Info;
  UINTN InfoSize;
  UINTN Done;
  UINTN Count;
  UINT8 Extra;

  *Data = NULL;
  *Size = 0;
  Status = Root->Open (Root, &File, Name, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  InfoSize = sizeof (Info);
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, &Info);
  if (!EFI_ERROR (Status)) {
    if ((InfoSize < SIZE_OF_EFI_FILE_INFO) ||
        ((Info.Info.Attribute & EFI_FILE_DIRECTORY) != 0) ||
        (Info.Info.FileSize == 0) || (Info.Info.FileSize > Maximum)) {
      Status = EFI_BAD_BUFFER_SIZE;
    } else {
      *Size = (UINTN)Info.Info.FileSize;
      Status = gBS->AllocatePool (Type, *Size, Data);
    }
  }
  Done = 0;
  while (!EFI_ERROR (Status) && (Done < *Size)) {
    Count = *Size - Done;
    Status = File->Read (File, &Count, (UINT8 *)*Data + Done);
    if (!EFI_ERROR (Status) && ((Count == 0) || (Count > *Size - Done))) {
      Status = EFI_DEVICE_ERROR;
    }
    if (!EFI_ERROR (Status)) {
      Done += Count;
    }
  }
  if (!EFI_ERROR (Status)) {
    Count = 1;
    Status = File->Read (File, &Count, &Extra);
    if (!EFI_ERROR (Status) && (Count != 0)) {
      Status = EFI_MEDIA_CHANGED;
    }
  }
  CloseStatus = File->Close (File);
  if (!EFI_ERROR (Status)) {
    Status = CloseStatus;
  }
  if (EFI_ERROR (Status) && (*Data != NULL)) {
    FreePool (*Data);
    *Data = NULL;
  }
  return Status;
}

STATIC
BOOLEAN
PantherDtb (IN VOID *Dtb, IN UINTN Size)
{
  CONST CHAR8 *Compatible;
  INT32 Length;
  INT32 Start;
  INT32 Index;
  BOOLEAN Found;

  if ((Size < sizeof (FDT_HEADER)) || (fdt_check_full (Dtb, Size) != 0)) {
    return FALSE;
  }
  Compatible = FdtGetProp (Dtb, 0, "compatible", &Length);
  if ((Compatible == NULL) || (Length <= 0) || (Compatible[Length - 1] != 0)) {
    return FALSE;
  }
  Found = FALSE;
  Start = 0;
  for (Index = 0; Index < Length; Index++) {
    if (Compatible[Index] == 0) {
      if (((UINTN)(Index - Start) == sizeof ("google,panther") - 1) &&
          (CompareMem (Compatible + Start, "google,panther", Index - Start) == 0)) {
        Found = TRUE;
      }
      Start = Index + 1;
    }
  }
  return Found;
}

// Returning would unload our protocol callback. Retain EVERYTHING until reset.
STATIC
VOID
Retain (IN CHAR16 *Reason, IN EFI_STATUS Status)
{
  Print (L"LinuxEfiBoot: %s: %r; resources retained, reset required.\n", Reason, Status);
  for (;;) {
    gBS->Stall (1000000);
  }
}

EFI_STATUS
EFIAPI
LinuxEfiBootMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_LOADED_IMAGE_PROTOCOL *Self;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL *Root;
  EFI_DEVICE_PATH_PROTOCOL *KernelPath;
  EFI_DEVICE_PATH_PROTOCOL *Remaining;
  EFI_HANDLE Provider;
  EFI_HANDLE Other;
  EFI_HANDLE Child;
  EFI_STATUS Status;
  EFI_STATUS Cleanup;
  VOID *Dtb;
  VOID *PreviousDtb;
  VOID *Text;
  CHAR16 *Options;
  UINTN DtbSize;
  UINTN TextSize;
  UINTN Index;
  BOOLEAN Published;

  Root = NULL;
  KernelPath = NULL;
  Provider = NULL;
  Child = NULL;
  Dtb = NULL;
  Text = NULL;
  Options = NULL;
  PreviousDtb = NULL;
  Published = FALSE;
  mLoadFile.LoadFile = ReadInitrd;
  CopyGuid (&mPath.Vendor.Guid, &gLinuxEfiInitrdMediaGuid);
  Remaining = (EFI_DEVICE_PATH_PROTOCOL *)&mPath;
  Status = gBS->LocateDevicePath (&gEfiLoadFile2ProtocolGuid, &Remaining, &Other);
  if (!EFI_ERROR (Status)) {
    return EFI_ALREADY_STARTED;
  }
  if (Status != EFI_NOT_FOUND) {
    return Status;
  }
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Self);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = gBS->HandleProtocol (Self->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = ReadFile (Root, L"\\linux\\panther.dtb", MAX_DTB, EfiACPIReclaimMemory, &Dtb, &DtbSize);
  if (EFI_ERROR (Status)) { goto Done; }
  if (!PantherDtb (Dtb, DtbSize)) { Status = EFI_COMPROMISED_DATA; goto Done; }
  Status = ReadFile (Root, L"\\linux\\initrd", MAX_INITRD, EfiLoaderData, &mInitrd, &mInitrdSize);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = ReadFile (Root, L"\\linux\\cmdline.txt", MAX_CMDLINE, EfiLoaderData, &Text, &TextSize);
  if (EFI_ERROR (Status)) { goto Done; }
  // Reject NUL/non-ASCII anywhere, even in a suffix that would be stripped.
  for (Index = 0; Index < TextSize; Index++) {
    if ((((UINT8 *)Text)[Index] == 0) || (((UINT8 *)Text)[Index] > 0x7f)) {
      Status = EFI_COMPROMISED_DATA; goto Done;
    }
  }
  while ((TextSize > 0) && ((((UINT8 *)Text)[TextSize - 1] == '\n') ||
                            (((UINT8 *)Text)[TextSize - 1] == '\r'))) {
    TextSize--;
  }
  Options = AllocateZeroPool ((TextSize + 1) * sizeof (CHAR16));
  if (Options == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  for (Index = 0; Index < TextSize; Index++) {
    if ((((UINT8 *)Text)[Index] < 0x20) || (((UINT8 *)Text)[Index] == 0x7f)) {
      Status = EFI_COMPROMISED_DATA; goto Done;
    }
    Options[Index] = ((UINT8 *)Text)[Index];
  }
  KernelPath = FileDevicePath (Self->DeviceHandle, L"\\linux\\Image.efi");
  if (KernelPath == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  Status = gBS->LoadImage (FALSE, ImageHandle, KernelPath, NULL, 0, &Child);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = gBS->HandleProtocol (Child, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Loaded->ImageCodeType != EfiLoaderCode) { Status = EFI_UNSUPPORTED; goto Done; }
  Loaded->LoadOptions = Options;
  Loaded->LoadOptionsSize = (UINT32)((TextSize + 1) * sizeof (CHAR16));
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Provider, &gEfiDevicePathProtocolGuid, &mPath,
                  &gEfiLoadFile2ProtocolGuid, &mLoadFile, NULL);
  if (EFI_ERROR (Status)) { goto Done; }
  // Save the exact prior pointer, including the distinction of no table.
  for (Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
    if (CompareGuid (&SystemTable->ConfigurationTable[Index].VendorGuid, &gFdtTableGuid)) {
      PreviousDtb = SystemTable->ConfigurationTable[Index].VendorTable;
      break;
    }
  }
  Status = gBS->InstallConfigurationTable (&gFdtTableGuid, Dtb);
  if (EFI_ERROR (Status)) { goto Done; }
  Published = TRUE;
  Status = gBS->StartImage (Child, NULL, NULL);

Done:
  if (Published) {
    Cleanup = gBS->InstallConfigurationTable (&gFdtTableGuid, PreviousDtb);
    if (EFI_ERROR (Cleanup)) { Retain (L"DTB restoration failed", Cleanup); }
  }
  if (Provider != NULL) {
    Cleanup = gBS->UninstallMultipleProtocolInterfaces (
                    Provider, &gEfiDevicePathProtocolGuid, &mPath,
                    &gEfiLoadFile2ProtocolGuid, &mLoadFile, NULL);
    if (EFI_ERROR (Cleanup)) { Retain (L"initrd uninstall failed", Cleanup); }
  }
  if (Child != NULL) {
    // EFI applications normally auto-unload on return from StartImage.
    Cleanup = gBS->HandleProtocol (Child, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
    if (!EFI_ERROR (Cleanup)) {
      Cleanup = gBS->UnloadImage (Child);
      if (EFI_ERROR (Cleanup)) { Retain (L"child unload failed", Cleanup); }
    } else if ((Cleanup != EFI_INVALID_PARAMETER) && (Cleanup != EFI_UNSUPPORTED)) {
      Retain (L"child lifetime unknown", Cleanup);
    }
  }
  if (Root != NULL) {
    Cleanup = Root->Close (Root);
    if (!EFI_ERROR (Status)) { Status = Cleanup; }
  }
  if (Options != NULL) { FreePool (Options); }
  if (Text != NULL) { FreePool (Text); }
  if (Dtb != NULL) { FreePool (Dtb); }
  if (mInitrd != NULL) { FreePool (mInitrd); mInitrd = NULL; mInitrdSize = 0; }
  if (KernelPath != NULL) { FreePool (KernelPath); }
  Print (L"LinuxEfiBoot: child returned or launch failed: %r\n", Status);
  return Status;
}
