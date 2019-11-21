#
# Copyright 2012 Google Inc.
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

# Make is silent per default, but 'make V=1' will show all compiler calls.
Q:=@
ifneq ($(V),1)
ifneq ($(Q),)
.SILENT:
endif
endif

CFLAGS?=-O2 -g
CFLAGS+=-Wall -Werror
CC?=gcc
PKG_CONFIG?=pkg-config

SOURCES=em100.c firmware.c fpga.c hexdump.c sdram.c spi.c system.c trace.c usb.c
INCLUDES=em100pro_chips.h em100.h

all: em100

em100: $(SOURCES) $(INCLUDES)
	printf "  CC     em100\n"
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $(SOURCES) \
		$(shell $(PKG_CONFIG) --cflags --libs libusb-1.0)

em100pro_chips.h: makechips.sh
	printf "  CREATE em100pro_chips.sh & firmware images\n"
	LANG=C ./makechips.sh

makechips.sh: makedpfw makechips

%: %.c
	printf "  CC     $@\n"
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f em100 makedpfw makechips
	rm -rf configs firmware

distclean: clean
	rm em100pro_chips.h

.PHONY: clean distclean
