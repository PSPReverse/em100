/*
 * Copyright (C) 2012 The Chromium OS Authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#include <architecture/byte_order.h>
  #if BYTE_ORDER == LITTLE_ENDIAN
    #define le32toh(x) (x)
    #define le16toh(x) (x)
    #define htobe16(x) bswap_16(x)
  #else
    #define le32toh(x) bswap_32(x)
    #define le16toh(x) bswap_16(x)
    #define htobe16(x) (x)
  #endif
#else
#include <endian.h>
#endif

#define DEDIPROG_CFG_PRO_SIZE 176
#define DEDIPROG_CFG_MAGIC 0x67666344
#define INIT_SEQUENCE_REIGSTER_OFFSET_0 0x2300
#define INIT_SEQUENCE_REIGSTER_OFFSET_1 0x1100

/* Init sequence and file format:
 *
 * All v1.1 dediprog configuration files are 176 bytes and all values are
 * encoded as little endian format.

 * At offset init_offset the init sequence consists of init entries that are
 * sent to endpoint 1.  There are 2 sets of entries separated by a 32-bit
 * terminator of 0xffffffff. Each entry consists of a value and register offset.
 * The first set of entries have a base register address of 0x2300 while the
 * the second set of entries have a base register address of 0x1100. Each entry
 * is sent to the device as <register> <value>, however the data is sent in
 * big endian format. Additionally, each entry is sent as a 16-byte transfer
 * with the remaining bytes all 0's.
 */


struct dediprog_cfg_hdr {
	uint32_t magic;
	uint16_t ver_min;
	uint16_t ver_maj;
	uint32_t init_offset;
	uint32_t chip_size;
	uint32_t vendor_name_offset;
	uint32_t chip_name_offset;
	uint32_t unknown_offset[2];
} __attribute__((packed));

struct dediprog_cfg_pro {
	struct dediprog_cfg_hdr hdr;
	uint8_t payload[DEDIPROG_CFG_PRO_SIZE-sizeof(struct dediprog_cfg_hdr)];
} __attribute__((packed));


struct dediprog_cfg_init_entry {
	uint16_t value;
	uint16_t reg;
} __attribute__((packed));

unsigned char cfg_buffer[DEDIPROG_CFG_PRO_SIZE];

static int parse_and_output_config(struct dediprog_cfg_pro *cfg)
{
	struct dediprog_cfg_init_entry *entry, *end;
	struct dediprog_cfg_hdr *hdr;
	const char *vendor, *chip_name;
	uint16_t reg_offset;

	hdr = &cfg->hdr;

	/* The magic number is actually string, but it can be converted to
	 * a host ordered 32-bit number. */
	hdr->magic= le32toh(hdr->magic);

	if (hdr->magic != DEDIPROG_CFG_MAGIC) {
		fprintf(stderr, "Invalid magic number: 0x%x\n", hdr->magic);
		return -1;
	}

	/* Convert all header values from little endian to host byte order. */
	hdr->ver_min = le16toh(hdr->ver_min);
	hdr->ver_maj = le16toh(hdr->ver_maj);
	hdr->init_offset = le32toh(hdr->init_offset);
	hdr->chip_size = le32toh(hdr->chip_size);
	hdr->vendor_name_offset = le32toh(hdr->vendor_name_offset);
	hdr->chip_name_offset = le32toh(hdr->chip_name_offset);

	if (hdr->ver_maj != 1 && hdr->ver_min != 1) {
		fprintf(stderr, "Invalid version number: %d.%d\n", hdr->ver_maj,
		        hdr->ver_min);
		return -1;
	}

	/* Adjust the offsets to be into the payload of the config file. */
	hdr->init_offset -= sizeof(*hdr);
	hdr->vendor_name_offset -= sizeof(*hdr);
	hdr->chip_name_offset -= sizeof(*hdr);

	vendor = (void *)&cfg->payload[hdr->vendor_name_offset];
	chip_name = (void *)&cfg->payload[hdr->chip_name_offset];


	printf("\t{ /* %s %s (%d kB) */\n",
	       vendor, chip_name, hdr->chip_size/1024);
	printf("\t\t.name = \"%s\",\n", chip_name);
	printf("\t\t.size = 0x%x,\n", hdr->chip_size);
	printf("\t\t.init = {\n");

	entry = (void *)&cfg->payload[hdr->init_offset];
	end = (void *)&cfg[1]; /* 1 past the last entry */

	reg_offset = INIT_SEQUENCE_REIGSTER_OFFSET_0;

	for (; entry != end; entry++) {
		uint8_t *reg, *value;

		/* Convert from little endian to host format. */
		entry->value = le16toh(entry->value);
		entry->reg = le16toh(entry->reg);

		if (entry->value == 0xffff && entry->reg == 0xffff) {
			reg_offset = INIT_SEQUENCE_REIGSTER_OFFSET_1;
			continue;
		}

		entry->reg += reg_offset;

		/* Convert from host to big endian format. */
		entry->value = htobe16(entry->value);
		entry->reg = htobe16(entry->reg);

		value = (void *)&entry->value;
		reg = (void *)&entry->reg;

		printf("\t\t\t{ 0x%02x, 0x%02x, 0x%02x, 0x%02x },\n",
		       reg[0], reg[1], value[0], value[1]);
	}

	printf("\t\t},\n");
	printf("\t},\n");

	return 0;
}

int main(int argc, char *argv[])
{
	int i;
	FILE *f;
	struct dediprog_cfg_pro *cfg;
	const char *filename;

	printf("\n#ifndef EM100PRO_CHIPS_H\n");
	printf("#define EM100PRO_CHIPS_H\n\n");
	printf("#include <stdint.h>\n");
	printf("#define NUM_INIT_ENTRIES 11\n");
	printf("#define BYTES_PER_INIT_ENTRY 4\n");
	printf("typedef struct {\n");
	printf("\tconst char *name;\n");
	printf("\tunsigned int size;\n");
	printf("\tuint8_t init[NUM_INIT_ENTRIES][BYTES_PER_INIT_ENTRY];\n");
	printf("} chipdesc;\n\n");

	printf("const chipdesc chips[] = {\n");

	for (i=1; i<argc; i++) {
		filename = argv[i];
		f = fopen(filename, "r");
		if (!f) {
			perror(filename);
			exit(1);
		}

		if (fread(cfg_buffer, sizeof(cfg_buffer), 1, f) != 1) {
			perror(argv[i]);
			fclose(f);
			exit(1);
		}

		cfg = (typeof(cfg))&cfg_buffer[0];

		if (parse_and_output_config(cfg)) {
			fprintf(stderr, "Error parsing %s\n", filename);
		}


		fclose(f);
	}

	printf("\n\t{ .name = NULL}\n};\n");
	printf("#endif /* EM100PRO_CHIPS_H */\n");
	return 0;
}

