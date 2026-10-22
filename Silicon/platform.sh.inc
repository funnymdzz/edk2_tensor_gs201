function platform_build_kernel(){
	cat \
		"${ROOTDIR}/tools/BootShim/BootShim.bin" \
		"${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd" \
		> "${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd-bootshim" \
		||return "$?"

	# Pixel bootloaders differ in which LZ4 container they accept:
	#   BOOTIMG_LZ4_LEGACY=1 -> legacy frame (magic 0x184C2102), what Pixel 7
	#                           stock boot.img uses and what its ABL decompresses
	#   default              -> modern LZ4 frame (magic 0x184D2204)
	# Using the wrong one makes fastboot boot return straight to the bootloader
	# without ever loading the payload.
	local _lz4_args=(-c)
	if [ "${BOOTIMG_LZ4_LEGACY:-0}" = "1" ]
	then
		_lz4_args=(-l -c)
	fi

	lz4 "${_lz4_args[@]}" \
		< "${WORKSPACE}/Build/${DEVICE}/${_MODE}_${TOOLCHAIN}/FV/${SOC_PLATFORM}_UEFI.fd-bootshim" \
		> "${WORKSPACE}/uefi-${DEVICE}.img.gz" \
		||return "$?"

	# A modern LZ4 frame carries an end mark, so appending the DTB after it is
	# harmless (frame decoders stop at the end mark and ignore trailing bytes).
	# The legacy container has no end marker: its decoder keeps consuming
	# [4-byte size][block] records until the input runs out, so trailing bytes
	# are parsed as a bogus block and abort the decompression. Stock Pixel 7
	# boot_a.img confirms the kernel field is exactly the bare legacy stream
	# (block walk ends at offset == kernel_size, 0 trailing bytes).
	if [ "${BOOTIMG_LZ4_LEGACY:-0}" = "1" ]
	then
		cp "${WORKSPACE}/uefi-${DEVICE}.img.gz" "${WORKSPACE}/uefi-${DEVICE}-kernel"
	else
		cat \
			"${WORKSPACE}/uefi-${DEVICE}.img.gz" \
			"${ROOTDIR}/Platform/${VENDOR_NAME}/${SOC_PLATFORM_L}/FdtBlob_compat/${PLATFORM_NAME}.dtb" \
			> "${WORKSPACE}/uefi-${DEVICE}.img.gz-dtb" \
			||return "$?"
		cp "${WORKSPACE}/uefi-${DEVICE}.img.gz-dtb" "${WORKSPACE}/uefi-${DEVICE}-kernel"
	fi
}

function platform_build_bootimg(){
	# BOOTIMG_NO_RAMDISK=1 produces a v4 boot image with ramdisk_size 0,
	# matching the stock Pixel 7 boot.img layout. Passing an empty file is not
	# the same thing: the header would still claim a 1-byte ramdisk.
	local _ramdisk_args=()
	if [ "${BOOTIMG_NO_RAMDISK:-0}" != "1" ]
	then
		_ramdisk_args=(--ramdisk ramdisk)
	fi

	# When a stock template image is provided, use magiskboot: it preserves the
	# exact vendor header layout (AVB tags, os version, page size) that the
	# target bootloader expects, and recompresses with the same format it
	# detected. mkbootimg.py rebuilds headers from scratch and is only a
	# fallback when no template is available.
	local _magiskboot="${MAGISKBOOT:-${ROOTDIR}/tools/magiskboot/magiskboot}"
	local _template="${BOOTIMG_TEMPLATE:-}"
	if [ -x "${_magiskboot}" ] && [ -f "${_template}" ]
	then
		"${ROOTDIR}/tools/pack-panther-magiskboot.sh" \
			"${_template}" \
			"${WORKSPACE}/uefi-${DEVICE}-kernel" \
			"${OUTDIR}/boot-${DEVICE}${EXT}.img" \
			||return "$?"
		return 0
	fi

	python3 "${ROOTDIR}/tools/mkbootimg.py" \
		--kernel "${WORKSPACE}/uefi-${DEVICE}-kernel" \
		"${_ramdisk_args[@]}" \
		--kernel_offset 0x00000000 \
		--ramdisk_offset 0x00000000 \
		--tags_offset 0x00000000 \
		--os_version "${BOOTIMG_OS_VERSION}" \
		--os_patch_level "${BOOTIMG_OS_PATCH_LEVEL}" \
		--header_version 4 \
		-o "${OUTDIR}/boot-${DEVICE}${EXT}.img" \
		||return "$?"
}
