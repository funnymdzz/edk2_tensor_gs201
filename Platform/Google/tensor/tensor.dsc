## @file
#
#  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.
#  Copyright (c) 2018 - 2019, Bingxing Wang. All rights reserved.
#  Copyright (c) 2022, Xilin Wu. All rights reserved.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################

[Defines]
  SOC_PLATFORM            = tensor
  USE_PHYSICAL_TIMER      = TRUE

!include Silicon/Google/GooglePkg/GoogleCommonDsc.inc

[PcdsFixedAtBuild.common]
  #gArmTokenSpaceGuid.PcdSystemMemoryBase|0x8AF000000         # Starting address
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000        # Starting address
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x0FDFA0000         # Limit to 4GB Size here

  gQcomTokenSpaceGuid.PcdCpuVectorBaseAddress|0xfd902000    # CPU Vectors
  # Upstream timer library reads CNTFRQ_EL0.
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|19
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|20
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x10400000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x10440000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|25

  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00000850
!if $(PLATFORM_NAME) == "panther"
  #
  # GS201 layout: 0xfa800000 falls inside the bootloader-reserved
  # 0xfa3f0000-0xfd3effff window, so move the PrePi stack into the free
  # 0x97400000-0x97ffffff System RAM block instead.
  #
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x97e00000      # UEFI Stack
!else
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0xfa800000      # UEFI Stack
!endif
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000      # 256K stack
  #gEmbeddedTokenSpaceGuid.PcdPrePiCpuIoSize|44

  gQcomTokenSpaceGuid.PcdUefiMemPoolBase|0x882000000         # DXE Heap base address
  gQcomTokenSpaceGuid.PcdUefiMemPoolSize|0x2E000000         # UefiMemorySize, DXE heap size
  
  gQcomTokenSpaceGuid.PcdMipiFrameBufferAddress|0xfd90c000

  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|3

  #
  # SimpleInit
  #
!if $(PLATFORM_NAME) == "panther"
  #
  # The FDT pointer is stored here by the very first instruction of PrePi
  # (ModuleEntryPoint.S: "str x0, [x10]"), before VBAR is programmed.
  # 0xe2500000 is inside GS201's "sec_pt" reserved region
  # (0xe0000000-0xe37fffff, no-map, secure page tables), so a non-secure
  # write there faults with an uninitialised vector table -> silent hang.
  # 0xfd900000 is ordinary System RAM on panther.
  #
  gSimpleInitTokenSpaceGuid.PcdDeviceTreeStore|0xfd900000
!else
  gSimpleInitTokenSpaceGuid.PcdDeviceTreeStore|0xe2500000 #random free space
!endif
  gSimpleInitTokenSpaceGuid.PcdLoggerdUseConsole|FALSE

  # Bring-up probe scratch. Must NOT live inside the PStore region
  # (0xfd3ff000-0xfd7fefff): the in-memory serial logger uses that whole
  # block as a ring buffer and would overwrite the probe.
  gQcomTokenSpaceGuid.PcdPmsgDebugBase|0xfd5ff000

[LibraryClasses.common]

  # Ported from SurfaceDuoPkg
  AslUpdateLib|Silicon/Google/GooglePkg/Library/DxeAslUpdateLib/DxeAslUpdateLib.inf
  KeypadDeviceImplLib|Silicon/Google/Tensor/Library/KeypadDeviceImplLib/KeypadDeviceImplLib.inf
  PlatformMemoryMapLib|Silicon/Google/Tensor/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf
  PlatformPeiLib|Silicon/Google/Tensor/Library/PlatformPeiLib/PlatformPeiLib.inf
  PlatformPrePiLib|Silicon/Google/Tensor/Library/PlatformPrePiLib/PlatformPrePiLib.inf
  MsPlatformDevicesLib|Silicon/Google/Tensor/Library/MsPlatformDevicesLib/MsPlatformDevicesLib.inf
  SOCSmbiosInfoLib|Silicon/Google/Tensor/Library/SOCSmbiosInfoLib/SOCSmbiosInfoLib.inf
  ResetSystemLib|Silicon/Google/Tensor/Library/ResetSystemLib/ResetSystemLib.inf

[Components.common]
  #
  # OnePlus 6T A/B Slot Support
  # Op6tSlotDxe and BootSlotDxe have the same goal, do not use them both at the same time in device fdf.
  #
  # GPLDrivers/Drivers/Op6tSlotDxe/Op6tSlotDxe.inf
