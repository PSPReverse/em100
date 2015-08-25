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

/* System level operations */

/**
 * get_version: fetch firmware version information
 * @param em100: initialized em100 device structure
 *
 * out(16 bytes): 0x10 0 .. 0
 * in(len + 4 bytes): 0x04 fpga_major fpga_minor mcu_major mcu_minor
 */
int get_version(struct em100 *em100)
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
		em100->mcu = (data[3] << 8) | data[4];
		em100->fpga = (data[1] << 8) | data[2];
		return 1;
	}
	return 0;
}

int set_voltage(struct em100 *em100, set_voltage_channel_t channel, int mV)
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

int get_voltage(struct em100 *em100, get_voltage_channel_t channel)
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

int set_led(struct em100 *em100, led_state_t led_state)
{
	unsigned char cmd[16];
	memset(cmd, 0, 16);
	cmd[0] = 0x13; /* set LED */
	cmd[1] = led_state;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	return 1;
}

