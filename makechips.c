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

#define DEDIPROG_CFG_PRO_MAX_ENTRIES 212

#define DEDIPROG_CFG_PRO_SIZE 176
#define DEDIPROG_CFG_PRO_SIZE_SFDP 256
#define DEDIPROG_CFG_PRO_SIZE_SRST 144

#define DEDIPROG_CFG_MAGIC  0x67666344 /* 'Dcfg' */
#define DEDIPROG_SFDP_MAGIC 0x50444653 /* 'SFDP' */
#define DEDIPROG_SRST_MAGIC 0x54535253 /* 'SRST' */
#define DEDIPROG_PROT_MAGIC 0x544f5250 /* 'PROT' */

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
 *
 * Configuration files that are >= 436 bytes contain SFDP data, separated by
 * the magic value 'SFDP' while those that are 584 bytes contain SRST data
 * separated by the magic value 'SRST' and containing 0 or 3 entries followed
 * by PROT data in the rest of the buffer.  Unfortunately there does not seem
 * to be any change in the version fields and these are still reported as 1.1.
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
	int entries = 0;

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
	printf("\t\t.vendor = \"%s\",\n", vendor);
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

		++entries;
	}

	return entries;
}

static int parse_and_output_sfdp(FILE *f)
{
	int i, len = 0;
	unsigned char sfdp_buffer[DEDIPROG_CFG_PRO_SIZE_SFDP];

	if (fread(sfdp_buffer, sizeof(sfdp_buffer), 1, f) != 1) {
		fprintf(stderr, "Error reading SFDP\n");
		return -1;
	}

	printf("\t\t\t/* SFDP */\n");
	printf("\t\t\t{ 0x23, 0xc9, 0x00, 0x01 },\n");
	len++;

	for (i = 0; i < DEDIPROG_CFG_PRO_SIZE_SFDP; i+=2) {
		printf("\t\t\t{ 0x23, 0xc1, 0x%02x, 0x%02x },\n",
		       sfdp_buffer[i+1], sfdp_buffer[i]);
		len++;
	}

	return len;
}

static int parse_and_output_srst(FILE *f)
{
	int i, len = 0;
	uint32_t magic;
	unsigned char srst_buffer[DEDIPROG_CFG_PRO_SIZE_SRST];

	if (fread(srst_buffer, sizeof(srst_buffer), 1, f) != 1) {
		fprintf(stderr, "Error reading SRST\n");
		return -1;
	}

	/* SRST has 0 or 3 entries before PROT */
	memcpy(&magic, &srst_buffer[0], sizeof(magic));

	if (magic != DEDIPROG_PROT_MAGIC) {
		int j;

		printf("\t\t\t/* SRST */\n");

		for (j = 0; j < 3; j++) {
			printf("\t\t\t{ 0x23, 0x%02x, 0x%02x, 0x%02x },\n",
			       srst_buffer[j*4+2],
			       srst_buffer[j*4+1],
			       srst_buffer[j*4]);
			len++;
		}

		/* Start after SFDP data and PROT magic */
		i = 16;
	} else {
		/* Start after PROT magic */
		i = 4;
	}

	printf("\t\t\t/* PROT */\n");
	printf("\t\t\t{ 0x23, 0xc4, 0x00, 0x01 },\n");
	len++;

	for (; i < DEDIPROG_CFG_PRO_SIZE_SRST; i+=2) {
		printf("\t\t\t{ 0x23, 0xc5, 0x%02x, 0x%02x },\n",
		       srst_buffer[i+1], srst_buffer[i]);
		len++;
	}

	return len;
}

int main(int argc, char *argv[])
{
	int i;
	FILE *f;
	struct dediprog_cfg_pro *cfg;
	const char *filename;
	int init_len = 0;

	printf("\n#ifndef EM100PRO_CHIPS_H\n");
	printf("#define EM100PRO_CHIPS_H\n\n");
	printf("#include <stdint.h>\n");
	printf("#define NUM_INIT_ENTRIES %d\n", DEDIPROG_CFG_PRO_MAX_ENTRIES);
	printf("#define BYTES_PER_INIT_ENTRY 4\n");
	printf("typedef struct {\n");
	printf("\tconst char *vendor;\n");
	printf("\tconst char *name;\n");
	printf("\tunsigned int size;\n");
	printf("\tuint8_t init[NUM_INIT_ENTRIES][BYTES_PER_INIT_ENTRY];\n");
	printf("\tint init_len;\n");
	printf("} chipdesc;\n\n");

	printf("const chipdesc chips[] = {\n");

	for (i=1; i<argc; i++) {
		uint32_t magic;

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

		init_len = parse_and_output_config(cfg);
		if (init_len < 0) {
			fprintf(stderr, "Error parsing %s\n", filename);
		}

		/* Handle any extra data */
		while (1) {
			int ret = 0;

			if (fread(&magic, sizeof(magic), 1, f) != 1)
				break;

			switch (magic) {
			case DEDIPROG_SFDP_MAGIC:
				ret = parse_and_output_sfdp(f);
				break;
			case DEDIPROG_SRST_MAGIC:
				ret = parse_and_output_srst(f);
				break;
			default:
				fprintf(stderr, "Unknown magic: 0x%08x\n",
					magic);
				break;
			}

			if (ret < 0) {
				perror(filename);
				fclose(f);
				exit(1);
			}

			init_len += ret;
		}

		printf("\t\t},\n");
		printf("\t\t.init_len = %d,\n", init_len);
		printf("\t},\n");

		fclose(f);
	}

	printf("\n\t{ .name = NULL}\n};\n");
	printf("#endif /* EM100PRO_CHIPS_H */\n");
	return 0;
}

