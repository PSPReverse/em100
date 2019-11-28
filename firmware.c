/*
 * Copyright 2015 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em100.h"

/* Firmware Update File Format
 * ===========================
 *
 *  0x0000000: 65 6d 31 30 30 70 72 6f     - magic           (20 bytes)
 *  0x0000014: 32 2e 32 36                 - version MCU     (10 bytes)
 *  0x000001e: 30 2e 37 35                 - version FPGA    (10 bytes)
 *  0x0000028: 57 46 50 44                 - Rev / WFPD      (16 bytes)
 *  0x0000038: 00 01 00 00                 - file offset FPGA (4 bytes)
 *  0x000003c: 44 15 07 00                 - file length FPGA (4 bytes)
 *  0x0000040: 00 17 07 00                 - file offset MCU  (4 bytes)
 *  0x0000044: 00 bf 00 00                 - file length MCU  (4 bytes)
 *
 * EM100Pro / EM100Pro-G2 Flash Layout
 * ===================================
 *
 *  0x0000000: fpga firmware
 *  0x0100000: 256 bytes of 0x00 filled space
 *  0x0100100: mcu firmware
 *  0x01f0000: 4 bytes secret key, 00 padded
 *  0x01fff00: ff xx yy yy yy yy ff ff
 *    xx: HW version (0xff, 0x04, 0x06)
 *    yy: 4 bytes serial number
 *
 * Notes
 * =====
 *
 *  - Empty pages remain 0xff filled.
 *  - Partially used pages are typically filled up with 00 bytes
 *    except the page containing the serial number.
 *  - After a FPGA firmware update, the page at 0x0100000 needs to
 *    be written with 0xaa 0x55 BOOT 0x55 0xaa to make the FPGA
 *    slurp in the new firmware.
 */

#undef DEBUG

static void print_progress(int percent)
{
	int i;

	putchar('\r');
	putchar('[');
	for (i = 0; i < percent / 2; i++)
		putchar('=');
	for (i = 0; i < 50 - (percent / 2); i++)
		putchar(' ');
	putchar(']');
	if (percent == 100)
		putchar('\n');
	fflush(stdout);
}

static uint32_t get_le32(const unsigned char *in)
{
	return (in[3] << 24) | (in[2] << 16) | (in[1] << 8) | (in[0] << 0);
}

static void put_le32(unsigned char *out, uint32_t val)
{
	out[0] = val&0xff;
	out[1] = (val>>8)&0xff;
	out[2] = (val>>16)&0xff;
	out[3] = (val>>24)&0xff;
}

int firmware_dump(struct em100 *em100, const char *filename,
		int firmware_is_dpfw)
{
	unsigned char *data;
	size_t i, rom_size = 0;
	uint32_t id;
	FILE *fw;

	id = get_spi_flash_id(em100);
	switch (id) {
	case 0x202015:
		rom_size = 2 MB;
		break;
	default:
		printf("Unknown SPI flash id = %06x. Please report\n", id);
		return 1;
	}
	data = malloc(rom_size);
	if (data == NULL) {
		perror("Out of memory.\n");
		exit(1);
	}
	memset(data, 0, rom_size);

	printf("\nWriting EM100Pro firmware to file %s\n", filename);
	for (i=0; i < rom_size; i+=256) {
		if((i & 0x7fff) == 0)
			print_progress(i * 100 / rom_size);
		if (!read_spi_flash_page(em100, i, data+i)) {
			if (!read_spi_flash_page(em100, i, data+i))
				if (!read_spi_flash_page(em100, i, data+i))
					printf("\nERROR: Couldn't read @%08zx\n",
							i);
		}
	}
	print_progress(100);
	fw = fopen(filename, "wb");
	if (!fw) {
		perror(filename);
		free(data);
		return 0;
	}

	if (firmware_is_dpfw) {
		int fpga_size = 0, mcu_size = 0, hdrversion = 0;
		char all_ff[256];
		char mcu_version[8];
		char fpga_version[8];
		unsigned char header[0x100];

		switch (em100->hwversion) {
		case HWVERSION_EM100PRO_EARLY:
		case HWVERSION_EM100PRO:
			hdrversion=1;
			break;
		case HWVERSION_EM100PRO_G2:
			hdrversion=2;
			break;
		default:
			printf("Dumping DPFW firmware on hardware version %u is "
					"not yet supported.\n", em100->hwversion);
			exit(1);
		}

		memset(all_ff, 255, sizeof(all_ff));
		for (i = 0; i < 0x100000; i+=0x100) {
			if (memcmp(data+i, all_ff, 256) == 0)
				break;
		}
		if (i == 0x100000) {
			printf("Can't parse device firmware. Please extract"
					" raw firmware instead.\n");
			free(data);
			exit(1);
		}
		fpga_size = i;

		for (i = 0; i < 0xfff00; i+=0x100) {
			if (memcmp(data+0x100100+i, all_ff, 256) == 0)
				break;
		}
		if (i == 0xfff00) {
			printf("Can't parse device firmware. Please extract"
					" raw firmware instead.\n");
			free(data);
			exit(1);
		}
		mcu_size = i;

		snprintf(mcu_version, 8, "%d.%d",
				em100->mcu >> 8, em100->mcu & 0xff);
		snprintf(fpga_version, 8, "%d.%d",
				em100->fpga >> 8 & 0x7f, em100->fpga & 0xff);

		memset(header, 0, 0x100);
		switch (hdrversion) {
		case 1:
			memcpy(header, "em100pro", 8);
			break;
		case 2:
			memcpy(header, "EM100Pro-G2", 11);
			break;
		}
		memcpy(header + 0x28, "WFPD", 4);
		memcpy(header + 0x14, mcu_version, 4);
		memcpy(header + 0x1e, fpga_version, 4);
		put_le32(header + 0x38, 0x100);
		put_le32(header + 0x3c, fpga_size);
		put_le32(header + 0x40, 0x100 + fpga_size);
		put_le32(header + 0x44, 0x100 + mcu_size);
		fwrite(header, 0x100, 1, fw);
		fwrite(data, fpga_size, 1, fw);
		fwrite(data + 0x100100, mcu_size, 1, fw);
	} else {
		if (fwrite(data, rom_size, 1, fw) != 1)
			printf("ERROR: Couldn't write %s\n", filename);
	}
	fclose(fw);

#ifdef DEBUG
	hexdump(data, rom_size);
#endif
	free(data);
	return 0;
}

int firmware_update(struct em100 *em100, const char *filename, int verify)
{
#define MAX_VERSION_LENGTH 10
	unsigned char page[256], vpage[256];
	FILE *f;
	long fsize;
	unsigned char *fw;
	int i;
	int fpga_offset, fpga_len, mcu_offset, mcu_len;
	char fpga_version[MAX_VERSION_LENGTH + 1],
	     mcu_version[MAX_VERSION_LENGTH + 1];

	switch (em100->hwversion) {
	case HWVERSION_EM100PRO_EARLY:
	case HWVERSION_EM100PRO:
		printf("Detected EM100Pro (original).\n");
		break;
	case HWVERSION_EM100PRO_G2:
		printf("Detected EM100Pro-G2.\n");
		break;
	default:
		printf("Updating EM100Pro firmware on hardware version %u is "
				"not yet supported.\n", em100->hwversion);
		exit(1);
	}

	printf("\nAttempting firmware update with file %s\n", filename);

	f = fopen(filename, "rb");
	if (!f) {
		perror(filename);
		return 0;
	}

	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	if (fsize < 0) {
		perror(filename);
		fclose(f);
		return 0;
	}
	fseek(f, 0, SEEK_SET);

	fw = malloc(fsize);
	if (!fw) {
		printf("ERROR: out of memory.\n");
		fclose(f);
		return 0;
	}
	if (fread(fw, fsize, 1, f) != 1) {
		perror(filename);
		fclose(f);
		free(fw);
		return 0;
	}
	fclose(f);

	if (em100->hwversion == HWVERSION_EM100PRO_EARLY && (memcmp(fw, "em100pro", 8) != 0 ||
			memcmp(fw + 0x28, "WFPD", 4) != 0)) {
		printf("ERROR: Not an EM100Pro (original) firmware file.\n");
		free(fw);
		return 0;
	}

	if (em100->hwversion == HWVERSION_EM100PRO && (memcmp(fw, "em100pro", 8) != 0 ||
			memcmp(fw + 0x28, "WFPD", 4) != 0)) {
		printf("ERROR: Not an EM100Pro (original) firmware file.\n");
		free(fw);
		return 0;
	}

	if (em100->hwversion == HWVERSION_EM100PRO_G2 && (memcmp(fw, "EM100Pro-G2", 11) != 0 ||
			memcmp(fw + 0x28, "WFPD", 4) != 0)) {
		printf("ERROR: Not an EM100Pro-G2 firmware file.\n");
		free(fw);
		return 0;
	}

	/* Find firmwares in the update file */
	fpga_offset = get_le32(fw+0x38);
	fpga_len = get_le32(fw+0x3c);
	mcu_offset = get_le32(fw+0x40);
	mcu_len = get_le32(fw+0x44);

	/* Extracting versions */
	strncpy(mcu_version, (char *)fw + 0x14, MAX_VERSION_LENGTH);
	mcu_version[MAX_VERSION_LENGTH] = '\0';
	strncpy(fpga_version, (char *)fw + 0x1e, MAX_VERSION_LENGTH);
	fpga_version[MAX_VERSION_LENGTH] = '\0';

	printf("EM100Pro%s Update File: %s\n",
			em100->hwversion == HWVERSION_EM100PRO_G2 ? "-G2" : "", filename);
	if (em100->hwversion == HWVERSION_EM100PRO)
		printf("  Installed version:  MCU %d.%d, FPGA %d.%d (%s)\n",
			em100->mcu >> 8, em100->mcu & 0xff,
			em100->fpga >> 8 & 0x7f, em100->fpga & 0xff,
			em100->fpga & 0x8000 ? "1.8V" : "3.3V");
	else
		printf("  Installed version:  MCU %d.%d, FPGA %d.%03d\n",
			em100->mcu >> 8, em100->mcu & 0xff,
			em100->fpga >> 8 & 0x7f, em100->fpga & 0xff);

	printf("  New version:        MCU %s, FPGA %s\n",
			mcu_version, fpga_version);

	if (fpga_len < 256 || mcu_len < 256 ||
		fpga_len > 0x100000 || mcu_len > 0xf0000) {
		printf("\nFirmware file not valid.\n");
		free(fw);
		return 0;
	}

	/* Unlock and erase sector. Reading
	 * the SPI flash ID is requires to
	 * actually unlock the chip.
	 */
	unlock_spi_flash(em100);
	get_spi_flash_id(em100);

	printf("Erasing firmware:\n");
	for (i=0; i<=0x1e; i++) {
		print_progress(i * 100 / 0x1e);
		erase_spi_flash_sector(em100, i);
	}
	get_spi_flash_id(em100); // Needed?

	printf("Writing firmware:\n");

	/* Writing FPGA firmware */
	for (i = 0; i < fpga_len; i += 256) {
		memset(page, 0xff, 256);
		memcpy(page, fw +  fpga_offset + i, fpga_len - i
				> 256 ? 256 : fpga_len - i);
		write_spi_flash_page(em100, i, page);
		if ((i & 0xfff) == 0)
			print_progress((i * 100) / (fpga_len + mcu_len));
	}

	/* Writing MCU firmware */
	for (i = 0; i < mcu_len; i += 256) {
		memset(page, 0xff, 256);
		memcpy(page, fw +  mcu_offset + i, (mcu_len - i)
				> 256 ? 256 : mcu_len - i);
		write_spi_flash_page(em100, i + 0x100100, page);
		if ((i & 0xfff) == 0)
			print_progress(((fpga_len + i) * 100) /
					(fpga_len + mcu_len));
	}
	print_progress(100);

	if (verify) {
		printf("Verifying firmware:\n");
		for (i = 0; i < fpga_len; i += 256) {
			memset(page, 0xff, 256);
			memcpy(page, fw +  fpga_offset + i, fpga_len - i
					> 256 ? 256 : fpga_len - i);
			read_spi_flash_page(em100, i, vpage);

			if ((i & 0xfff) == 0)
				print_progress((i * 100) / (fpga_len + mcu_len));
			if (memcmp(page, vpage, 256))
				printf("\nERROR: Could not write FPGA firmware"
						" (%x).\n", i);
		}
		for (i = 0; i < mcu_len; i += 256) {
			memset(page, 0xff, 256);
			memcpy(page, fw +  mcu_offset + i, mcu_len - i
					> 256 ? 256 : mcu_len - i);
			read_spi_flash_page(em100, i + 0x100100, vpage);

			if ((i & 0xfff) == 0)
				print_progress(((fpga_len + i) * 100) /
						(fpga_len + mcu_len));
			if (memcmp(page, vpage, 256))
				printf("\nERROR: Could not write MCU firmware"
						" (%x).\n", i);
		}
	}
	print_progress(100);

	free(fw);

	/* Write magic update tag '.UBOOTU.' */
	memset(page, 0x00, 256);
	page[0] = 0xaa;
	page[1] = 0x55;
	page[2] = 0x42;
	page[3] = 0x4f;
	page[4] = 0x4f;
	page[5] = 0x54;
	page[6] = 0x55;
	page[7] = 0xaa;
	write_spi_flash_page(em100, 0x100000, page);

	if (verify) {
		read_spi_flash_page(em100, 0x100000, vpage);
		if (memcmp(page, vpage, 256))
			printf("ERROR: Could not write update tag.\n");
	}

	printf("\nDisconnect and reconnect your EM100pro\n");

	return 1;
}
