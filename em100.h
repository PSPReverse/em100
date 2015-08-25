/*
 * Copyright 2015 Google Inc.
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

#ifndef __EM100_H__
#define __EM100_H__

#include <libusb.h>

struct em100 {
	libusb_device_handle *dev;
	libusb_context *ctx;
	uint16_t mcu;
	uint16_t fpga;
	uint32_t serialno;
};

#define BULK_SEND_TIMEOUT	5000	/* sentinel value */

/* usb.c */
int send_cmd(libusb_device_handle *dev, void *data);
int get_response(libusb_device_handle *dev, void *data, int length);

/* fpga.c */
int reconfig_fpga(struct em100 *em100);
int check_fpga_status(struct em100 *em100);
int read_fpga_register(struct em100 *em100, int reg, uint16_t *val);
int write_fpga_register(struct em100 *em100, int reg, int val);

/* sdram.c */
int read_sdram(struct em100 *em100, void *data, int address, int length);
int write_sdram(struct em100 *em100, unsigned char *data, int address,
		int length);

/* spi.c */
int get_spi_flash_id(struct em100 *em100);
int erase_spi_flash(struct em100 *em100);
int poll_spi_flash_status(struct em100 *em100);
int read_spi_flash_page(struct em100 *em100, int addr, unsigned char *blk);
int write_spi_flash_page(struct em100 *em100, unsigned char *data,
		int address);
int read_ht_register(struct em100 *em100, int reg, uint8_t *val);
int write_ht_register(struct em100 *em100, int reg, uint8_t val);

/* system.c */
typedef enum {
	out_trigger_vcc = 0,
	out_reset_vcc   = 1,
	out_ref_plus    = 2,
	out_ref_minus   = 3,
	out_buffer_vcc  = 4
} set_voltage_channel_t;

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

typedef enum {
	both_off = 0,
	green_on = 1,
	red_on   = 2,
	both_on  = 3
} led_state_t;

int get_version(struct em100 *em100);
int set_voltage(struct em100 *em100, set_voltage_channel_t channel, int mV);
int get_voltage(struct em100 *em100, get_voltage_channel_t channel);
int set_led(struct em100 *em100, led_state_t led_state);

/* trace.c */
int reset_spi_trace(struct em100 *em100);
int read_spi_trace(struct em100 *em100);

#endif
