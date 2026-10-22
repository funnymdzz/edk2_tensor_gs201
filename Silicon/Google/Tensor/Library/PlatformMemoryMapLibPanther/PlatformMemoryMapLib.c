#include <Library/PlatformMemoryMapLib.h>

STATIC ARM_MEMORY_REGION_DESCRIPTOR_EX gDeviceMemoryDescriptorEx[] = {
    /*
     * Panther (Pixel 7 / GS201) memory map.
     *
     * Deliberately minimal, mirroring the raven map that is known to boot:
     * only the regions UEFI itself needs are mapped. Everything else is handed
     * to DXE through the memory HOBs.
     *
     * Rationale for the rewrite: the previous 24-entry map caused CpuDxe's
     * CpuSetMemoryAttributes() to keep re-programming page tables. It compares
     * the requested attribute against the attribute of the region containing
     * the address, and calls ArmSetMemoryAttributes() again whenever they
     * differ. With many small RAM regions using different cache attributes
     * (WRITE_BACK vs WRITE_BACK_XN vs WRITE_THROUGH_XN) that never converges:
     * every 4KB page ends up needing a fresh L0->L1->L2->L3 chain, the page
     * allocator is exhausted, and the board hangs until the watchdog fires
     * (observed: RST_STAT 0x1 CLUSTER0_NONCPU_WDTRESET).
     *
     * Keep every normal-memory region on the SAME attribute (WRITE_BACK) so
     * the comparison succeeds and the loop terminates.
     */

    /* Device memory: a single flat window, as in the raven map. */
    {"Peripherals",       0x00000000, 0x20000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

    /* UEFI firmware volume. BootShim copies the payload here (FD_BASE). */
    {"UEFI FD",           0x880000000, 0x00800000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},

    /* DXE heap. 736MB, same offset as raven. */
    {"DXE Heap",          0x882000000, 0x2E000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},

    /* PrePi stack, clear of every panther carveout. */
    {"UEFI Stack",        0x97e00000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},

    /* Exception vectors (PcdCpuVectorBaseAddress). */
    {"CPU Vectors",       0xfd902000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},

    /* FDT pointer storage (PcdDeviceTreeStore). */
    {"FDT Store",         0xfd900000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},

    /* ramoops. 4KB is enough for the in-memory serial ring: mem_putchar()
       invalidates the whole region on every character, so a large region
       makes logging enormously expensive. */
    {"PStore",            0xfd3ff000, 0x00001000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},

    /* Bring-up probe records, in the pmsg half of ramoops so Android can read
       them as /sys/fs/pstore/pmsg-ramoops-0. */
    {"Pmsg Probe",        0xfd5ff000, 0x00001000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},

    /* Bootloader log, preserved for post-mortem. */
    {"Bootloader Log",    0xfd800000, 0x00100000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},

    /* Bootloader framebuffer scanned out by DECON0 window 5 / DPP0.
       p53 proved DPP0's DMA base 0xfac00000 is CPU-physical: a CPU-written
       pattern appeared on the panel. 1080x2400x4 = 0x9e3400 bytes, plus the
       shared FB-console cursor right after it; 2MB aligned, 10MB. Kept
       WRITE_BACK like every other region (see above); FB writers clean the
       cache explicitly before DECON fetches. */
    {"Display Reserved",  0xfac00000, 0x00a00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},

    /*
     * Conventional RAM. One contiguous low window and one high window, both
     * WRITE_BACK like everything else. These are the only regions DXE will
     * allocate from, and they avoid every documented panther carveout:
     *   gsa 0x90200000, tpu 0x91e00000/0x93000000, aoc 0x94000000,
     *   gxp 0x98000000, pkvm 0xb5000000, sec_dram 0xb7200000,
     *   sec_pt 0xe0000000, cp_rmem 0xe8000000, abl 0xf8800000,
     *   48MB hole 0xfa3f0000, ramoops 0xfd3ff000, bldr log 0xfd800000
     */
    {"RAM Low",           0x80000000, 0x10200000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
    {"RAM Mid",           0x98500000, 0x1cb00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},

    /* High DRAM window: 256MB above UEFI FD + DXE heap. Kept modest so page
       table construction stays cheap; grow once UEFI boots reliably. */
    {"RAM High",          0x8b0000000, 0x10000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,  WRITE_BACK},

    /* GIC. */
    {"APSS_GIC500_GICD",  0x10400000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
    {"APSS_GIC500_GICR",  0x10440000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},

    {"Terminator", 0, 0, 0, 0, 0, 0, 0}};

PARM_MEMORY_REGION_DESCRIPTOR_EX GetPlatformMemoryMap()
{
  return gDeviceMemoryDescriptorEx;
}
