#
# Copyright (C) 2012 The Chromium OS Authors.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
#

CFLAGS?=-O2 -g
CFLAGS+=-Wall -Werror
CC?=gcc
PKG_CONFIG?=pkg-config

SOURCES=em100.c fpga.c sdram.c spi.c system.c trace.c usb.c
INCLUDES=em100pro_chips.h em100.h

em100: $(SOURCES) $(INCLUDES)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(SOURCES) \
		$(shell $(PKG_CONFIG) --cflags --libs libusb-1.0)

em100pro_chips.h: makechips.c makechips.sh
	./makechips.sh
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o makechips $<
	VERSION="$$(cat configs/VERSION)" ./makechips configs/*.cfg > $@
	rm makechips

clean:
	rm -f em100

distclean: clean
	rm -rf configs makechips

.PHONY: clean distclean
