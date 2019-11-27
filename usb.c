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
#include "em100.h"

/* USB communication */

int send_cmd(libusb_device_handle *dev, void *data)
{
	int actual;
	int length = 16; /* haven't seen any other length yet */
	libusb_bulk_transfer(dev, 1 | LIBUSB_ENDPOINT_OUT,
			data, length, &actual, BULK_SEND_TIMEOUT);
	return (actual == length);
}

int get_response(libusb_device_handle *dev, void *data, int length)
{
	int actual;
	libusb_bulk_transfer(dev, 2 | LIBUSB_ENDPOINT_IN,
			data, length, &actual, BULK_SEND_TIMEOUT);
	return actual;
}


