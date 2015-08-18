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
#include <signal.h>
#include <getopt.h>
#include <libusb.h>

#define BULK_SEND_TIMEOUT	5000	/* sentinel value */

/* SPI flash chips parameters definition */
#include "em100pro_chips.h"

volatile int do_exit_flag = 0;

void exit_handler(int sig) {
	do_exit_flag = 1;
}

struct em100 {
	libusb_device_handle *dev;
	libusb_context *ctx;
	uint16_t mcu;
	uint16_t fpga;
	uint32_t serialno;
};

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

/* USB communication */

static int send_cmd(libusb_device_handle *dev, void *data)
{
	int actual;
	int length = 16; /* haven't seen any other length yet */
	libusb_bulk_transfer(dev, 1 | LIBUSB_ENDPOINT_OUT, data, length, &actual, BULK_SEND_TIMEOUT);
	return (actual == length);
}

static int get_response(libusb_device_handle *dev, void *data, int length)
{
	int actual;
	libusb_bulk_transfer(dev, 2 | LIBUSB_ENDPOINT_IN, data, length, &actual, BULK_SEND_TIMEOUT);
	return actual;
}

static int check_status(struct em100 *em100)
{
	unsigned char cmd[16];
	unsigned char data[512];
	memset(cmd, 0, 16);
	cmd[0] = 0x30; /* status */
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	int len = get_response(em100->dev, data, 512);
	if ((len == 3) && (data[0] == 0x20) && (data[1] == 0x20) && (data[2] == 0x15))
		return 1;
	return 0;
}

/**
 * reset_spi_trace: clear SPI trace buffer
 * @param em100: em100 device structure
 *
 * out(16 bytes): 0xbd 0 .. 0
 */
static int reset_spi_trace(struct em100 *em100)
{
	unsigned char cmd[16];
	memset(cmd, 0, 16);
	cmd[0] = 0xbd; /* reset SPI trace buffer*/
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	return 1;
}

/**
 * read_spi_trace: fetch SPI trace data
 * @param em100: em100 device structure
 * globals: curpos, counter, cmdid
 *
 * out(16 bytes): bc 00 00 00 08 00 00 00 00 15 00 00 00 00 00 00
 * in(8x8192 bytes): 2 bytes (BE) number of records (0..0x3ff),
 *    then records of 8 bytes each
 */
unsigned int counter = 0;
unsigned char curpos = 0;
unsigned char cmdid = 0xff; // timestamp, so never a valid command id
static int read_spi_trace(struct em100 *em100)
{
	unsigned char cmd[16];
	unsigned char data[8192];
	unsigned int count, i, report;
	memset(cmd, 0, 16);
	cmd[0] = 0xbc; /* read SPI trace buffer*/
	cmd[4] = 0x08; /* cmd1..cmd4 are probably u32BE on how many
			  reports (8192 bytes each) to fetch */
	cmd[9] = 0x15; /* no idea */
	if (!send_cmd(em100->dev, cmd)) {
		printf("sending trace command failed\n");
		return 0;
	}
	for (report = 0; report < 8; report++) {
		memset(data, 0, sizeof(data));
		int len = get_response(em100->dev, data, sizeof(data));
		if (len != sizeof(data)) {
			/* FIXME: handle error: device reset? */
			printf("error, len = %d instead of %zd. bailing out\n\n", len, sizeof(data));
			return 0;
		}
		count = (data[0] << 8) | data[1];
		for (i = 0; i < count; i++) {
			unsigned int j;
			unsigned char cmd = data[2 + i*8];
			if (cmd == 0xff) {
				/* timestamp */
				unsigned long long timestamp = 0;
				timestamp = data[2 + i*8 + 2];
				timestamp = (timestamp << 8) | data[2 + i*8 + 3];
				timestamp = (timestamp << 8) | data[2 + i*8 + 4];
				timestamp = (timestamp << 8) | data[2 + i*8 + 5];
				timestamp = (timestamp << 8) | data[2 + i*8 + 6];
				timestamp = (timestamp << 8) | data[2 + i*8 + 7];
				printf("\ntimestamp: %lld.%lld", timestamp / 100000000, timestamp % 100000000);
				continue;
			}
#if 0
			printf("{(%d)", curpos);
			for (j = 0; j < 8; j++) {
				printf("%02x ", data[2 + i*8 + j]);
			}
			printf("}");
#endif
			/* from here, it must be data */
			if (cmd != cmdid) {
				/* new command */
				cmdid = cmd;
				printf("\nspi command %6d: ", ++counter);
				curpos = 0;
			}
			/* this exploits 8bit wrap around in curpos */
			unsigned char blocklen = (data[2 + i*8 + 1] - curpos);
			blocklen /= 8;
			for (j = 0; j < blocklen; j++) {
				printf("%02x ", data[2 + i*8 + 2 + j]);
			}
			curpos = data[2 + i*8 + 1] + 0x10; // this is because the em100 counts funny
		}
	}
	return 1;
}

/* System level operations */

/**
 * get_version: fetch firmware version information
 * @param em100: initialized em100 device structure
 *
 * out(16 bytes): 0x10 0 .. 0
 * in(len + 4 bytes): 0x04 fpga_major fpga_minor mcu_major mcu_minor
 */
static int get_version(struct em100 *em100)
{
	unsigned char cmd[16];
	unsigned char data[512];
	memset(cmd, 0, 16);
	cmd[0] = 0x10; /* version */
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	int len = get_response(em100->dev, data, 512);
	if ((len == 5) && (data[0] == 4)) {
		em100->mcu = (data[3]*100) + data[4];
		em100->fpga = (data[1]*100) + data[2];
		return 1;
	}
	return 0;
}

typedef enum {
	out_trigger_vcc = 0,
	out_reset_vcc   = 1,
	out_ref_plus    = 2,
	out_ref_minus   = 3,
	out_buffer_vcc  = 4
} set_voltage_channel_t;

static int set_voltage(struct em100 *em100, set_voltage_channel_t channel, int mV)
{
	unsigned char cmd[16];

	if ((channel == out_buffer_vcc) &&
		(mV != 18 && mV != 25 && mV != 33)) {
		printf("Error: For Buffer VCC, voltage needs to be 1.8V, 2.5V or 3.3V.\n");
		return 0;
	}

	memset(cmd, 0, 16);
	cmd[0] = 0x11; /* set voltage */
	cmd[1] = channel;
	cmd[2] = mV >> 8;
	cmd[3] = mV & 0xff;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	return 1;
}

typedef enum {
	in_v1_2        = 0,
	in_e_vcc       = 1,
	in_ref_plus    = 2,
	in_ref_minus   = 3,
	in_buffer_vcc  = 4,
	in_trigger_vcc = 5,
	in_reset_vcc   = 6,
	in_v3_3        = 7,
	in_buffer_v3_3 = 8,
	in_v5          = 9
} get_voltage_channel_t;

static int get_voltage(struct em100 *em100, get_voltage_channel_t channel)
{
	unsigned char cmd[16];
	unsigned char data[512];
	int voltage = 0;

	memset(cmd, 0, 16);
	cmd[0] = 0x12; /* measure voltage */
	cmd[1] = channel;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	int len = get_response(em100->dev, data, 512);
	if ((len == 3) && (data[0] == 2)) {
		voltage = (data[1] << 8) + data[2];
		return voltage;
	}
	return 0;
}

/**
 * get_serialno: fetch device's serial number
 * @param em100: initialized em100 device structure
 *
 * out(16 bytes): 0x33 0x1f 0xff 0 .. 0
 * in(len + 255 bytes): 0xff ?? serno_lo serno_hi ?? ?? .. ??
 */
static int get_serialno(struct em100 *em100)
{
	unsigned char cmd[16];
	unsigned char data[256];
	memset(cmd, 0, 16);
	cmd[0] = 0x33; /* read configuration block */
	cmd[1] = 0x1f;
	cmd[2] = 0xff;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	int len = get_response(em100->dev, data, 256);
	if ((len == 256) && (data[0] == 255)) {
		em100->serialno = (data[3]<<8) + data[2];
		return 1;
	}
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

	printf("Sending flash chip configuration\n");

	memset(cmd, 0, 16);

	for (i = 0; i < desc->init_len; i++) {
		memcpy(&cmd[0], &desc->init[i][0], BYTES_PER_INIT_ENTRY);
		result += !send_cmd(em100->dev, cmd);
	}

	return !result;
}

static int set_state(struct em100 *em100, int run)
{
	unsigned char cmd[16];
	memset(cmd, 0, 16);
	cmd[0] = 0x23;
	cmd[1] = 0x28;
	cmd[3] = run & 1;
	return send_cmd(em100->dev, cmd);
}

static int set_hold_pin_state(struct em100 *em100, int pin_state)
{
	unsigned char cmd[16];
	unsigned char data[512];
	int len;
	/* Read and acknowledge hold pin state setting bit 2 of pin state respone. */
	memset(cmd, 0, 16);
	cmd[0] = 0x22;
	cmd[1] = 0x2a;

	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	len = get_response(em100->dev, data, sizeof(data));
	if (len != 3 || data[0] != 0x2 || data[1] != 0x0) {
		printf("Couldn't get hold pin state.\n");
		return 0;
	}

	memset(cmd, 0, 16);
	cmd[0] = 0x23;
	cmd[1] = 0x2a;
	cmd[2] = 0x00;
	cmd[3] = (1 << 2) | data[2];
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}

	memset(cmd, 0, 16);
	cmd[0] = 0x22;
	cmd[1] = 0x2a;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	len = get_response(em100->dev, data, sizeof(data));
	if (len != 3 || data[0] != 0x2 || data[1] != 0x0) {
		printf("Couldn't get hold pin state.\n");
		return 0;
	}

	/* Now set desired pin state. */
	memset(cmd, 0, 16);
	cmd[0] = 0x23;
	cmd[1] = 0x2a;
	cmd[2] = 0x00;
	cmd[3] = pin_state;

	/* Send the pin state. */
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}

	/* Read the pin state. */
	memset(cmd, 0, 16);
	cmd[0] = 0x22;
	cmd[1] = 0x2a;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	len = get_response(em100->dev, data, sizeof(data));

	if (len != 3 || data[0] != 0x2 || data[1] != 0x0 || data[2] != pin_state) {
		printf("Invalid pin state response: len(%d) 0x%02x 0x%02x 0x%02x\n",
		       len, data[0], data[1], data[2]);
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

/* SDRAM related operations */

static int read_sdram(struct em100 *em100, void *data, int address, int length)
{
	int actual;
	int transfer_length = 0x200000;
	int bytes_read=0;
	int bytes_left;
	int bytes_to_read;
	unsigned char cmd[16];

	memset(cmd, 0, 16);
	cmd[0] = 0x41; /* em100-to-host eeprom data */
	cmd[1] = (address >> 24) & 0xff;
	cmd[2] = (address >> 16) & 0xff;
	cmd[3] = (address >> 8) & 0xff;
	cmd[4] = address & 0xff;
	cmd[5] = (length >> 24) & 0xff;
	cmd[6] = (length >> 16) & 0xff;
	cmd[7] = (length >> 8) & 0xff;
	cmd[8] = length & 0xff;

	if (!send_cmd(em100->dev, cmd)) {
		printf("error initiating host-to-em100 transfer.\n");
		return 0;
	}

	while (bytes_read < length) {
		actual = 0;

		bytes_left = length - bytes_read;
		bytes_to_read = (bytes_left < transfer_length) ?
			bytes_left : transfer_length;

		libusb_bulk_transfer(em100->dev, 2 | LIBUSB_ENDPOINT_IN,
				     data + bytes_read, bytes_to_read,
				     &actual, BULK_SEND_TIMEOUT);

		bytes_read += actual;
		if (actual < bytes_to_read) {
			printf("tried reading %d bytes, got %d\n",
			       bytes_to_read, actual);
			break;
		}

		printf("Read %d bytes of %d\n", bytes_read, length);
	}

	return (bytes_read == length);
}

static int write_sdram(struct em100 *em100, unsigned char *data, int address, int length)
{
	int actual;
	int transfer_length = 0x200000;
	int bytes_sent=0;
	int bytes_left;
	int bytes_to_send;
	unsigned char cmd[16];

	memset(cmd, 0, 16);
	cmd[0] = 0x40; /* host-to-em100 eeprom data */
	cmd[1] = (address >> 24) & 0xff;
	cmd[2] = (address >> 16) & 0xff;
	cmd[3] = (address >> 8) & 0xff;
	cmd[4] = address & 0xff;
	cmd[5] = (length >> 24) & 0xff;
	cmd[6] = (length >> 16) & 0xff;
	cmd[7] = (length >> 8) & 0xff;
	cmd[8] = length & 0xff;

	if (!send_cmd(em100->dev, cmd)) {
		printf("error initiating host-to-em100 transfer.\n");
		return 0;
	}

	while ( bytes_sent < length) {
		actual = 0;

		bytes_left = length - bytes_sent;
		bytes_to_send = (bytes_left < transfer_length) ? bytes_left : transfer_length;

		libusb_bulk_transfer(em100->dev, 1 | LIBUSB_ENDPOINT_OUT,
			data + bytes_sent, bytes_to_send, &actual, BULK_SEND_TIMEOUT);
		bytes_sent += actual;
		if (actual < bytes_to_send) {
			printf("Tried sending %d bytes, sent %d\n", bytes_to_send, actual);
			break;
		}

		printf("Sent %d bytes of %d\n", bytes_sent, length);
	}

	printf ("Transfer %s\n",bytes_sent == length ? "Succeeded" : "Failed");
	return (bytes_sent == length);
}

static const struct option longopts[] = {
	{"set", 1, 0, 'c'},
	{"download", 1, 0, 'd'},
	{"start", 0, 0, 'r'},
	{"stop", 0, 0, 's'},
	{"verify", 0, 0, 'v'},
	{"holdpin", 1, 0, 'p'},
	{"help", 0, 0, 'h'},
	{NULL, 0, 0, 0}
};

static void usage(void)
{
	printf("em100: em100 client utility\n\n"
		"-c CHIP|--set CHIP: select CHIP emulation\n"
		"-d[ownload] FILE:   upload FILE into em100\n"
		"-r|--start:         em100 shall run\n"
		"-s|--stop:          em100 shall stop\n"
		"-v|--verify:        verify EM100 content matches the file\n"
		"-p|--holdpin [LOW|FLOAT|INPUT]:       set the hold pin state\n"
		"-h|--help:          this help text\n");
}

/* get MCU and FPGA version, *100 encoded */
int main(int argc, char **argv)
{
	int opt, idx;
	const char *desiredchip = NULL;
	const char *filename = NULL;
	const char *holdpin = NULL;
	int do_start = 0, do_stop = 0;
        int verify = 0, trace = 0;
	while ((opt = getopt_long(argc, argv, "c:d:p:rsvht",
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
		case 'h':
			usage();
			return 0;
		}
	}

	const chipdesc *chip = chips;
	if (desiredchip) {
		do {
			if (strcmp(desiredchip, chip->name) == 0) {
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

	printf("MCU version: %d.%d\n", em100.mcu/100, em100.mcu%100);
	printf("FPGA version: %d.%d\n", em100.fpga/100, em100.fpga%100);
	printf("Serial number: DP%06d\n", em100.serialno);

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
			printf("failed to set em100 to input\n");
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
			printf("failed to set em100 to float\n");
			return 1;
		}
	}

	return em100_detach(&em100);
}
