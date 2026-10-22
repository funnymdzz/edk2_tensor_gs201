# LinuxEfiBoot (infrastructure WIP)

ARM64 UEFI application, not a hardware bring-up or boot-success claim. No parent
DSC/FDF/build configuration was changed and no build was run. Host tests, static
analysis, link/build validation and target boot validation remain outstanding.

## Inputs

All four inputs are required, from the root of the Simple File System volume
identified by this application's LoadedImage.DeviceHandle (not a shell CWD or
another discovered volume):

- `\linux\Image.efi`: ARM64 Linux EFI-stub application, loaded by firmware with
  `LoadImage(FALSE, ...)`; firmware image authentication policy still applies.
- `\linux\panther.dtb`: nonempty, at most 2 MiB; bounded `fdt_check_full`, then
  exact root compatible string-list membership `google,panther`. A generic
  `google,gs201` alone is deliberately rejected. No DTB fixups or fabricated
  ACPI tables are supplied. This check identifies supplied data, not hardware.
- `\linux\initrd`: nonempty and at most 512 MiB; completely preloaded before
  publishing a Linux initrd vendor-media DevicePath/LoadFile2 provider.
- `\linux\cmdline.txt`: nonempty input, at most 4096 bytes, no NUL or non-ASCII
  bytes anywhere. Trailing CR/LF bytes are stripped; other control bytes and DEL
  are rejected. Printable internal spaces are preserved. The resulting line
  may be empty. UTF-16 LoadOptions include the terminating NUL in their size.

File metadata is bounded, directories are rejected, short reads are retried,
zero-progress reads and size changes detected at EOF are errors. This is not an
atomic snapshot against concurrent same-size file modification. Open files are
read-only; there are no disk, variable, MMIO or hardware configuration writes.

The callback accepts only its own protocol and an entire-end four-byte path
remainder, rejects BootPolicy, supports NULL/short-buffer size queries without
copying, and copies exactly the preloaded initrd on success. Existing matching
Linux LoadFile2 paths cause refusal rather than duplicate providers. Matching
uses firmware LocateDevicePath, following the upstream initrd shell example.
Concurrent third-party provider installation is outside the single-threaded
launcher's guarantee.

## Handoff and ownership

The supplied DTB is allocated as EfiACPIReclaimMemory, and the initrd as
EfiLoaderData. Both stay owned and live across StartImage. Kernel EFI stub owns
ExitBootServices and cache/architecture handoff; this launcher does neither.
The prior DT configuration-table pointer is saved immediately before replacing
it; an absent prior table is restored by removing our entry. Every returned or
failed StartImage first restores the prior table, then uninstalls the provider,
then unloads any still-present child before releasing its load options and the
other buffers. A returned EFI application is normally already unloaded by
firmware, so its handle is checked before explicit unload.

**Fail-closed cleanup:** if DTB restoration fails, no provider uninstall, child
unload or allocation free is attempted. If provider uninstall fails, its initrd,
callback code and every other owned object stay resident. Child unload or
unexpected handle-query failures likewise retain all allocations. These cases
print an error and remain in the application's Boot Services stall loop until
reset; returning would automatically unload the callback code. Even after a
successful table restoration, retained objects are not selectively freed.
Firmware may already have automatically unloaded a returned EFI application;
this launcher cannot prevent that or resurrect it. It guarantees no *explicit*
child unload after a failed table restoration. This exceptional resident state
is intentionally not a normal return-to-shell path.

## Library/API notes

Use the upstream MdePkg `BaseFdtLib` instance for `FdtLib`. It compiles libfdt's
`fdt_check.c` and therefore exports `fdt_check_full`; the current public FdtLib
header has no full-check wrapper. The C source narrowly declares the underlying
ARM64 C ABI (`int`/`size_t` as INT32/UINTN), while using the public FdtGetProp
wrapper for compatible lookup. Another FdtLib instance must provide that symbol
or linking must fail; header-only `FdtCheckHeader` is not a substitute.

Reviewed source/API precedents: upstream MdePkg LoadFile2 protocol and Linux
initrd GUID header (`gLinuxEfiInitrdMediaGuid`); OvmfPkg
LinuxInitrdDynamicShellCommand provider; EmbeddedPkg AndroidBootImgLib image
launch; MdeModulePkg Core/Dxe/Image automatic application unload. No source
outside this directory was modified by this implementation.
