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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "em100.h"

enum ifd_version {
	IFD_VERSION_1,
	IFD_VERSION_2,
};

enum platform {
	PLATFORM_APL,
	PLATFORM_CNL,
	PLATFORM_GLK,
	PLATFORM_ICL,
	PLATFORM_SKLKBL,
	PLATFORM_TGL,
};

enum spi_frequency {
	SPI_FREQUENCY_20MHZ = 0,
	SPI_FREQUENCY_33MHZ = 1,
	SPI_FREQUENCY_48MHZ = 2,
	SPI_FREQUENCY_50MHZ_30MHZ = 4,
	SPI_FREQUENCY_17MHZ = 6,
};

/* flash descriptor */
typedef struct {
	uint32_t flvalsig;
	uint32_t flmap0;
	uint32_t flmap1;
	uint32_t flmap2;
} __attribute__((packed)) fdbar_t;

/* component section */
typedef struct {
	uint32_t flcomp;
	uint32_t flill;
	uint32_t flpb;
} __attribute__((packed)) fcba_t;

/**
 * valid_pointer - examine whether a pointer falls in [base, base + limit)
 * @param ptr:    the non-void* pointer to a single arbitrary-sized object.
 * @param base:   base address represented with char* type.
 * @param limit:  upper limit of the legal address.
 * @return:       ptr if valid, otherwise NULL.
 */
static void *valid_pointer(char *ptr, char *base, size_t limit)
{
	return ((char *)(ptr) >= (base) &&
		 (char *)&(ptr)[1] <= (base) + (limit)) ? ptr : NULL;
}

/**
 * find_fd - Find firmware flash descriptor
 * @param image: pointer to image in ram
 * @param size: size of image
 * @return: pointer to flash descriptor or NULL
 */
static fdbar_t *find_fd(char *image, int size)
{
#define FD_SIGNATURE 0x0FF0A55A
	int i, found = 0;

	/* Scan for FD signature */
	for (i = 0; i < (size - 4); i += 4) {
		if (*(uint32_t *) (image + i) == FD_SIGNATURE) {
			found = 1;
			break;	/* signature found. */
		}
	}

	if (!found) {
		printf("No Flash Descriptor found in this image\n");
		return NULL;
	}

	return (fdbar_t *)valid_pointer(image + i, image, size);
}


/**
 * find_fcba - Find FCBA block in image
 * @param image: pointer to image in ram
 * @param size: size of image
 * @return: pointer to FCBA or NULL
 */
static fcba_t *find_fcba(fdbar_t *fdb, char *image, int size)
{
	return (fcba_t *)valid_pointer(image + ((fdb->flmap0 & 0xff) << 4), image, size);
}

/*
 * There is no version field in the descriptor so to determine
 * if this is a new descriptor format we check the hardcoded SPI
 * read frequency to see if it is fixed at 20MHz or 17MHz.
 */
static int get_ifd_version(fcba_t *fcba, int platform)
{
	int read_freq;
	unsigned int i;

	/* Some newer platforms have re-defined the FCBA field that was
	 * used to distinguish IFD v1 v/s v2. Define a list of platforms
	 * that we know do not have the required FCBA field, but are IFD
	 * v2 and return true if current platform is one of them.
	 */
	static const int ifd_2_platforms[] = {
		PLATFORM_GLK,
		PLATFORM_CNL,
		PLATFORM_ICL,
		PLATFORM_TGL,
	};

	/* Unfortunately we do not have a way to determine the
	 * platform of an image any more than we have to determine
	 * its IFD version, but leaving this code in to elaborate
	 * a possible fix.
	 */
	for (i = 0; i < ARRAY_SIZE(ifd_2_platforms); i++) {
		if (platform == ifd_2_platforms[i])
			return IFD_VERSION_2;
	}

	read_freq = (fcba->flcomp >> 17) & 7;

	switch (read_freq) {
	case SPI_FREQUENCY_20MHZ:
		return IFD_VERSION_1;
	case SPI_FREQUENCY_17MHZ:
	case SPI_FREQUENCY_50MHZ_30MHZ:
		return IFD_VERSION_2;
	default:
		fprintf(stderr, "Unknown descriptor version: %d\n",
			read_freq);
		exit(EXIT_FAILURE);
	}
}

static void ifd_set_spi_frequency(fcba_t *fcba, enum spi_frequency freq)
{
	/* clear bits 21-30 */
	fcba->flcomp &= ~0x7fe00000;
	/* Read ID and Read Status Clock Frequency */
	fcba->flcomp |= freq << 27;
	/* Write and Erase Clock Frequency */
	fcba->flcomp |= freq << 24;
	/* Fast Read Clock Frequency */
	fcba->flcomp |= freq << 21;
}

static void ifd_set_em100_mode(fcba_t *fcba, struct em100 *em100)
{
	int freq;

	if (em100->hwversion == HWVERSION_EM100PRO_G2) {
		printf("Warning: EM100Pro-G2 can run at full speed.\n");
	}

	/* Auto-detect IFD version. Right now we don't support
	 * hard-coding the IFD version, hence passing -1 for platform.
	 */
	int ifd_version = get_ifd_version(fcba, -1);
	switch (ifd_version) {
	case IFD_VERSION_1:
		freq = SPI_FREQUENCY_20MHZ;
		printf("Limit SPI frequency to 20MHz.\n");
		break;
	case IFD_VERSION_2:
		freq = SPI_FREQUENCY_17MHZ;
		printf("Limit SPI frequency to 17MHz.\n");
		break;
	default:
		freq = SPI_FREQUENCY_17MHZ;
		printf("Limit SPI frequency to 17MHz.\n");
		break;
	}

	ifd_set_spi_frequency(fcba, freq);
}

/**
 * autocorrect_image: Modify image to work with EM100
 * @param em100: initialized em100 device structure
 * @param image: pointer to emulated image in RAM
 * @param size: size of emulated image
 *
 * @return: 0: image type known and image patched
 * @return: 1: image type not detected (unpatched)
 */

int autocorrect_image(struct em100 *em100, char *image, size_t size)
{
	printf("Auto-detecting image type ... ");
	fdbar_t *fdb = find_fd(image, size);
	if (fdb) {
		printf("IFD\n");

		fcba_t *fcba = find_fcba(fdb, image, size);
		if (!fcba) {
			printf("Inconsistent image.\n");
			return 1;
		}

		/* Set EM100 mode */
		ifd_set_em100_mode(fcba, em100);
	} else {
		printf("<unknown>\n");
		return 1; /* No support for other image types (yet). */
	}

	return 0;
}
