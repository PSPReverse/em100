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
 * Foundation, Inc.
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
	int retval = write_fpga_register(em100, 0x28, run & 1);

	if (retval)
		printf("%s EM100Pro\n", run ? "Started" : "Stopped");

	return retval;
}

static void get_current_state(struct em100 *em100)
{
	uint16_t state;
	if (read_fpga_register(em100, 0x28, &state))
		printf("EM100Pro currently %s\n", state ? "running" : "stopped");
	else
		printf("EM100Pro state unknown\n");
}

static char * get_pin_string(int pin) {
	switch (pin) {
	case 0:
		return ("low");
		break;
	case 2:
		return ("float");
		break;
	case 3:
		return ("input");
		break;
	}
	return ("unknown");
}

static void get_current_pin_state(struct em100 *em100)
{
	uint16_t val = 0xffff;
	read_fpga_register(em100, 0x2a, &val);
	printf("EM100Pro hold pin currently %s\n", get_pin_string(val));
}

static int set_hold_pin_state(struct em100 *em100, int pin_state)
{
	uint16_t val;

	/* Read and acknowledge hold pin state setting bit 2 of pin state response. */
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
		printf("Invalid pin state response: 0x%04x %s"
				" (expected 0x%04x %s)\n", val,
				get_pin_string(val), pin_state,
				get_pin_string(pin_state));
		return 0;
	}

	printf("Hold pin state set to %s\n", get_pin_string(val));
	return 1;
}

static int set_hold_pin_state_from_str(struct em100 *em100, const char *state)
{
	int pin_state;
	const struct em100_hold_pin_states *s = &hold_pin_states[0];

	while (s->description != NULL) {
		if (!strcasecmp(s->description, state))
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

static int em100_debug(struct em100 *em100)
{
	int i;
	printf("\nVoltages:\n");
	set_led(em100, both_off);
	printf("  1.2V:        %dmV\n", get_voltage(em100, in_v1_2));
	printf("  E_VCC:       %dmV\n", get_voltage(em100, in_e_vcc));
	set_led(em100, both_on);
	printf("  REF+:        %dmV\n", get_voltage(em100, in_ref_plus));
	printf("  REF-:        %dmV\n", get_voltage(em100, in_ref_minus));
	set_led(em100, red_on);
	printf("  Buffer VCC:  %dmV\n", get_voltage(em100, in_buffer_vcc));
	printf("  Trig VCC:    %dmV\n", get_voltage(em100, in_trigger_vcc));
	set_led(em100, both_on);
	printf("  RST VCC:     %dmV\n", get_voltage(em100, in_reset_vcc));
	printf("  3.3V:        %dmV\n", get_voltage(em100, in_v3_3));
	set_led(em100, red_on);
	printf("  Buffer 3.3V: %dmV\n", get_voltage(em100, in_buffer_v3_3));
	printf("  5V:          %dmV\n", get_voltage(em100, in_v5));
	set_led(em100, green_on);
	printf("\nFPGA registers:");
	for (i = 0; i < 256; i += 2) {
		uint16_t val;
		if ((i % 16) == 0)
			printf("\n  %04x: ", i);
		if (read_fpga_register(em100, i, &val))
			printf("%04x ", val);
		else
			printf("XXXX ");
	}

	printf("\n");
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

static int em100_init(struct em100 *em100, libusb_context *ctx,
		libusb_device_handle *dev)
{
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
		printf("Failed to fetch version information.\n");
		return 0;
	}

	if (!get_serialno(em100)) {
		printf("Failed to fetch serial number.\n");
		return 0;
	}

	return 1;
}

static int em100_attach(struct em100 *em100, int bus, int device,
		int serial_number)
{
	libusb_device_handle *dev = NULL;
	libusb_context *ctx = NULL;

	if (libusb_init(&ctx) < 0) {
		printf("Could not init libusb.\n");
		return 0;
	}

#if LIBUSB_API_VERSION < 0x01000106
	libusb_set_debug(ctx, 3);
#else
	libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);
#endif

	if ((!bus || !device) && !serial_number) {
		dev = libusb_open_device_with_vid_pid(ctx, 0x4b4, 0x1235);
	} else {
		libusb_device **devs, *d;
		int i;

		if (libusb_get_device_list(ctx, &devs) < 0) {
			printf("Could not find USB devices.\n");
			return 0;
		}

		for (i = 0; (d = devs[i]) != NULL; i++) {
			if ((bus > 0 && (libusb_get_bus_number(d) == bus)) &&
				(device > 0 &&
				(libusb_get_device_address(d) == device))) {

				struct libusb_device_descriptor desc;
				libusb_get_device_descriptor(d, &desc);
				if (desc.idVendor == 0x4b4 &&
						desc.idProduct == 0x1235) {
					if (libusb_open(d, &dev)) {
						printf("Couldn't open EM100pro"
								" device.\n");
						return 0;
					}
				} else {
					printf("USB device on bus %03d:%02d is"
							" not an EM100pro.\n",
							bus, device);
					return 0;
				}
				break;
			}
			if (serial_number) {
				struct libusb_device_descriptor desc;
				libusb_get_device_descriptor(d, &desc);
				if (desc.idVendor == 0x4b4 &&
						desc.idProduct == 0x1235) {
					if (libusb_open(d, &dev)) {
						printf("Couldn't open EM100pro"
								" device.\n");
						continue;
					}
					if (!dev) {
						printf("Couldn't open EM100pro"
								" device.\n");
						continue;
					}

					if (em100_init(em100, ctx, dev) &&
						(serial_number == em100->serialno))
						break;

					libusb_release_interface(dev, 0);
					libusb_close(dev);
					em100->dev = NULL;
					em100->ctx = NULL;
					em100->serialno = 0;
					dev = NULL;
				}
			}
		}

		libusb_free_device_list(devs, 1);
	}

	if (!dev) {
		if (bus && device)
			printf("Could not find EM100pro at %03d:%03d.\n", bus, device);
		else if (serial_number)
			printf("Could not find EM100pro with serial number DP%06d.\n",
					serial_number);
		else
			printf("Could not find EM100pro device.\n");

		return 0;
	}

	return em100_init(em100, ctx, dev);
}

static int em100_detach(struct em100 *em100)
{
	if (libusb_release_interface(em100->dev, 0) != 0) {
		printf("Releasing interface failed.\n");
		return 1;
	}

	libusb_close(em100->dev);
	libusb_exit(em100->ctx);

	return 0;
}

static int em100_list(void)
{
	struct em100 em100;
	libusb_device **devs, *dev;
	libusb_context *ctx = NULL;
	int i, count = 0;

	if (libusb_init(&ctx) < 0) {
		printf("Could not init libusb.\n");
		return 0;
	}

#if LIBUSB_API_VERSION < 0x01000106
	libusb_set_debug(ctx, 3);
#else
	libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);
#endif

	if (libusb_get_device_list(ctx, &devs) < 0) {
		printf("Could not find USB devices.\n");
		return 0;
	}

	for (i = 0; (dev = devs[i]) != NULL; i++) {
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(dev, &desc);
		if (desc.idVendor != 0x4b4 || desc.idProduct != 0x1235)
			continue;

		if (!em100_attach(&em100, libusb_get_bus_number(dev),
				libusb_get_device_address(dev), 0)) {
			printf("Could not read from EM100 at Bus %03d Device"
					" %03d\n", libusb_get_bus_number(dev),
					libusb_get_device_address(dev));
			continue;
		}
		printf(" Bus %03d Device %03d: EM100pro DP%06d\n",
				libusb_get_bus_number(dev),
				libusb_get_device_address(dev),
				em100.serialno);
		em100_detach(&em100);
		count++;
	}
	if (count == 0)
		printf("No EM100pro devices found.\n");
	libusb_exit(ctx);
	return 1;
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

	/*
	 * Set FPGA registers as the Dediprog software does:
	 * 0xc4 is set every time the chip type is updated
	 * 0x10 and 0x81 are set once when the software is initialized.
	 */
	write_fpga_register(em100, 0xc4, 0x01);
	write_fpga_register(em100, 0x10, 0x00);
	write_fpga_register(em100, 0x81, 0x00);

	return !result;
}

static const struct option longopts[] = {
	{"set", 1, 0, 'c'},
	{"download", 1, 0, 'd'},
	{"start", 0, 0, 'r'},
	{"stop", 0, 0, 's'},
	{"verify", 0, 0, 'v'},
	{"holdpin", 1, 0, 'p'},
	{"debug", 0, 0, 'D'},
	{"help", 0, 0, 'h'},
	{"trace", 0, 0, 't'},
	{"offset", 1, 0, 'O'},
	{"set-serialno", 1, 0, 'S'},
	{"firmware-update", 1, 0, 'F'},
	{"firmware-dump", 1, 0, 'f'},
	{"firmware-write", 1, 0, 'g'},
	{"device", 1, 0, 'x'},
	{"list-devices", 0, 0, 'l'},
	{"terminal",0 ,0, 'T'},
	{NULL, 0, 0, 0}
};

static void usage(char *name)
{
	printf("em100: EM100pro command line utility\n\nExample:\n"
		"  %s --stop --set M25P80 -d file.bin -v --start -t -O 0xfff00000\n"
		"\nUsage:\n"
		"  -c|--set CHIP:                  select chip emulation\n"
		"  -d|--download FILE:             download FILE into EM100pro\n"
	        "  -u|--upload FILE:               upload from EM100pro into FILE\n"
		"  -r|--start:                     em100 shall run\n"
		"  -s|--stop:                      em100 shall stop\n"
		"  -v|--verify:                    verify EM100 content matches the file\n"
		"  -t|--trace:                     trace mode\n"
		"  -O|--offset HEX_VAL:            address offset for trace mode\n"
		"  -T|--terminal:                  terminal mode\n"
		"  -F|--firmware-update FILE:      update EM100pro firmware (dangerous)\n"
		"  -f|--firmware-dump FILE:        export raw EM100pro firmware to file\n"
		"  -g|--firmware-write FILE:       export EM100pro firmware to DPFW file\n"
		"  -S|--set-serialno NUM:          set serial number to NUM\n"
		"  -p|--holdpin [LOW|FLOAT|INPUT]: set the hold pin state\n"
		"  -x|--device BUS:DEV             use EM100pro on USB bus/device\n"
		"  -x|--device DPxxxxxx            use EM100pro with serial no DPxxxxxx\n"
		"  -l|--list-devices               list all connected EM100pro devices\n"
		"  -D|--debug:                     print debug information.\n"
		"  -h|--help:                      this help text\n\n",
		name);
}

/* get MCU and FPGA version, *100 encoded */
int main(int argc, char **argv)
{
	int opt, idx;
	const char *desiredchip = NULL;
	const char *serialno = NULL;
	const char *filename = NULL, *read_filename = NULL;
	const char *firmware_in = NULL, *firmware_out = NULL;
	const char *holdpin = NULL;
	int do_start = 0, do_stop = 0;
	int verify = 0, trace = 0, terminal=0;
	int debug = 0;
	int bus = 0, device = 0;
	int firmware_is_dpfw = 0;
	unsigned int serial_number = 0;
	unsigned long address_offset = 0;

	while ((opt = getopt_long(argc, argv, "c:d:u:rsvtO:F:f:g:S:p:Dx:lhT",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'c':
			desiredchip = optarg;
			break;
		case 'd':
			filename = optarg;
			/* TODO: check that file exists */
			break;
		case 'u':
			read_filename = optarg;
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
		case 'O':
			sscanf(optarg, "%lx", &address_offset);
			printf("Address offset: 0x%08lx\n", address_offset);
			break;
		case 'T':
			terminal = 1;
			break;
		case 'S':
			serialno = optarg;
			break;
		case 'D':
			debug=1;
			break;
		case 'F':
			firmware_in = optarg;
			break;
		case 'f':
			firmware_out = optarg;
			break;
		case 'g':
			firmware_out = optarg;
			firmware_is_dpfw = 1;
			break;
		case 'x':
			if ((optarg[0] == 'D' || optarg[0] == 'd') &&
				(optarg[1] == 'P' || optarg[1] == 'p'))
				sscanf(optarg + 2, "%d", &serial_number);
			else
				sscanf(optarg, "%d:%d", &bus, &device);
			break;
		case 'l':
			em100_list();
			return 0;
		default:
		case 'h':
			usage(argv[0]);
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
			printf("\n\nCould not find emulation for '%s'.\n",
					desiredchip);

			return 1;
		}
	}

	struct em100 em100;
	if (!em100_attach(&em100, bus, device, serial_number)) {
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
		printf("FPGA version: %d.%02d\n", em100.fpga >> 8,
				em100.fpga & 0xff);
	}

	if (em100.serialno != 0xffffffff)
		printf("Serial number: DP%06d\n", em100.serialno);
	else
		printf("Serial number: N.A.\n");
	printf("SPI flash database: %s\n", VERSION);
	get_current_state(&em100);
	get_current_pin_state(&em100);
	printf("\n");

	if (debug) {
		em100_debug(&em100);
	}

	if (firmware_in) {
		firmware_update(&em100, firmware_in, verify);
		return em100_detach(&em100);
	}

	if (firmware_out) {
		firmware_dump(&em100, firmware_out, firmware_is_dpfw);
		return em100_detach(&em100);
	}

	if (serialno) {
		int offset = 0;
		/* if the user specified a serial containing DP, skip that */
		if ((serialno[0] == 'D' || serialno[0] == 'd') &&
		    (serialno[1] == 'P' || serialno[1] == 'p'))
			offset = 2;

		if (sscanf(serialno + offset, "%d", &serial_number) != 1)
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
		printf("Chip set to %s\n", desiredchip);
	}

	if (holdpin) {
		if (!set_hold_pin_state_from_str(&em100, holdpin)) {
			printf("Failed configuring hold pin state.\n");
			return 0;
		}
	}

	if (read_filename) {
		/* largest size - 64MB */
		int maxlen = desiredchip ? chip->size : 0x4000000;
		void *data = malloc(maxlen);
		if (data == NULL) {
			printf("FATAL: couldn't allocate memory\n");
			return 1;
		}
		FILE *fdata = fopen(read_filename, "wb");
		if (!fdata) {
			perror("Could not open download file");
			free(data);
			return 1;
		}

		read_sdram(&em100, data, 0x00000000, maxlen);

		int length = fwrite(data, maxlen, 1, fdata);
		fclose(fdata);
		free(data);

		if (length != 1) {
			printf("FATAL: failed to write");
			return 1;
		}
	}

	if (filename) {
		int maxlen = 0x4000000; /* largest size - 64MB */
		void *data = malloc(maxlen);
		if (data == NULL) {
			printf("FATAL: couldn't allocate memory\n");
			return 1;
		}
		FILE *fdata = fopen(filename, "rb");
		if (!fdata) {
			perror("Could not open upload file");
			free(data);
			return 1;
		}

		int length = 0;
		while ((!feof(fdata)) && (length < maxlen)) {
			int blocksize = 65536;
			length += blocksize * fread(data+length, blocksize, 1,
					fdata);
		}
		fclose(fdata);

		if (length > maxlen) {
			printf("FATAL: length > maxlen\n");
			free(data);
			return 1;
		}

		if (length == 0) {
			printf("FATAL: No file to upload.\n");
			free(data);
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

	if (trace || terminal) {
		struct sigaction signal_action;

		if ((holdpin == NULL) && (!set_hold_pin_state(&em100, 3))) {
			printf("Error: Failed to set EM100 to input\n");
			return 1;
		}

		if (!do_start && !do_stop)
			set_state(&em100, 1);

		printf ("Starting ");

		if (trace) {
			reset_spi_trace(&em100);
			printf("trace%s", terminal ? " & " : "");
		}

		if (terminal) {
			init_spi_terminal(&em100);
			printf("terminal");
		}

		printf(". Press CTL-C to exit.\n\n");
		signal_action.sa_handler = exit_handler;
		signal_action.sa_flags = 0;
		sigemptyset(&signal_action.sa_mask);
		sigaction(SIGINT, &signal_action, NULL);

		while (!do_exit_flag) {
			if (trace)
				read_spi_trace(&em100, terminal,
						address_offset);
			else
				read_spi_terminal(&em100, 0);
		}

		if (!do_start && !do_stop)
			set_state(&em100, 0);
		if (trace)
			reset_spi_trace(&em100);

		if ((holdpin == NULL) && (!set_hold_pin_state(&em100, 2))) {
			printf("Error: Failed to set EM100 to float\n");
			return 1;
		}
	}

	return em100_detach(&em100);
}
