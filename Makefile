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
CFLAGS+=-Wall -Werror -Wno-error=unused-function
CC?=gcc
PKG_CONFIG?=pkg-config

em100: em100.c em100pro_chips.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< \
		$(shell $(PKG_CONFIG) --cflags --libs libusb-1.0)

makechips: makechips.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $<

em100pro_chips.h: makechips
	./makechips.sh
	./makechips configs/*.cfg > $@

clean:
	rm -f em100

distclean: clean
	rm -rf configs makechips em100pro_chips.h

.PHONY: clean distclean
