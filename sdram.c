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

/* SDRAM related operations */

int read_sdram(struct em100 *em100, void *data, int address, int length)
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

int write_sdram(struct em100 *em100, unsigned char *data, int address,
		int length)
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
