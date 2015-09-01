/*
 * Copyright 2012-2015 Google Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <getopt.h>
#include "em100.h"

/* SPI flash chips parameters definition */
#include "em100pro_chips.h"

volatile int do_exit_flag = 0;

void exit_handler(int sig) {
	do_exit_flag = 1;
}

struct em100_hold_pin_states {
	const char *description;
	int value;
};

static const struct em100_hold_pin_states hold_pin_states[] = {
	{ "FLOAT", 0x2 },
	{ "LOW", 0x0 },
	{ "INPUT", 0x3 },
	{ NULL, 0x0 },
};

/* High Level functions */

static int set_state(struct em100 *em100, int run)
{
	return write_fpga_register(em100, 0x28, run & 1);
}

static int set_hold_pin_state(struct em100 *em100, int pin_state)
{
	uint16_t val;

	/* Read and acknowledge hold pin state setting bit 2 of pin state respone. */
	if (!read_fpga_register(em100, 0x2a, &val)) {
		printf("Couldn't get hold pin state.\n");
		return 0;
	}
	write_fpga_register(em100, 0x2a, (1 << 2) | val);


	if (!read_fpga_register(em100, 0x2a, &val)) {
		printf("Couldn't get hold pin state.\n");
		return 0;
	}

	/* Now set desired pin state. */
	write_fpga_register(em100, 0x2a, pin_state);

	/* Read the pin state. */
	if (!read_fpga_register(em100, 0x2a, &val)) {
		printf("Couldn't get hold pin state.\n");
		return 0;
	}

	if (val != pin_state) {
		printf("Invalid pin state response: 0x%04x (expected 0x%04x)\n",
		       val, pin_state);
		return 0;
	}

	return 1;
}

static int set_hold_pin_state_from_str(struct em100 *em100, const char *state)
{
	int pin_state;
	const struct em100_hold_pin_states *s = &hold_pin_states[0];

	while (s->description != NULL) {
		if (!strcmp(s->description, state))
			break;
		s++;
	}
	if (s->description == NULL) {
		printf("Invalid hold pin state: %s\n", state);
		return 0;
	}
	pin_state = s->value;

	return set_hold_pin_state(em100, pin_state);
}

/**
 * get_serialno: fetch device's serial number
 * @param em100: initialized em100 device structure
 */
static int get_serialno(struct em100 *em100)
{
	unsigned char data[256];
	if (read_spi_flash_page(em100, 0x1fff00, data)) {
		em100->serialno = (data[5] << 24) | (data[4] << 16) | \
				  (data[3] << 8) | data[2];
		return 1;
	}
	return 0;
}

static int set_serialno(struct em100 *em100, unsigned int serialno)
{
	unsigned char data[512];
	unsigned int old_serialno;

	if (!read_spi_flash_page(em100, 0x1fff00, data))
		return 0;

	old_serialno = (data[5] << 24) | (data[4] << 16) | \
		       (data[3] << 8) | data[2];

	if (old_serialno == serialno) {
		printf("Serial number unchanged.\n");
		return 1;
	}

	data[2] = serialno;
	data[3] = serialno >> 8;
	data[4] = serialno >> 16;
	data[5] = serialno >> 24;

	if (old_serialno != 0xffffffff) {
		/* preserve magic */
		read_spi_flash_page(em100, 0x1f0000, data + 256);
		/* Unlock and erase sector. Reading
		 * the SPI flash ID is requires to
		 * actually unlock the chip.
		 */
		unlock_spi_flash(em100);
		get_spi_flash_id(em100);
		erase_spi_flash_sector(em100, 0x1f);
		/* write back magic */
		write_spi_flash_page(em100, 0x1f0000, data + 256);
	}

	if (!write_spi_flash_page(em100, 0x1fff00, data)) {
		printf("Error: Could not write SPI flash.\n");
		return 0;
	}
	get_serialno(em100);
	if (em100->serialno != 0xffffffff)
		printf("New serial number: DP%06d\n", em100->serialno);
	else
		printf("New serial number: N.A.\n");

	return 1;
}

static int check_status(struct em100 *em100)
{
	int spi_flash_id;

	spi_flash_id = get_spi_flash_id(em100);
	/* Check for Micron  (formerly Numonyx, formerly STMicro)
	 * M25P16 spi flash part
	 */
	if (spi_flash_id == 0x202015)
		return 1;
	return 0;
}

static int em100_attach(struct em100 *em100)
{
	libusb_device **devs;
	libusb_device_handle *dev;
	libusb_context *ctx = NULL;

	if (libusb_init(&ctx) < 0) {
		printf("Could not init libusb.\n");
		return 0;
	}

	libusb_set_debug(ctx, 3);

	if (libusb_get_device_list(ctx, &devs) < 0) {
		printf("Could not find USB devices.\n");
		return 0;
	}

	dev = libusb_open_device_with_vid_pid(ctx, 0x4b4, 0x1235);
	if (!dev) {
		printf("Could not find em100pro.\n");
		return 0;
	}

	libusb_free_device_list(devs, 1);

	if (libusb_kernel_driver_active(dev, 0) == 1) {
		if (libusb_detach_kernel_driver(dev, 0) != 0) {
			printf("Could not detach kernel driver.\n");
			return 0;
		}
	}

	if (libusb_claim_interface(dev, 0) < 0) {
		printf("Could not claim interface.\n");
		return 0;
	}

	em100->dev = dev;
	em100->ctx = ctx;

	if (!check_status(em100)) {
		printf("Device status unknown.\n");
		return 0;
	}

	if (!get_version(em100)) {
		printf("Failed fetching version information.\n");
		return 0;
	}

	if (!get_serialno(em100)) {
		printf("Failed fetching serial number.\n");
		return 0;
	}

	return 1;
}

static int em100_detach(struct em100 *em100)
{
	if (libusb_release_interface(em100->dev, 0) != 0) {
		printf("releasing interface failed.\n");
		return 1;
	}

	libusb_close(em100->dev);
	libusb_exit(em100->ctx);

	return 0;
}

static int set_chip_type(struct em100 *em100, const chipdesc *desc)
{
	unsigned char cmd[16];
	/* result counts unsuccessful send_cmd()s.
         * These are then converted in a boolean success value
         */
	int result = 0;
	int i;
	int fpga_voltage, chip_voltage = 0, wrong_voltage = 0;

	printf("Sending flash chip configuration\n");

	memset(cmd, 0, 16);

	fpga_voltage = em100->fpga & 0x8000 ? 1800 : 3300;

	for (i = 0; i < desc->init_len; i++) {
		if(desc->init[i][0] != 0x11 || desc->init[i][1] != 0x04)
			continue;

		chip_voltage = (desc->init[i][2] << 8) | desc->init[i][3];

		switch (chip_voltage) {
		case 1601: /* 1.65V-2V */
		case 1800:
			if (fpga_voltage == 3300)
				wrong_voltage = 1;
			break;
		case 2500: /* supported by both 1.8V and 3.3V FPGA */
			break;
		case 3300:
			if (fpga_voltage == 1800)
				wrong_voltage = 1;
		}
		break;
	}

	if (wrong_voltage) {
		printf("Error: The current FPGA firmware (%.1fV) does not "
			"support %s %s (%.1fV)\n", (float)fpga_voltage/1000,
			desc->vendor, desc->name, (float)chip_voltage/1000);
		return 0;
	}

	for (i = 0; i < desc->init_len; i++) {
		memcpy(&cmd[0], &desc->init[i][0], BYTES_PER_INIT_ENTRY);
		result += !send_cmd(em100->dev, cmd);
	}

	return !result;
}

static const struct option longopts[] = {
	{"set", 1, 0, 'c'},
	{"download", 1, 0, 'd'},
	{"start", 0, 0, 'r'},
	{"stop", 0, 0, 's'},
	{"verify", 0, 0, 'v'},
	{"holdpin", 1, 0, 'p'},
	{"help", 0, 0, 'h'},
	{"set-serialno", 1, 0, 'S'},
	{NULL, 0, 0, 0}
};

static void usage(void)
{
	printf("em100: em100 client utility\n\nUsage:\n"
		"  -c CHIP|--set CHIP: select CHIP emulation\n"
		"  -d[ownload] FILE:   upload FILE into em100\n"
		"  -r|--start:         em100 shall run\n"
		"  -s|--stop:          em100 shall stop\n"
		"  -v|--verify:        verify EM100 content matches the file\n"
		"  -p|--holdpin [LOW|FLOAT|INPUT]:       set the hold pin state\n"
		"  -h|--help:          this help text\n\n");
}

/* get MCU and FPGA version, *100 encoded */
int main(int argc, char **argv)
{
	int opt, idx;
	const char *desiredchip = NULL;
	const char *serialno = NULL;
	const char *filename = NULL;
	const char *holdpin = NULL;
	int do_start = 0, do_stop = 0;
        int verify = 0, trace = 0;
	while ((opt = getopt_long(argc, argv, "c:d:p:rsvhtS",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'c':
			desiredchip = optarg;
			break;
		case 'd':
			filename = optarg;
			/* TODO: check that file exists */
			break;
		case 'p':
			holdpin = optarg;
			break;
		case 'r':
			do_start = 1;
			break;
		case 's':
			do_stop = 1;
			break;
		case 'v':
			verify = 1;
			break;
		case 't':
			trace = 1;
			break;
		case 'S':
			serialno = optarg;
			break;
		case 'h':
			usage();
			return 0;
		}
	}

	const chipdesc *chip = chips;
	if (desiredchip) {
		do {
			if (strcasecmp(desiredchip, chip->name) == 0) {
				printf("will emulate '%s'\n", chip->name);
				break;
			}
		} while ((++chip)->name);

		if (chip->name == NULL) {
			printf("Supported chips:\n");
			chip = chips;
			do {
				printf("%s ", chip->name);
			} while ((++chip)->name);
			printf("\n\nCould not find emulation for '%s'.\n", desiredchip);

			return 1;
		}
	}

	struct em100 em100;
	if (!em100_attach(&em100)) {
		return 1;
	}

	printf("MCU version: %d.%02d\n", em100.mcu >> 8, em100.mcu & 0xff);
	if (em100.fpga > 0x0033) { /* 0.51 */
		printf("FPGA version: %d.%02d (%s)\n",
				em100.fpga >> 8 & 0x7f, em100.fpga & 0xff,
				em100.fpga & 0x8000 ? "1.8V" : "3.3V");
	} else {
		/* While the Dediprog software for Windows will refuse to work
		 * with 1.8V chips on older FPGA versions, it does not
		 * specifically output a voltage when reporting the FPGA
		 * version. We emulate this behavior here. Version 0.51 is
		 * known to behave the old way, 0.75 is behaving the new
		 * way.
		 */
		printf("FPGA version: %d.%02d\n", em100.fpga >> 8, em100.fpga & 0xff);
	}

	if (em100.serialno != 0xffffffff)
		printf("Serial number: DP%06d\n", em100.serialno);
	else
		printf("Serial number: N.A.\n");
	printf("SPI flash database: %s\n", VERSION);

	if (serialno) {
		unsigned int serial_number;
		if (sscanf(serialno, "%d", &serial_number) != 1)
			printf("Error: Can't parse serial number '%s'\n",
					serialno);
		else
			set_serialno(&em100, serial_number);

		return em100_detach(&em100);
	}

	if (do_stop) {
		set_state(&em100, 0);
	}

	if (desiredchip) {
		if (!set_chip_type(&em100, chip)) {
			printf("Failed configuring chip type.\n");
			return 0;
		}
	}

	if (holdpin) {
		if (!set_hold_pin_state_from_str(&em100, holdpin)) {
			printf("Failed configuring hold pin state.\n");
			return 0;
		}
	}

	if (filename) {
		int maxlen = 0x1000000; /* largest size - 16MB */
		void *data = malloc(maxlen);
		if (data == NULL) {
			printf("FATAL: couldn't allocate memory\n");
			return 1;
		}
		FILE *fdata = fopen(filename, "rb");
		if (!fdata) {
			perror("Could not open upload file");
			return 1;
		}

		int length = 0;
		while ((!feof(fdata)) && (length < maxlen)) {
			int blocksize = 65536;
			length += blocksize * fread(data+length, blocksize, 1, fdata);
		}
		fclose(fdata);

		if (length > maxlen) {
			printf("FATAL: length > maxlen\n");
			return 1;
		}

		write_sdram(&em100, (unsigned char *)data, 0x00000000, length);
		if (verify) {
			int done;
			void *readback = malloc(length);
			if (data == NULL) {
				printf("FATAL: couldn't allocate memory\n");
				return 1;
			}
			done = read_sdram(&em100, readback, 0x00000000, length);
			if (done && (memcmp(data, readback, length) == 0))
				printf("Verify: PASS\n");
			else
				printf("Verify: FAIL\n");
			free(readback);
		}

		free(data);
	}

	if (do_start) {
		set_state(&em100, 1);
	}

	if (trace) {
		struct sigaction signal_action;

		if (!set_hold_pin_state(&em100, 3)) {
			printf("Error: Failed to set EM100 to input\n");
			return 1;
		}
		set_state(&em100, 1);
		reset_spi_trace(&em100);

		signal_action.sa_handler = exit_handler;
		signal_action.sa_flags = 0;
		sigemptyset(&signal_action.sa_mask);
		sigaction(SIGINT, &signal_action, NULL);

		while (!do_exit_flag) {
			read_spi_trace(&em100);
		}

		set_state(&em100, 0);
		reset_spi_trace(&em100);

		if (!set_hold_pin_state(&em100, 2)) {
			printf("Error: Failed to set EM100 to float\n");
			return 1;
		}
	}

	return em100_detach(&em100);
}
