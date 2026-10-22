/** @file
  Minimal package-level identity, not an MP/PSCI discovery mechanism.
  No fabricated cache/DRAM records or Qualcomm frequencies/part numbers.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Base.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SOCSmbiosInfoLib.h>

VOID RegisterSOCSmbiosInfo (
  SMBIOS_LOG_SMBIOS_DATA LogSmbiosData,
  EFI_SMBIOS_HANDLE Type16
  )
{
  SMBIOS_TABLE_TYPE4 Cpu;
  CHAR8 *Strings[] = { "SoC", "Google", "Tensor G2 (GS201)", NULL };
  (VOID)Type16;
  ZeroMem (&Cpu, sizeof (Cpu));
  Cpu.Hdr.Type = EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION;
  Cpu.Hdr.Length = sizeof (Cpu);
  Cpu.Socket = 1;
  Cpu.ProcessorType = CentralProcessor;
  Cpu.ProcessorFamily = ProcessorFamilyIndicatorFamily2;
  Cpu.ProcessorManufacturer = 2;
  Cpu.ProcessorVersion = 3;
  Cpu.Status = 0x41; // Package populated, processor enabled; not proof of CPU_ON.
  Cpu.ProcessorUpgrade = ProcessorUpgradeNone;
  Cpu.L1CacheHandle = 0xFFFF;
  Cpu.L2CacheHandle = 0xFFFF;
  Cpu.L3CacheHandle = 0xFFFF;
  // Physical package counts from GS201 topology. Enabled count deliberately
  // unknown (0): secondary CPU bring-up has not been established by this port.
  Cpu.CoreCount = 8;
  Cpu.ThreadCount = 8;
  Cpu.ProcessorCharacteristics = BIT2 | BIT3; // 64-bit, multicore
  Cpu.ProcessorFamily2 = ProcessorFamilyARMv8;
  // Frequencies, IDs, cache sizes, memory device characteristics remain unknown.
  LogSmbiosData ((EFI_SMBIOS_TABLE_HEADER *)&Cpu, Strings, NULL);
}
