/*
 * Copyright 2017 Google LLC
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
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define ALIGN(x,a)              __ALIGN_MASK(x,(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)    (((x)+(mask))&~(mask))

/* dpfw file format:
 *  0x0000000: 65 6d 31 30 30 70 72 6f     - magic 8 bytes
 *  0x0000014: 32 2e 32 36                 - version MCU
 *  0x000001e: 30 2e 37 35                 - version FPGA
 *  0x0000028: 57 46 50 44                 - WFPD (??)
 *  0x0000038: 00 01 00 00                 - file offset FPGA
 *  0x000003c: 44 15 07 00                 - file length FPGA
 *  0x0000040: 00 17 07 00                 - file offset MCU
 *  0x0000044: 00 bf 00 00                 - file length MCU
 */

static void write32(void *a, uint32_t d)
{
	char *addr = (char *)a;
	addr[0] = d & 0xff;
	addr[1] = (d >> 8) & 0xff;
	addr[2] = (d >> 16) & 0xff;
	addr[3] = (d >> 24) & 0xff;
}

static const struct option longopts[] = {
	{"debug", 0, 0, 'D'},
	{"help", 0, 0, 'h'},
	{"output", 1, 0, 'o'},
	{"mcu-file", 1, 0, 'm'},
	{"mcu-version", 1, 0, 'M'},
	{"fpga-file", 1, 0, 'f'},
	{"fpga-version", 1, 0, 'F'},
	{NULL, 0, 0, 0}
};

static void usage(char *name)
{
	printf("makedpfw: EM100pro firmware update maker\n\nExample:\n"
		"  %s -m 2.bin -M 2.27 -f 1.bin -F 0.85 -o \n"
		"\nUsage:\n"
		"  -m|--mcu-file <file>            MCU firmware file name\n"
		"  -M|--mcu-version <version>      MCU firmware version\n"
		"  -f|--fpga-file <file>           FPGA firmware file name\n"
		"  -F|--fpga-version <version>     FPGA firmware version\n"
		"  -o|--output <file.dpfw>         output file name\n"
		"  -D|--debug:                     print debug information.\n"
		"  -h|--help:                      this help text\n\n",
		name);
}


int main(int argc, char *argv[])
{
	int opt, idx, params_ok = 1, debug = 0, c = 0;
	char *mcufile = NULL, *fpgafile = NULL;
	char *mcuversion = NULL, *fpgaversion = NULL;
	char *outfile = NULL;

	int mcu_size = 0, fpga_size = 0;
	char *mcu = NULL, *fpga = NULL, *out = NULL;
	FILE *mcu_f, *fpga_f, *out_f;

	struct stat s;

	while ((opt = getopt_long(argc, argv, "m:M:f:F:o:Dh",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'm':
			mcufile = optarg;
			break;
		case 'M':
			mcuversion = optarg;
			break;
		case 'f':
			fpgafile = optarg;
			break;
		case 'F':
			fpgaversion = optarg;
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'D':
			debug = 1;
			break;
		default:
		case 'h':
			printf ("Option %c\n", opt);
			usage(argv[0]);
			return 0;
		}
	}

	if (mcufile == NULL) {
		fprintf(stderr, "Need MCU file name (-m).\n");
		params_ok = 0;
	} else {
		if (stat(mcufile, &s)) {
			params_ok = 0;
			perror(mcufile);
		} else
			mcu_size = s.st_size;
	}

	if (mcuversion == NULL) {
		fprintf(stderr, "Need MCU version (-M).\n");
		params_ok = 0;
	} else {
		if (strlen(mcuversion) != 4) {
			printf("MCU version format: x.yy\n");
			params_ok = 0;
		}
	}

	if (fpgafile == NULL) {
		fprintf(stderr, "Need FPGA file name (-f).\n");
		params_ok = 0;
	} else {
		if (stat(fpgafile, &s)) {
			params_ok = 0;
			perror(fpgafile);
		} else
			fpga_size = s.st_size;
	}


	if (fpgaversion == NULL) {
		fprintf(stderr, "Need FPGA version (-F).\n");
		params_ok = 0;
	} else {
		if (strlen(fpgaversion) != 4) {
			printf("FPGA version format: x.yy\n");
			params_ok = 0;
		}
	}

	if (!params_ok)
		return 1;

	mcu = malloc(ALIGN(mcu_size, 0x100));
	fpga = malloc(ALIGN(fpga_size, 0x100));
	out = malloc(256);

	if (!mcu || !fpga || !out) {
		fprintf(stderr, "Out of memory.\n");
		return 1;
	}

	if (debug)
		printf("Reading input files.\n");

	mcu_f = fopen(mcufile, "r");
	if (mcu_f) {
		if (!fread(mcu, mcu_size, 1, mcu_f)) {
			perror(mcufile);
			params_ok = 0;
		}
		fclose(mcu_f);
	} else {
		perror(mcufile);
		params_ok = 0;
	}

	fpga_f = fopen(fpgafile, "r");
	if (fpga_f) {
		if (!fread(fpga, fpga_size, 1, fpga_f)) {
			perror(fpgafile);
			params_ok = 0;
		}
		fclose(fpga_f);
	} else {
		perror(fpgafile);
		params_ok = 0;
	}

	if (!params_ok)
		return 1;

	if (debug)
		printf("Preparing header.\n");

	memcpy(out + 0x00, "em100pro", 8);
	memcpy(out + 0x14, mcuversion, 4);
	memcpy(out + 0x1e, fpgaversion, 4);
	memcpy(out + 0x28, "WFPD", 4);

	write32(out + 0x38, 0x100);
	write32(out + 0x3c, fpga_size);

	write32(out + 0x40, ALIGN(0x100 + fpga_size, 0x100));
	write32(out + 0x44, mcu_size);

	if (debug)
		printf("Writing output file '%s'.\n", outfile);

	out_f = fopen(outfile, "w");
	if (out_f == NULL) {
		perror(outfile);
		return 1;
	}

	c += fwrite(out, 0x100, 1, out_f);
	c += fwrite(fpga, ALIGN(fpga_size, 0x100), 1, out_f);
	c += fwrite(mcu, ALIGN(mcu_size, 0x100), 1, out_f);

	if (c != 3) {
		fprintf(stderr, "%s: write failed.\n", outfile);
		return 1;
	}

	if (debug)
		printf("Done.\n");
	fclose(out_f);
	free(out);
	free(fpga);
	free(mcu);

	return 0;
}
