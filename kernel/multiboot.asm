; The multiboot header, must be the first file linked into the binary
[BITS 32]
[SECTION .mbhdr]
[EXTERN _loadStart]
[EXTERN _loadEnd]
[EXTERN _bssEnd]
[EXTERN EntryPoint]
[GLOBAL MbHdr]

ALIGN 8
MbHdr:
	dd 0xE85250D6 ; magic
	dd 0 ; architecture
	dd HdrEnd - MbHdr ; length
	dd -(0xE85250D6 + 0 + (HdrEnd - MbHdr)) ; checksum

	; tags
	; entry point override
	dw 3, 0 ; multiboot_header_tag_entry_address
	dd 12
	dd EntryPoint

	dd 0 ; align next tag to 8 byte boundry

	; request some information from GRUB for the kernel
	dw 1, 0 ; multiboot_header_tag_information_request
	dd 16
	dd 6 ; request multiboot_tag_type_mmap
	dd 8 ; request MULTIBOOT_TAG_TYPE_FRAMEBUFFER

	; request a video mode
	dw 5, 0 ; MULTIBOOT_HEADER_TAG_FRAMEBUFFER
	dd 20
	dd 0 ; width
	dd 0 ; height
	dd 0 ; depth

	dd 0 ; align next tag to 8 byte boundry

	; end of tags
	dw 0, 0 ; MULTIBOOT_TAG_TYPE_END
	dd 8

	; hdr end mark

HdrEnd:
