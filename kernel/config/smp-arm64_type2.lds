PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(7);
}
SECTIONS
{
	. = 0xffffffffff800000; /* KERNEL_START */
	_head = .;

	.text : {
		*(.text);
	} : text

	. = ALIGN(0x1000);
	.data : {
		*(.data)
		*(.data.*)
		. = ALIGN(8);
		__start___verbose = .;
		*(__verbose);
		__stop___verbose = .;
	} :data
	.rodata : {
		*(.rodata .rodata.*)
	} :data

	.vdso : ALIGN(0x1000) {
		vdso_page = .;

		. = vdso_page + 0x0000;
		*(.vdso.data)

		. = vdso_page + 0x1000;
		*(.vdso.text)

		. = ALIGN(0x1000);
	} : data = 0xf4

	.bss : {
		*(.bss .bss.*)
	}
	. = ALIGN(0x1000);
	idmap_page_table = .;
	. += 0x1000;	/* PAGE_SIZE */
	swapper_page_table = .;
	. += 0x1000;	/* PAGE_SIZE */
	idmap_pg_dir = .;
	. += 0x3000;	/* IDMAP_DIR_SIZE */
	swapper_pg_dir = .;
	. += 0x3000;	/* SWAPPER_DIR_SIZE */

	_end = .;
}
