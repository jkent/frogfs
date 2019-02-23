OUTPUT_FORMAT("elf32-xtensa-le")


SECTIONS
{
	.rodata : ALIGN(4) SUBALIGN(4) {
		image_espfs_start = .;
		*(*)
		image_espfs_end = .;
		image_espfs_size = image_espfs_end - image_espfs_start;
	}
}
