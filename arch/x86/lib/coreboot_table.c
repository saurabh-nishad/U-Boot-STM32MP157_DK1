// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016, Bin Meng <bmeng.cn@gmail.com>
 */

#include <common.h>
#include <malloc.h>
#include <net.h>
#include <vbe.h>
#include <acpi/acpi_s3.h>
#include <asm/coreboot_tables.h>
#include <asm/e820.h>

DECLARE_GLOBAL_DATA_PTR;

int high_table_reserve(void)
{
	/* adjust stack pointer to reserve space for configuration tables */
	gd->arch.high_table_limit = gd->start_addr_sp;
	gd->start_addr_sp -= CONFIG_HIGH_TABLE_SIZE;
	gd->arch.high_table_ptr = gd->start_addr_sp;

	/* clear the memory */
	if (IS_ENABLED(CONFIG_HAVE_ACPI_RESUME) &&
	    gd->arch.prev_sleep_state != ACPI_S3) {
		memset((void *)gd->arch.high_table_ptr, 0,
		       CONFIG_HIGH_TABLE_SIZE);
	}

	gd->start_addr_sp &= ~0xf;

	return 0;
}

void *high_table_malloc(size_t bytes)
{
	u32 new_ptr;
	void *ptr;

	new_ptr = gd->arch.high_table_ptr + bytes;
	if (new_ptr >= gd->arch.high_table_limit)
		return NULL;
	ptr = (void *)gd->arch.high_table_ptr;
	gd->arch.high_table_ptr = new_ptr;

	return ptr;
}

/**
 * cb_table_init() - initialize a coreboot table header
 *
 * This fills in the coreboot table header signature and the header bytes.
 * Other fields are set to zero.
 *
 * @cbh:	coreboot table header address
 */
static void cb_table_init(struct cb_header *cbh)
{
	memset(cbh, 0, sizeof(struct cb_header));
	memcpy(cbh->signature, "LBIO", 4);
	cbh->header_bytes = sizeof(struct cb_header);
}

/**
 * cb_table_add_entry() - add a coreboot table entry
 *
 * This increases the coreboot table entry size with added table entry length
 * and increases entry count by 1.
 *
 * @cbh:	coreboot table header address
 * @cbr:	to be added table entry address
 * @return:	pointer to next table entry address
 */
static u32 cb_table_add_entry(struct cb_header *cbh, struct cb_record *cbr)
{
	cbh->table_bytes += cbr->size;
	cbh->table_entries++;

	return (u32)cbr + cbr->size;
}

/**
 * cb_table_finalize() - finalize the coreboot table
 *
 * This calculates the checksum for all coreboot table entries as well as
 * the checksum for the coreboot header itself.
 *
 * @cbh:	coreboot table header address
 */
static void cb_table_finalize(struct cb_header *cbh)
{
	struct cb_record *cbr = (struct cb_record *)(cbh + 1);

	cbh->table_checksum = compute_ip_checksum(cbr, cbh->table_bytes);
	cbh->header_checksum = compute_ip_checksum(cbh, cbh->header_bytes);
}

void write_coreboot_table(u32 addr, struct memory_area *cfg_tables)
{
	struct cb_header *cbh = (struct cb_header *)addr;
	struct cb_record *cbr;
	struct cb_memory *mem;
	struct cb_memory_range *map;
	struct e820_entry e820[32];
	struct cb_framebuffer *fb;
	struct vesa_mode_info *vesa;
	int i, num;

	cb_table_init(cbh);
	cbr = (struct cb_record *)(cbh + 1);

	/*
	 * Two type of coreboot table entries are generated by us.
	 * They are 'struct cb_memory' and 'struct cb_framebuffer'.
	 */

	/* populate memory map table */
	mem = (struct cb_memory *)cbr;
	mem->tag = CB_TAG_MEMORY;
	map = mem->map;

	/* first install e820 defined memory maps */
	num = install_e820_map(ARRAY_SIZE(e820), e820);
	for (i = 0; i < num; i++) {
		map->start.lo = e820[i].addr & 0xffffffff;
		map->start.hi = e820[i].addr >> 32;
		map->size.lo = e820[i].size & 0xffffffff;
		map->size.hi = e820[i].size >> 32;
		map->type = e820[i].type;
		map++;
	}

	/* then install all configuration tables */
	while (cfg_tables->size) {
		map->start.lo = cfg_tables->start & 0xffffffff;
		map->start.hi = cfg_tables->start >> 32;
		map->size.lo = cfg_tables->size & 0xffffffff;
		map->size.hi = cfg_tables->size >> 32;
		map->type = CB_MEM_TABLE;
		map++;
		num++;
		cfg_tables++;
	}
	mem->size = num * sizeof(struct cb_memory_range) +
		    sizeof(struct cb_record);
	cbr = (struct cb_record *)cb_table_add_entry(cbh, cbr);

	/* populate framebuffer table if we have sane vesa info */
	vesa = &mode_info.vesa;
	if (vesa->x_resolution && vesa->y_resolution) {
		fb = (struct cb_framebuffer *)cbr;
		fb->tag = CB_TAG_FRAMEBUFFER;
		fb->size = sizeof(struct cb_framebuffer);

		fb->x_resolution = vesa->x_resolution;
		fb->y_resolution = vesa->y_resolution;
		fb->bits_per_pixel = vesa->bits_per_pixel;
		fb->bytes_per_line = vesa->bytes_per_scanline;
		fb->physical_address = vesa->phys_base_ptr;
		fb->red_mask_size = vesa->red_mask_size;
		fb->red_mask_pos = vesa->red_mask_pos;
		fb->green_mask_size = vesa->green_mask_size;
		fb->green_mask_pos = vesa->green_mask_pos;
		fb->blue_mask_size = vesa->blue_mask_size;
		fb->blue_mask_pos = vesa->blue_mask_pos;
		fb->reserved_mask_size = vesa->reserved_mask_size;
		fb->reserved_mask_pos = vesa->reserved_mask_pos;

		cbr = (struct cb_record *)cb_table_add_entry(cbh, cbr);
	}

	cb_table_finalize(cbh);
}
