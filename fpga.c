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
#include <string.h>
#include <unistd.h>
#include "em100.h"

/* FPGA related operations */

/**
 * reconfig_fpga:  Reconfigures FPGA after a change(?)
 * @param em100: initialized em100 device structure
 *
 * out(16 bytes): 0x20 0 .. 0
 */
int reconfig_fpga(struct em100 *em100)
{
	unsigned char cmd[16];
	memset(cmd, 0, 16);
	cmd[0] = 0x20; /* reconfig FPGA */
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	/* Specification says to wait 2s before
	 * issuing another USB command
	 */
	sleep(2);
	return 1;
}

/**
 * check_fpga_status:  Checkl FPGA configuration status
 * @param em100: initialized em100 device structure
 *
 * out(16 bytes): 0x21 0 .. 0
 * in(result): pass: 1, fail: 0
 */
int check_fpga_status(struct em100 *em100)
{
	unsigned char cmd[16];
	unsigned char data[512];
	printf("FPGA configuration status: ");
	memset(cmd, 0, 16);
	cmd[0] = 0x21; /* Check FPGA status */
	if (!send_cmd(em100->dev, cmd)) {
		printf("Unknown\n");
		return 0;
	}
	int len = get_response(em100->dev, data, 512);
	if (len == 1) {
		printf("%s\n", data[0] == 1 ? "PASS" : "FAIL");
		return 1;
	}
	printf("Unknown\n");
	return 0;
}

/**
 * read_fpga_register: Read FPGA registers
 * @param em100: initialized em100 device structure
 * @param reg:   FPGA register to write
 * @param val:   pointer to value
 *
 * out(2 bytes): 0x22 RegAddr .. 0
 * in(len + 2 bytes): 0x02 val val
 */
int read_fpga_register(struct em100 *em100, int reg, uint16_t *val)
{
	unsigned char cmd[16];
	unsigned char data[256];

	memset(cmd, 0, 16);
	cmd[0] = 0x22; /* Read FPGA register */
	cmd[1] = reg;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	int len = get_response(em100->dev, data, 3);
	if ((len == 3) && (data[0] == 2)) {
		*val = (data[1] << 8) + data[2];
		return 1;
	}
	return 0;
}

/**
 * write_fpga_register: Write FPGA registers
 * @param em100: initialized em100 device structure
 *
 * out(4 bytes): 0x23 RegAddr Val Val .. 0
 */
int write_fpga_register(struct em100 *em100, int reg, int val)
{
	unsigned char cmd[16];
	memset(cmd, 0, 16);
	cmd[0] = 0x23; /* Write FPGA register */
	cmd[1] = reg;
	cmd[2] = val >> 8;
	cmd[3] = val & 0xff;
	if (!send_cmd(em100->dev, cmd)) {
		return 0;
	}
	return 1;
}

int fpga_set_voltage(struct em100 *em100, int voltage_code)
{
	unsigned char cmd[16];

	memset(cmd, '\0', 16);
	cmd[0] = 0x24; /* Switch FPGA */
	if (voltage_code == 18) {
		cmd[2] = 7;
		cmd[3] = 0x80;
	}
	if (!send_cmd(em100->dev, cmd))
		return 0;

	return 1;
}

int fpga_get_voltage(struct em100 *em100, int *voltage_codep)
{
	if (!get_version(em100))
		return 0;

	*voltage_codep = em100->fpga & 0x8000 ? 18 : 33;

	return 1;
}

int fpga_reconfigure(struct em100 *em100)
{
	unsigned char cmd[16];

	memset(cmd, '\0', 16);
	cmd[0] = 0x20; /* Switch FPGA */
	if (!send_cmd(em100->dev, cmd))
		return 0;

	return 1;
}
