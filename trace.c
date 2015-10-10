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
#include <string.h>
#include "em100.h"

/* SPI Trace related operations */

/**
 * reset_spi_trace: clear SPI trace buffer
 * @param em100: em100 device structure
 *
 * out(16 bytes): 0xbd 0 .. 0
 */
int reset_spi_trace(struct em100 *em100)
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
static unsigned int counter = 0;
static unsigned char curpos = 0;
static unsigned char cmdid = 0xff; // timestamp, so never a valid command id

#define REPORT_BUFFER_LENGTH	8192
#define REPORT_BUFFER_COUNT	8

static int read_report_buffer(struct em100 *em100, 
	unsigned char reportdata[REPORT_BUFFER_COUNT][REPORT_BUFFER_LENGTH])
{
	unsigned char cmd[16] = {0};
	int len;
	unsigned int report;

	cmd[0] = 0xbc; /* read SPI trace buffer*/

	/* 
	 * Trace length, unit is 4k according to specs
	 *
	 * cmd1..cmd4 are probably u32BE on how many
	 * reports (8192 bytes each) to fetch
	 */
	cmd[1] = 0x00;
	cmd[2] = 0x00;
	cmd[3] = 0x00;
	cmd[4] = REPORT_BUFFER_COUNT;
	/* Timeout in ms */
	cmd[5] = 0x00;
	cmd[6] = 0x00;
	cmd[7] = 0x00;
	cmd[8] = 0x00;
	/* Trace Config
	 * [1:0] 00 start/stop spi trace according to emulation status
	 *       01 start when TraceConfig[2] == 1
	 *       10 start when trig signal goes high
	 *       11 RFU
	 * [2]   When TraceConfig[1:0] == 01 this bit starts the trace
	 * [7:3] RFU
	 */
	cmd[9] = 0x15;

	if (!send_cmd(em100->dev, cmd)) {
		printf("sending trace command failed\n");
		return 0;
	}

	for (report = 0; report < REPORT_BUFFER_COUNT; report++) {
		len = get_response(em100->dev, &reportdata[report][0],
				REPORT_BUFFER_LENGTH);
		if (len != REPORT_BUFFER_LENGTH) {
			printf("error, report length = %d instead of %d.\n\n",
					len, REPORT_BUFFER_LENGTH);
			return 0;
		}
	}

	return 1;
}

struct spi_cmd_values {
	char *cmd_name;
	uint8_t cmd;
	uint8_t uses_address;
	uint8_t pad_bytes;
};

struct spi_cmd_values spi_command_list[] = {
		/* name				cmd,	addr,	pad */
		{"write status register",	0x01,	0,	0},
		{"page program",		0x02,	1,	0},
		{"read",			0x03,	1,	0},
		{"write disable",		0x04,	0,	0},
		{"read status register",	0x05,	0,	0},
		{"write enable",		0x06,	0,	0},
		{"fast read",			0x0b,	1,	1},
		{"EM100 specific",		0x11,	0,	0},
		{"fast dual read",		0x3b,	1,	2},
		{"chip erase",			0x60,	0,	0},
		{"read JEDEC ID",		0x9f,	0,	0},
		{"chip erase",			0xc7,	0,	0},
		{"sector erase",		0xd8,	1,	0},

		{"unknown command",		0xff,	0,	0}
};

static struct spi_cmd_values * get_command_vals(uint8_t command) {
	/* cache last command so a search isn't needed every time */
	static struct spi_cmd_values *spi_cmd = &spi_command_list[3]; /* init to read */
	int i;

	if (spi_cmd->cmd != command) {
		for (i = 0; spi_command_list[i].cmd != 0xff; i++) {
			if (spi_command_list[i].cmd == command)
				break;
		}
		spi_cmd = &spi_command_list[i];
	}

	return spi_cmd;
}

#define MAX_TRACE_BLOCKLENGTH	6
int read_spi_trace(struct em100 *em100, int display_terminal,
		unsigned long addr_offset)
{
	unsigned char reportdata[REPORT_BUFFER_COUNT][REPORT_BUFFER_LENGTH] =
			{{0}};
	unsigned char *data;
	unsigned int count, i, report;
	static int outbytes = 0;
	static int additional_pad_bytes = 0;
	static unsigned int address = 0;
	static unsigned long long timestamp = 0;
	static unsigned long long start_timestamp = 0;
	static struct spi_cmd_values *spi_cmd_vals = &spi_command_list[3];

	if (!read_report_buffer(em100, reportdata))
		return 0;

	for (report = 0; report < REPORT_BUFFER_COUNT; report++) {
		data = &reportdata[report][0];
		count = (data[0] << 8) | data[1];
		if (count > 1022) {
			printf("Warning: EM100pro sends too much data.\n");
			count = 1022;
		}
		for (i = 0; i < count; i++) {
			unsigned int j = additional_pad_bytes;
			additional_pad_bytes = 0;
			unsigned char cmd = data[2 + i*8];
			if (cmd == 0xff) {
				/* timestamp */
				timestamp = data[2 + i*8 + 2];
				timestamp = (timestamp << 8) | data[2 + i*8 + 3];
				timestamp = (timestamp << 8) | data[2 + i*8 + 4];
				timestamp = (timestamp << 8) | data[2 + i*8 + 5];
				timestamp = (timestamp << 8) | data[2 + i*8 + 6];
				timestamp = (timestamp << 8) | data[2 + i*8 + 7];
				if (display_terminal)
					read_spi_terminal(em100, 1);
				continue;
			}

			/* from here, it must be data */
			if (cmd != cmdid) {
				unsigned char spi_command = data[i * 8 + 4];
				spi_cmd_vals = get_command_vals(spi_command);

				/* new command */
				cmdid = cmd;
				if (counter == 0)
					start_timestamp = timestamp;

				/* set up address if used by this command*/
				if (!spi_cmd_vals->uses_address) {
					j = 1; /* skip command byte */
				} else {
					address = (data[i * 8 + 5] << 16) +
							(data[i * 8 + 6] << 8) +
							data[i * 8 + 7];

					/* skip command, address bytes, and padding */
					j = 4 + spi_cmd_vals->pad_bytes;
					if (j > MAX_TRACE_BLOCKLENGTH) {
						additional_pad_bytes = j -
							MAX_TRACE_BLOCKLENGTH;
						j = MAX_TRACE_BLOCKLENGTH;
					}
				}
				printf("\nTime: %06lld.%08lld",
						(timestamp - start_timestamp) /
						100000000,
						(timestamp - start_timestamp) %
						100000000);
				printf(" command # %-6d : 0x%02x - %s",
						++counter, spi_command,
						spi_cmd_vals->cmd_name);
				curpos = 0;
				outbytes = 0;
			}

			/* this exploits 8bit wrap around in curpos */
			unsigned char blocklen = (data[2 + i*8 + 1] - curpos);
			blocklen /= 8;

			for (; j < blocklen; j++) {
				if (outbytes == 0) {
					if (spi_cmd_vals->uses_address) {
						printf("\n%08lx : ",
								addr_offset +
								address);
					} else {
						printf("\n         : ");
					}
				}
				printf("%02x ", data[i * 8 + 4 + j]);
				outbytes++;
				if (outbytes == 16) {
					outbytes = 0;
					if (spi_cmd_vals->uses_address)
						address += 16;
				}
			}
			// this is because the em100 counts funny
			curpos = data[2 + i*8 + 1] + 0x10;
			fflush(stdout);
		}
	}
	return 1;
}

#define UFIFO_SIZE	512
#define UFIFO_TIMEOUT	0x00

/*
 * Polls the uFIFO buffer to see if there's any data. The HT registers don't
 * seem to ever be updated to reflect that there's data present, and the
 * Dediprog software doesn't use them either.
 *
 * Multiple messages can be in a single uFIFO transfer, so loop through
 * the data looking for the signature.
 */
int read_spi_terminal(struct em100 *em100, int show_counter) {
	unsigned char data[UFIFO_SIZE] = { 0 };
	static unsigned int msg_counter = 1; /* Number of messages */
	uint16_t data_length;
	unsigned char *data_start;
	unsigned int j, k;
	struct em100_msg *msg = NULL;

	if (!read_ufifo(em100, UFIFO_SIZE, UFIFO_TIMEOUT, &data[0]))
		return 0;

	/* the first two bytes are the amount of valid data */
	data_length = (data[0] << 8) + data[1];
	if (data_length == 0)
		return 1;

	/* actual data starts after the length */
	data_start = &data[sizeof(uint16_t)];

	/* examine data; stop when we run out of message or buffer */
	for (j = 0; j < data_length &&
			j < UFIFO_SIZE - sizeof(struct em100_msg_header); j++) {

		msg = (struct em100_msg *)(data_start + j);
		if (msg->header.signature == EM100_MSG_SIGNATURE) {

			if (show_counter)
				printf("\nHT%06d: ", msg_counter);

			/* print message byte according to format */
			for (k = 0; k < msg->header.data_length; k++) {
				if (&msg->data[k] >= data_start + data_length)
					break;
				if (&msg->data[k] >= &data[0] + UFIFO_SIZE)
					break;

				switch (msg->header.data_type) {
				case ht_checkpoint_1byte:
				case ht_checkpoint_2bytes:
				case ht_checkpoint_4bytes:
				case ht_hexadecimal_data:
				case ht_timestamp_data:
					printf("%02x ", msg->data[k]);
					break;
				case ht_ascii_data:
					printf("%c", msg->data[k]);
					break;
				case ht_lookup_table:
					/* TODO - support lookup table */
					printf("Lookup unsupported: %02x%02x",
						msg->data[k], msg->data[k + 1]);
					k++;
					break;
				}
			}

			/* advance to the end of the message */
			j += msg->header.data_length +
					sizeof(struct em100_msg_header) - 1;
			msg_counter++;
			fflush(stdout);
		}
	}

	return 1;
}

int init_spi_terminal (struct em100 *em100)
{
	int retval = 0x01;
	uint16_t val;

	retval &= write_ht_register(em100, ufifo_data_fmt_reg, 0);
	retval &= write_ht_register(em100, status_reg, START_SPI_EMULATION);

	/* set em100 to recognize spi command 0x11 */
	retval &= write_fpga_register(em100, 0x82, EM100_SPECIFIC_CMD);
	retval &= read_fpga_register(em100, 0x28, &val);

	return retval;
}
