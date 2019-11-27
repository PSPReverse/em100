/*
 * Copyright 2019 Google LLC
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

#define _GNU_SOURCE
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "em100.h"
#include "xz.h"

#define ROUND_UP(n, inc) (n + (inc - n % inc) % inc)

typedef struct {
	char name[100];
	char mode[8];
	char owner[8];
	char group[8];
	char size[12];
	char mtime[12];
	char checksum[8];
	char type;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char padding[12];
} __packed tar_header_t;

static unsigned int checksum(tar_header_t *file)
{
	size_t i, chk_off = offsetof(tar_header_t, checksum);
	unsigned char *raw = (unsigned char *)file;
	unsigned int chksum = 256;

	for (i = 0; i < sizeof(tar_header_t); i++) {
		if (i >= chk_off && i < chk_off + 8)
			continue;
		chksum += raw[i];
	}
	return chksum;
}

int tar_for_each(TFILE *tfile, int (*run)(char *, TFILE *, void *, int), void *data)
{
	size_t i = 0;

	while (i < tfile->length) {
		tar_header_t *f = (tar_header_t *)(tfile->address + i);
		/* null header at end of tar */
		if (f->name[0] == 0)
			break;

		unsigned int size = strtol(f->size, NULL, 8);
		unsigned int cksum = strtol(f->checksum, NULL, 8);
		unsigned int ok = (checksum(f) == cksum);

		if (f->type == '0') {
			TFILE s;
			s.address = tfile->address + i + sizeof(tar_header_t);
			s.length = size;
			s.alloc = 0;

			if ((*run)(f->name, &s, data, ok))
				break;
		}

		if (!ok)
			break;
		i += sizeof(tar_header_t) + ROUND_UP(size, 512);
	}

	return 0;
}

static int tar_ls_entry(char *name, TFILE *file __unused, void *data __unused, int ok)
{
	printf("%s %s\n", name, ok?"✔":"✘");
	return 0;
}

int tar_ls(TFILE *tfile)
{
	tar_for_each(tfile, tar_ls_entry, NULL);
	return 0;
}

TFILE *tar_find(TFILE *tfile, const char *name, int casesensitive)
{
	size_t i = 0;
	TFILE *ret;

	while (i < tfile->length) {
		tar_header_t *f = (tar_header_t *)(tfile->address + i);
		if (f->name[0] == 0) /* null header at end of tar */
			break;

		unsigned int size = strtol(f->size, NULL, 8);
		unsigned int cksum = strtol(f->checksum, NULL, 8);
		unsigned int ok = (checksum(f) == cksum);

		if (!ok)
			break;

		int compare = casesensitive ? strncmp(name, f->name, 100) :
				strncasecmp(name, f->name, 100);
		if (!compare && f->type == '0') {
			ret = (TFILE *)malloc(sizeof(TFILE));
			if (!ret) {
				perror("Out of memory.\n");
				return NULL;
			}

			ret->address = tfile->address + i + sizeof(tar_header_t);
			ret->length = size;
			ret->alloc = 0;

			return ret;
		}
		i += sizeof(tar_header_t) + ROUND_UP(size, 512);
	}

	return NULL;
}

/* Finding the uncompressed size of an archive should really be part
 * of the xz API.
 */
static uint32_t decode_vli(unsigned char **streamptr)
{
	unsigned char *stream = *streamptr;
	uint32_t val = 0;
	int pos = 0;

	val = *stream & 0x7f;
	do {
		pos += 7;
		stream++;
		val |= ((uint32_t)*stream & 0x7f) << pos;
	} while (stream[0] & 0x80);
	*streamptr = stream+1;

	return val;
}

static uint32_t uncompressed_size(unsigned char *stream, size_t length)
{
	if (stream[length-2] != 0x59 || stream[length-1] != 0x5a) {
		printf("Bad stream footer.\n");
		return 0;
	}
	unsigned char *bytes = stream + length - 8;
	uint32_t backward_size =
		bytes[0] | (bytes[1]<<8) | (bytes[2]<<16) | (bytes[3]<<24);
	backward_size = (backward_size + 1) << 2;
	bytes = stream + length - 12 /* stream footer */ - backward_size;
	if (bytes[0] != 0x00) {
		printf("Bad index indicator.\n");
		return 0;
	}
	if (bytes[1] != 0x01) {
		printf("More than one index. I'm confused.\n");
		return 0;
	}
	bytes += 2;

	/* skip unpadded size */
	decode_vli(&bytes);
	return decode_vli(&bytes);

	return 0;
}

TFILE *tar_load_compressed(char *filename)
{
	FILE *f;
	long cfsize, fsize;
	unsigned char *cfw, *fw;

	/* Load our file into memory */

	f = fopen(filename, "rb");
	if (!f) {
		perror(filename);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	cfsize = ftell(f);
	if (cfsize < 0) {
		perror(filename);
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	cfw = malloc(cfsize);
	if (!cfw) {
		printf("Out of memory.\n");
		fclose(f);
		return NULL;
	}
	if (fread(cfw, cfsize, 1, f) != 1) {
		perror(filename);
		fclose(f);
		free(cfw);
		return NULL;
	}
	fclose(f);

	fsize = uncompressed_size(cfw, cfsize);
	fw = malloc(fsize);
	if (!fw) {
		printf("Out of memory.\n");
		free(cfw);
		return NULL;
	}

	/* Decompress xz */
	struct xz_buf b;
	struct xz_dec *s;
	enum xz_ret ret;

	xz_crc32_init();
#ifdef XZ_USE_CRC64
	xz_crc64_init();
#endif
	s = xz_dec_init(XZ_SINGLE, 0);
	if (s == NULL) {
		printf("Decompression init failed.\n");
		free(cfw);
		free(fw);
		return NULL;
	}

        b.in = cfw;
        b.in_pos = 0;
        b.in_size = cfsize;
        b.out = fw;
        b.out_pos = 0;
        b.out_size = fsize;

	ret = xz_dec_run(s, &b);
	if (ret != XZ_STREAM_END) {
		printf("Decompression failed.\n");
		free(cfw);
		free(fw);
		return NULL;
	}
	free(cfw);

	/* Prepare answer */
	TFILE *tfile = malloc(sizeof(TFILE));
	if (tfile == NULL) {
		printf("Out of memory.\n");
		free(fw);
		return NULL;
	}
	tfile->address = fw;
	tfile->length = fsize;
	tfile->alloc = 1;

	return tfile;
}

int tar_close(TFILE *tfile)
{
	if (tfile->alloc) {
		free(tfile->address);
	}
	free(tfile);
	return 0;
}
