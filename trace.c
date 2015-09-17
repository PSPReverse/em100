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
		len = get_response(em100->dev, &reportdata[report][0], REPORT_BUFFER_LENGTH);
		if (len != REPORT_BUFFER_LENGTH) {
			printf("error, report length = %d instead of %d.\n\n",
					len, REPORT_BUFFER_LENGTH);
			return 0;
		}
	}

	return 1;
}

int read_spi_trace(struct em100 *em100)
{
	unsigned char reportdata[REPORT_BUFFER_COUNT][REPORT_BUFFER_LENGTH] = {{0}};
	unsigned char *data;
	unsigned int count, i, report;
	static int outbytes = 0;

	if (!read_report_buffer(em100, reportdata))
		return 0;

	for (report = 0; report < REPORT_BUFFER_COUNT; report++) {
		data = &reportdata[report][0];
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
				outbytes = 0;
			}
			/* this exploits 8bit wrap around in curpos */
			unsigned char blocklen = (data[2 + i*8 + 1] - curpos);
			blocklen /= 8;
			for (j = 0; j < blocklen; j++) {
				printf("%02x ", data[2 + i*8 + 2 + j]);
				outbytes++;
				if (outbytes == 16) {
					outbytes=0;
					printf("\n                    ");
				}
			}
			curpos = data[2 + i*8 + 1] + 0x10; // this is because the em100 counts funny
		}
	}
	return 1;
}
