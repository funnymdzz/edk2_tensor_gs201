[Defines]
  PLATFORM_NAME                  = panther
  PLATFORM_GUID                  = 6e5e894b-a0ee-4b2f-9f21-c0e6414cd873
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Platform/Google/tensor/tensor.fdf
  DEVICE_DXE_FV_COMPONENTS       = Platform/Google/tensor/panther.fdf.inc

!include Platform/Google/tensor/tensor.dsc

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DENABLE_SIMPLE_INIT -DENABLE_LINUX_SIMPLE_MASS_STORAGE -DPANTHER_EFI_SAFE_BOOT_ONLY

[LibraryClasses.common]
  # Truthful package identity; do not expose the shared Snapdragon 845 records.
  SOCSmbiosInfoLib|Silicon/Google/Tensor/Library/SOCSmbiosInfoLibPanther/SOCSmbiosInfoLibPanther.inf
  # Pixel 7 has its own memory map: GS201 carveouts differ from GS101 and the
  # 8GB DRAM layout must be described explicitly. Overrides tensor.dsc.
  PlatformMemoryMapLib|Silicon/Google/Tensor/Library/PlatformMemoryMapLibPanther/PlatformMemoryMapLibPanther.inf

  # Volume keys live on different ALIVE pin controllers than on GS101, and
  # panther has no power-key GPIO (it is behind the main PMIC). Overrides
  # tensor.dsc.
  KeypadDeviceImplLib|Silicon/Google/Tensor/Library/KeypadDeviceImplLibPanther/KeypadDeviceImplLibPanther.inf

[PcdsFixedAtBuild.common]
  # The shared tensor.dsc enables ALL debug levels (0x807B55FF). Combined with
  # InMemorySerialPortLib - whose mem_putchar() invalidates the WHOLE PStore
  # region on every single character - that makes boot enormously slow: each
  # character costs 4MB of cache maintenance, so PrePi overruns the watchdog
  # and the board resets mid-boot, looking like a random hang.
  # Errors only for now; raise this again once UEFI reaches DXE.
  # Errors, warnings and driver loads: on the framebuffer console each line
  # costs one 10MB cache clean, so keep the volume modest.
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000006

  # Arch timer PPIs from the device DT (timer: PPI 13/14/11/10 -> INTID
  # 29/30/27/26). tensor.dsc's 19/20 left the NS physical timer (INTID 30,
  # used via CNTP_* with USE_PHYSICAL_TIMER) unregistered, so DXE's idle
  # WFI (CpuDxe IdleLoopEventCallback) never woke up.
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26



  #
  # Display. Pixel 7 panel is 1080x2400 @ 32bpp.
  #
  # Bootloader framebuffer: DPP0 DMA base 0xfac00000, verified CPU-physical
  # by the p53 pixel test (BGRX: byte0=B, byte1=G, byte2=R). Mapped as
  # "Display Reserved" in PlatformMemoryMapLibPanther.
  gQcomTokenSpaceGuid.PcdMipiFrameBufferAddress|0xFAC00000
  gQcomTokenSpaceGuid.PcdMipiFrameBufferWidth|1080
  gQcomTokenSpaceGuid.PcdMipiFrameBufferHeight|2400

  # DECON main register base: GS201 uses 0x1C240000 (GS101 used 0x1C300000)
  gQcomTokenSpaceGuid.PcdDeconBaseAddress|0x1C240000
  # Diagnostic image only: guarded green window, then restore after 3 seconds.
  gQcomTokenSpaceGuid.PcdPantherColorTest|FALSE
  gQcomTokenSpaceGuid.PcdPantherPixelTest|FALSE
  # Guarded: leave DECON's HW TE trigger unmasked for continuous refresh.
  gQcomTokenSpaceGuid.PcdPantherDisplayEnable|TRUE
  # Cluster-0 WDT armed by PrePi (WTCON 0xFF39); reloaded by ExynosWatchdogDxe.
  gQcomTokenSpaceGuid.PcdWatchdogBase|0x10060000
  # Bring-up: stay in UEFI ~5 minutes (no input devices yet), then let the
  # watchdog return the phone to Android without a long power-key press.
  gQcomTokenSpaceGuid.PcdWatchdogReloadSeconds|300

  gRenegadePkgTokenSpaceGuid.PcdDeviceVendor|"Google"
  gRenegadePkgTokenSpaceGuid.PcdDeviceProduct|"Pixel 7"
  gRenegadePkgTokenSpaceGuid.PcdDeviceCodeName|"panther"

  # Simple Init
  gSimpleInitTokenSpaceGuid.PcdGuiDefaultDPI|420

[Components.common]
  Silicon/Google/GooglePkg/Application/BootProbe/BootProbe.inf
  Silicon/Google/GooglePkg/Application/LinuxEfiBoot/LinuxEfiBoot.inf
  Silicon/Google/GooglePkg/Drivers/ExynosWatchdogDxe/ExynosWatchdogDxe.inf
