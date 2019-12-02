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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <wordexp.h>

#include "em100.h"

TFILE *configs;
char *database_version;

volatile int do_exit_flag = 0;

static void exit_handler(int sig __unused)
{
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

static const char *get_pin_string(int pin) {
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

static int set_fpga_voltage(struct em100 *em100, int voltage_code)
{
	int val;

	if (!fpga_reconfigure(em100)) {
		printf("Couldn't reconfigure FPGA.\n");
		return 0;
	}

	if (!fpga_set_voltage(em100, voltage_code)) {
		printf("Couldn't set FPGA voltage.\n");
		return 0;
	}

	/* Must wait 2s before issuing any other USB comand */
	sleep(2);

	if (!fpga_get_voltage(em100, &val)) {
		printf("Couldn't get FPGA voltage.\n");
		return 0;
	}

	if (val != voltage_code) {
		printf("Invalid voltage response: %#x (expected %#x)\n", val,
		       voltage_code);
		return 0;
	}

	printf("Voltage set to %s\n", val == 18 ? "1.8" : "3.3");

	return 1;
}

static int set_fpga_voltage_from_str(struct em100 *em100,
				     const char *voltage_str)
{
	int voltage_code;

	if (!strcmp(voltage_str, "3.3"))
		voltage_code = 33;
	else if (!strcmp(voltage_str, "1.8"))
		voltage_code = 18;
	else {
		printf("Invalid voltage, use 1.8 or 3.3.\n");
		return 0;
	}

	return set_fpga_voltage(em100, voltage_code);
}

/**
 * get_device_info: fetch device's serial number and hardware version
 * @param em100: initialized em100 device structure
 */
static int get_device_info(struct em100 *em100)
{
	unsigned char data[256];
	if (read_spi_flash_page(em100, 0x1fff00, data)) {
		em100->serialno = (data[5] << 24) | (data[4] << 16) | \
				  (data[3] << 8) | data[2];
		em100->hwversion = data[1];
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

	/* Re-read serial number */
	get_device_info(em100);
	if (em100->serialno != 0xffffffff)
		printf("New serial number: %s%06d\n",
				em100->hwversion == HWVERSION_EM100PRO_EARLY ? "DP" : "EM",
				em100->serialno);
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

	if (!get_device_info(em100)) {
		printf("Failed to fetch serial number and hardware version.\n");
		return 0;
	}

	return 1;
}

static int em100_attach(struct em100 *em100, int bus, int device,
		uint32_t serial_number)
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
			printf("Could not find EM100pro with serial number EM%06d.\n",
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
		printf(" Bus %03d Device %03d: EM100pro %s%06d\n",
				libusb_get_bus_number(dev),
				libusb_get_device_address(dev),
				em100.hwversion == HWVERSION_EM100PRO_EARLY ? "DP" : "EM",
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
	int fpga_voltage, chip_voltage = 0;
	int req_voltage = 0;

	printf("Configuring SPI flash chip emulation.\n");

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
				req_voltage = 18;
			break;
		case 2500: /* supported by both 1.8V and 3.3V FPGA */
			break;
		case 3300:
			if (fpga_voltage == 1800)
				req_voltage = 33;
		}
		break;
	}

	if (req_voltage) {
		if (!set_fpga_voltage(em100, req_voltage)) {
			printf("Error: The current FPGA firmware (%.1fV) does "
			       "not support %s %s (%.1fV)\n",
			       (float)fpga_voltage / 1000, desc->vendor,
			       desc->name, (float)chip_voltage / 1000);
			return 0;
		}
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

/**
 * Searches for a specific FPGA register in the chip initialisation
 * sequence and returns the value in out.
 *
 * @reg1: e.g. FPGA write command (0x23)
 * @reg2: e.g. FPGA register
 *
 * Returns 0 on success.
 */
static int get_chip_init_val(const chipdesc *desc,
			     const uint8_t reg1,
			     const uint8_t reg2,
			     uint16_t *out)
{
	int i;

	for (i = 0; i < desc->init_len; i++) {
		if (desc->init[i][0] == reg1 && desc->init[i][1] == reg2) {
			*out = (desc->init[i][2] << 8) | desc->init[i][3];
			return 0;
		}
	}

	return 1;
}

typedef struct {
	uint16_t venid;
	uint16_t devid;
	int found;
	chipdesc chip;
} vendev_t;

static int get_chip_type_entry(char *name __unused, TFILE *dcfg, void *data, int ok __unused)
{
	uint16_t comp;
	chipdesc chip;

	vendev_t *v = (vendev_t *)data;

	parse_dcfg(&chip, dcfg);

	if (get_chip_init_val(&chip, 0x23, FPGA_REG_DEVID, &comp) || v->devid != comp)
		return 0;
	if (get_chip_init_val(&chip, 0x23, FPGA_REG_VENDID, &comp) || v->venid != comp)
		return 0;
	v->found = 1;
	v->chip = chip;
	return 1;
}

/**
 * Tries to identify the currently emulated SPI flash by looking at
 * known registers in the FPGA and matches those bits with the
 * chip initialisation sequence.
 *
 * Returns 0 on success.
 */
static int get_chip_type(struct em100 *em100, chipdesc *out)
{
	vendev_t v;

	/* Read manufacturer and vendor id from FPGA */
	if (!read_fpga_register(em100, FPGA_REG_VENDID, &v.venid))
		return 1;
	if (!read_fpga_register(em100, FPGA_REG_DEVID, &v.devid))
		return 1;

	tar_for_each(configs, get_chip_type_entry, (void *)&v);
	if (!v.found)
		return 1;

	*out = v.chip;

	return 0;
}

static int list_chips_entry(char *name __unused, TFILE *file, void *data __unused, int ok __unused)
{
	static chipdesc chip;
	/* Is the file a dcfg file? Then print the name, otherwise skip. */

	if (!parse_dcfg(&chip, file))
		printf("  • %s %s\n", chip.vendor, chip.name);

	return 0;
}

static chipdesc *setup_chips(const char *desiredchip)
{
	static chipdesc chip;
	char *configs_name = get_em100_file("configs.tar.xz");
	configs = tar_load_compressed(configs_name);
	free(configs_name);
	if (!configs) {
		printf("Can't find chip configs in $EM100_HOME/configs.tar.xz.\n");
		return NULL;
	}

	TFILE *version = tar_find(configs,"configs/VERSION", 1);
	if (!version) {
		printf("Can't find VERSION of chip configs.\n");
		return NULL;
	}
	database_version = (char *)version->address;
	tar_close(version);

	if (desiredchip) {
		char chipname[256];
		sprintf(chipname, "configs/%s.cfg", desiredchip);
		TFILE *dcfg = tar_find(configs, chipname, 0);
		if (!dcfg) {
			printf("Supported chips:\n\n");
			tar_for_each(configs, list_chips_entry, NULL);
			printf("\nCould not find a chip matching '%s' to be emulated.\n",
					desiredchip);
			return NULL;
		}
		parse_dcfg(&chip, dcfg);
		tar_close(dcfg);
		return &chip;
	}
	return NULL;
}

static char *get_em100_home(void)
{
	static char directory[FILENAME_BUFFER_SIZE] = "\0";

	if (directory[0] != 0)
		return directory;

	/* find out file */
	wordexp_t p;
	char *em100_home = getenv("EM100_HOME");
	if (em100_home)
		wordexp("$EM100_HOME/", &p, 0);
	else
		wordexp("$HOME/.em100/", &p, 0);

	strncpy(directory, p.we_wordv[0], FILENAME_BUFFER_SIZE - 1);
	wordfree(&p);

	DIR *dir = opendir(directory);
	if (dir) {
		// success
	} else if (errno == ENOENT) {
		if (mkdir(directory, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) {
			perror(directory);
			directory[0]=0;
			return NULL;
		}
	} else {
		perror("EM100_HOME inaccessible");
		directory[0]=0;
		return NULL;
	}

	return directory;
}

char *get_em100_file(const char *name)
{
	char file[FILENAME_BUFFER_SIZE + 1];
	strncpy(file, get_em100_home(), FILENAME_BUFFER_SIZE);
	strncat(file, name, FILENAME_BUFFER_SIZE - strlen(file) - 1);
	return strdup(file);
}

static const struct option longopts[] = {
	{"set", 1, 0, 'c'},
	{"download", 1, 0, 'd'},
	{"start-address", 1, 0, 'a'},
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
	{"update-files", 0, 0, 'U'},
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
		"  -a|--start address:             only works with -d (E.g. -d file.bin -a 0x300000)\n"
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
		"  -V|--set-voltage [1.8|3.3]      switch FPGA voltage\n"
		"  -p|--holdpin [LOW|FLOAT|INPUT]: set the hold pin state\n"
		"  -x|--device BUS:DEV             use EM100pro on USB bus/device\n"
		"  -x|--device EMxxxxxx            use EM100pro with serial no EMxxxxxx\n"
		"  -l|--list-devices               list all connected EM100pro devices\n"
		"  -U|--update-files               update device (chip) and firmware database\n"
		"  -D|--debug:                     print debug information.\n"
		"  -h|--help:                      this help text\n\n",
		name);
}

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
	unsigned int spi_start_address = 0;
	const char *voltage = NULL;

	while ((opt = getopt_long(argc, argv, "c:d:a:u:rsvtO:F:f:g:S:V:p:Dx:lUhT",
				  longopts, &idx)) != -1) {
		switch (opt) {
		case 'c':
			desiredchip = optarg;
			break;
		case 'd':
			filename = optarg;
			/* TODO: check that file exists */
			break;
		case 'a':
			sscanf(optarg, "%x", &spi_start_address);
			printf("SPI address: 0x%08x\n", spi_start_address);
			break;
		case 'u':
			read_filename = optarg;
			break;
		case 'V':
			voltage = optarg;
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
		case 'U':
			update_all_files();
			return 0;
		default:
		case 'h':
			usage(argv[0]);
			return 0;
		}
	}

	struct em100 em100;
	if (!em100_attach(&em100, bus, device, serial_number)) {
		return 1;
	}

	const chipdesc *chip = setup_chips(desiredchip);
	if (desiredchip && !chip)
		return 1;


	if (em100.hwversion == HWVERSION_EM100PRO || em100.hwversion == HWVERSION_EM100PRO_EARLY) {
		printf("MCU version: %d.%02d\n", em100.mcu >> 8, em100.mcu & 0xff);
		/* While the Dediprog software for Windows will refuse to work
		 * with 1.8V chips on older FPGA versions, it does not
		 * specifically output a voltage when reporting the FPGA
		 * version. We emulate this behavior here. Version 0.51 is
		 * known to behave the old way, 0.75 is behaving the new
		 * way.
		 */
		if (em100.fpga > 0x0033) /* 0.51 */
			printf("FPGA version: %d.%02d (%s)\n",
				em100.fpga >> 8 & 0x7f, em100.fpga & 0xff,
				em100.fpga & 0x8000 ? "1.8V" : "3.3V");
		else
			printf("FPGA version: %d.%02d\n", em100.fpga >> 8,
				em100.fpga & 0xff);
	} else {/* EM100Pro-G2 */
		printf("MCU version: %d.%d\n", em100.mcu >> 8, em100.mcu & 0xff);
		printf("FPGA version: %d.%03d\n",
			em100.fpga >> 8 & 0x7f, em100.fpga & 0xff);
	}

	printf("Hardware version: %u\n", em100.hwversion);

	if (em100.serialno != 0xffffffff)
		printf("Serial number: %s%06d\n",
				em100.hwversion == HWVERSION_EM100PRO_EARLY ? "DP" : "EM", em100.serialno);
	else
		printf("Serial number: N.A.\n");
	printf("SPI flash database: %s\n", database_version);
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
		/* if the user specified a serial containing EM, skip that */
		if ((serialno[0] == 'E' || serialno[0] == 'e') &&
		    (serialno[1] == 'M' || serialno[1] == 'm'))
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
		printf("Chip set to %s %s.\n", chip->vendor, chip->name);
	}

	if (voltage) {
		if (!set_fpga_voltage_from_str(&em100, voltage)) {
			printf("Failed configuring FPGA voltage.\n");
			return 1;
		}
	}

	if (holdpin) {
		if (!set_hold_pin_state_from_str(&em100, holdpin)) {
			printf("Failed configuring hold pin state.\n");
			return 0;
		}
	}

	if (read_filename) {
		int maxlen = 0x4000000; /* largest size - 64MB */

		if (!desiredchip) {
			/* Read configured SPI emulation from EM100 */
			chipdesc emulated_chip;

			if (!get_chip_type(&em100, &emulated_chip)) {
				printf("Configured to emulate %dkB chip\n", emulated_chip.size / 1024);
				maxlen = emulated_chip.size;
			}
		} else {
			maxlen = chip->size;
		}

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
		unsigned int maxlen = desiredchip ? chip->size : 0x4000000; /* largest size - 64MB */
		void *data = malloc(maxlen);
		int done;
		void *readback = NULL;

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

		unsigned int length = 0;
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

		if (desiredchip && (length != (chip->size - spi_start_address)) )
		{
			printf("FATAL: file size does not match to chip size.\n");
			free(data);
			return 1;
		}

		if (spi_start_address) {
			readback = malloc(maxlen);
			if (readback == NULL) {
				printf("FATAL: couldn't allocate memory(size: %x)\n", maxlen);
				free(data);
				return 1;
			}
			done = read_sdram(&em100, readback, 0, maxlen);
			if (done) {
				memcpy((unsigned char*)readback + spi_start_address, data, length);
				write_sdram(&em100, (unsigned char*)readback, 0x00000000, maxlen);
			} else {
				printf("Error: sdram readback failed\n");
			}
			free(readback);
		} else {
			write_sdram(&em100, (unsigned char*)data, 0x00000000, length);
		}

		if (verify) {
			readback = malloc(length);
			if (readback == NULL) {
				printf("FATAL: couldn't allocate memory\n");
				free(data);
				return 1;
			}
			done = read_sdram(&em100, readback, spi_start_address, length);
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
