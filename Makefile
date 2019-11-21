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

CFLAGS ?= -O2 -g -fomit-frame-pointer
CFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0)

CFLAGS += -Wall -Wundef -Wstrict-prototypes -Wmissing-prototypes
CFLAGS += -Wwrite-strings -Wredundant-decls -Wstrict-aliasing -Wshadow -Wextra
CFLAGS += -Wno-unused-but-set-variable
# Remove after fixing
CFLAGS += -Wno-sign-compare -Wno-discarded-qualifiers

LDFLAGS ?=
LDFLAGS += $(shell $(PKG_CONFIG) --libs libusb-1.0)

CC ?= gcc
PKG_CONFIG ?= pkg-config

SOURCES = em100.c firmware.c fpga.c hexdump.c sdram.c spi.c system.c trace.c usb.c
OBJECTS = $(SOURCES:.c=.o)

all: dep em100

em100: $(OBJECTS)
	printf "  LD     em100\n"
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

em100pro_chips.h: makechips.sh
	printf "  CREATE em100pro_chips.sh & firmware images\n"
	LANG=C ./makechips.sh

%: %.c
	printf "  CC+LD  $@\n"
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $<

%.o: %.c
	printf "  CC     $@\n"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

dep: $(SOURCES) em100pro_chips.h
	$(CC) $(CFLAGS) -MM $(SOURCES) > .dependencies
	#perl -pi -e 's,^xz,xz/xz,g' .dependencies

makechips.sh: makedpfw makechips

clean:
	rm -f em100 makedpfw makechips
	rm -f $(OBJECTS)
	rm -rf configs firmware
	rm -f .dependencies

distclean: clean
	rm -f em100pro_chips.h

-include .dependencies

.PHONY: clean distclean
