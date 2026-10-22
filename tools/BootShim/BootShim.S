_Head:
	/* Set _Entry address */
	adr	x1, _Payload

	/* Jump to the real code */
	b	_Start

	/* Fake Linux kernel header */

_StackBase0: //has to be 0 on some devices
	/* Text Offset */
	.quad 0

_StackSize0:
	/* Image Size */
	.quad 0

	/* Flags */
	.quad 0

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* Reserved */
	.quad 0

	/* ARM64 Magic */
	.ascii "ARM\x64"

	/* Reserved */
	.long 0

	.ascii "EDK2-MSM"

_StackBase:
	/* Text Offset */
	.quad UEFI_BASE

_StackSize:
	/* Image Size */
	.quad UEFI_SIZE

_Start:
	/* Linux arm64 boot contract: MMU and D-cache must be off before
	 * relocating the payload. Never disable a dirty cache here: doing so
	 * would lose loader data. Reject a nonconforming entry instead.
	 * x0 (DTB) and x1 (payload) stay intact until relocation. */
	mrs	x7, CurrentEL
	cmp	x7, #0x8
	b.eq	_CheckEl2
	cmp	x7, #0x4
	b.ne	_BadEntry
	mrs	x7, sctlr_el1
	b	_CheckCaches
_CheckEl2:
	mrs	x7, sctlr_el2
_CheckCaches:
	tst	x7, #1                 /* MMU on: physical MMIO not safe */
	b.ne	_BadEntry
	tst	x7, #4                 /* D-cache on: no uncached copy */
	b.ne	_ResetBadCacheEntry
	mov	x4, x1
	ldr	x5, _StackBase
	cmp	x4, x5
	beq	_Entry
	ldr	x6,	_StackSize

_CopyLoop:
	ldp	x2, x3, [x4], #16
	stp	x2, x3, [x5], #16
	subs	x6, x6, #16
	b.ne	_CopyLoop
	ldr	x5, _StackBase

_Entry:
	/* I-cache may be enabled by the loader; discard stale instructions
	 * after the uncached relocation before executing its destination. */
	dsb	sy
	ic	iallu
	dsb	sy
	isb
	br	x5

_ResetBadCacheEntry:
	/* MMU is confirmed off. Arm a bounded hardware reset instead of
	 * copying firmware through potentially dirty cache lines. */
#if PANTHER_BOOT_CONTRACT
	movz	x8, #0x1006, lsl #16
	movz	w9, #0xffff
	str	w9, [x8, #4]
	str	w9, [x8, #8]
	movz	w9, #0xff39
	str	w9, [x8]
	dsb	sy
#endif
_BadEntry:
	/* An unexpected EL or enabled MMU has no safe physical MMIO path.
	 * Refuse entry; do not guess translation tables or discard caches. */
	wfe
	b	_BadEntry

_Dead:
	/* We should never get here */
	b	_Dead

.text
.align 4

_Payload:
	/* Your code will get ran right after this binary */
