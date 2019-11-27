/*
 * Copyright 2013-2015 Google Inc.
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
#include <stddef.h>
#include <ctype.h>
#include "em100.h"

void hexdump(const void *memory, size_t length)
{
	size_t i;
	uint8_t *m;
	int all_zero = 0;
	int all_one = 0;

	m = (uint8_t *) memory;

	for (i = 0; i < length; i += 16) {
		size_t j;

		all_zero++;
		all_one++;
		for (j = 0; j < 16; j++) {
			if (m[i + j] != 0) {
				all_zero = 0;
				break;
			}
		}
		for (j = 0; j < 16; j++) {
			if (m[i + j] != 0xff) {
				all_one = 0;
				break;
			}
		}
		if (all_zero < 2 && all_one < 2) {
			printf( "%08zx:", i);
			for (j = 0; j < 16; j++)
				printf( " %02x", m[i + j]);
			printf("  ");
			for (j = 0; j < 16; j++)
				printf( "%c",
				       isprint(m[i + j]) ? m[i + j] : '.');
			printf( "\n");
		} else if (all_zero == 2 || all_one == 2) {
			printf( "...\n");
		}
	}
}
