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

/* Config file format v1.1:
 * All files 1.1 are 176 bytes
 * and data is stored little endian
 *
 *  0: Magic "Dcfg"
 *  4: Version 01 00 01 00 --> 1.1
 *     non-pro config files:
 *             00 00 01 00 --> 1.0 ( 2 byte commands)
 *  8: offset of init sequence (0x80)
 * 12: size of chip
 * 16: offset of vendor name (0x20)
 * 20: offset of device name (0x30)
 * 24: offset ? (0x40)
 *
 * offsets below are just examples and need
 * to be determined at run time:
 * 0x20: vendor name
 * 0x30: device name
 * 0x80: init sequence x23
 *       3 byte commands?
 *       4 bytes cmd[3] cmd[2] cmd[1] 00
 *               ....
 *               ff ff ff ff end of list
 * 0x94: init sequence x11
 *       4 bytes cmd[3] cmd[2] cmd[1] 00
 *               ....
 *               end of file
 */

unsigned char cfg[176];

int main(int argc, char *argv[])
{
	int i;
	FILE *f;

	printf("\n#ifndef EM100PRO_CHIPS_H\n");
	printf("#define EM100PRO_CHIPS_H\n\n");
	printf("typedef struct {\n\tconst char *name;\n\t");
	printf("unsigned int size;\n\tconst char x23[16];\n\t");
	printf("const char x11[6];\n} chipdesc;\n\n");
	printf("const chipdesc chips[] = {\n");

	for (i=1; i<argc; i++) {
		f = fopen(argv[i], "r");
		if (!f) {
			perror(argv[i]);
			exit(1);
		}
		size_t fread(void *ptr, size_t size, size_t nmemb, FILE
				*stream);
		if (fread(cfg, 176, 1, f) != 1) {
			perror(argv[i]);
			fclose(f);
			exit(1);
		}

		//printf("	/* %s */\n", argv[i]);
		printf("\t{ /* %s %s (%d kB) */\n", &cfg[0x20], 
				&cfg[0x30], *(uint32_t *)&cfg[0xc] / 1024);
		printf("\t\t.name = \"%s\",\n", &cfg[0x30]);
		printf("\t\t.size = 0x%x,\n", *(uint32_t *)&cfg[0xc]);
		printf("\t\t.x23 = {\n");
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0x81], cfg[0x80]);
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0x85], cfg[0x84]);
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0x89], cfg[0x88]);
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0x8d], cfg[0x8c]);

		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0x91], cfg[0x90]);
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0x95], cfg[0x94]);
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0x99], cfg[0x98]);
		printf("\t\t\t0x%02x, 0x%02x},\n", cfg[0x9d], cfg[0x9c]);
		printf("\t\t.x11 = {\n");
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0xa5], cfg[0xa4]);
		printf("\t\t\t0x%02x, 0x%02x,\n", cfg[0xa9], cfg[0xa8]);
		printf("\t\t\t0x%02x, 0x%02x}},\n", cfg[0xad], cfg[0xac]);

		fclose(f);
	}

	printf("\n\t{ .name = NULL}\n};\n");
	printf("#endif /* EM100PRO_CHIPS_H */\n");
	return 0;
}

