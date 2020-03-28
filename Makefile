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
CFLAGS += -DXZ_USE_CRC64 -DXZ_DEC_ANY_CHECK -Ixz

LDFLAGS ?=
LDFLAGS += $(shell $(PKG_CONFIG) --libs libusb-1.0)
LDFLAGS += $(shell $(PKG_CONFIG) --libs libcurl)

CC ?= gcc
PKG_CONFIG ?= pkg-config

XZ = xz/xz_crc32.c  xz/xz_crc64.c  xz/xz_dec_bcj.c  xz/xz_dec_lzma2.c  xz/xz_dec_stream.c
SOURCES = em100.c firmware.c fpga.c hexdump.c sdram.c spi.c system.c trace.c usb.c
SOURCES += image.c curl.c chips.c tar.c net.c $(XZ)
OBJECTS = $(SOURCES:.c=.o)

all: dep em100

em100: $(OBJECTS)
	printf "  LD     em100\n"
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

%: %.c
	printf "  CC+LD  $@\n"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LDFLAGS)

%.o: %.c
	printf "  CC     $@\n"
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

dep: $(SOURCES)
	$(CC) $(CFLAGS) -MM $(SOURCES) > .dependencies.tmp
	sed -i 's,^xz,xz/xz,g' .dependencies.tmp
	mv .dependencies.tmp .dependencies

tarballs: makedpfw makechips.sh
	printf "  CREATE config & firmware images\n"
	LANG=C ./makechips.sh

clean:
	rm -f em100 makedpfw
	rm -f $(OBJECTS)
	rm -rf configs{,.tar.xz} firmware{,.tar.xz}
	rm -f .dependencies

distclean: clean
	rm -f em100pro_chips.h

-include .dependencies

.PHONY: clean distclean tarballs
