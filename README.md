# EDK2 Tensor GS201 / Panther

EDK2-based firmware work for Google Tensor GS201 (Pixel 7 / `panther`).
The project is experimental. It is not an official Google firmware and has not
been validated for every operating system or hardware function.

## Build

### Requirements

- Linux host
- Git and Git submodules
- Python 3
- GNU AArch64 cross compiler (`aarch64-linux-gnu-`)
- `iasl` and `lz4`
- A compatible Android boot-image packaging tool for the target device

Do not commit local build output, device logs, firmware images, credentials, or
private machine paths to this repository.

### Clone

```sh
git clone https://github.com/funnymdzz/edk2_tensor_gs201.git
cd edk2_tensor_gs201
git submodule update --init --recursive
```

### Configure tools

Add the repository's tool directory to `PATH` when using the bundled IASL
runtime. Set `CROSS_COMPILE` if the compiler prefix is not on `PATH`.

```sh
export PATH="$PWD/tools/iasl-root/usr/bin:$PATH"
export CROSS_COMPILE="aarch64-linux-gnu-"
```

The exact IASL installation is host-specific; do not hard-code an absolute path
in scripts or documentation.

### Build Panther

```sh
./build.sh --device panther --toolchain GCC --outputdir "$PWD/out"
```

For persistent early diagnostics, use the pstore build option:

```sh
./build.sh --device panther --toolchain GCC --pstore --outputdir "$PWD/out-pstore"
```

The build produces a UEFI payload and, when the platform hook is available, a
boot image under the selected output directory. Always inspect the build log
and verify the resulting artifact hash before testing.

### Safe testing

Only test with the bootloader's temporary boot mechanism for your device. Do
not flash partitions, erase storage, alter partition tables, disable Android
encryption, or use commands that reset the device. Keep an unmodified stock
boot image as the recovery template.

A successful build does not prove hardware safety. Verify display, input,
watchdog recovery, and return to the existing operating system independently.

## Platform notes

- The Panther display path currently uses the bootloader-inherited GS201 scanout
  and a verified framebuffer handoff; it is not a complete native display driver.
- Linux and Windows ARM64 startup are separate work items. Existing Qualcomm
  ACPI tables must not be reused as Panther hardware descriptions without
  validation.
- Storage and Android encrypted userdata are intentionally not modified by this
  project.

## License

Code outside `GPLDrivers` follows the BSD 2-Clause Patent license used by the
project. Components under `GPLDrivers` retain their respective GPL licenses.
See individual source files and submodule licenses for details.
