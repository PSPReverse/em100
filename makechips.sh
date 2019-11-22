#!/bin/bash
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

URL="https://www.dediprog.com/download/save/78.msi"
VURL="https://www.dediprog.com/download?productCategory=SPI+Flash+Solution&productName=EM100Pro-G2+SPI+Flash+Emulator&fileType=10"

if ! which curl > /dev/null; then
  echo "Install curl to run this script."
  exit 1;
fi
if ! which msiextract > /dev/null; then
  echo "Install msitools (https://wiki.gnome.org/msitools) to run this script."
  exit 1
fi

FILE=EM100Pro.msi
TEMP=$(mktemp -d /tmp/makech.XXXXXX)
WD=$(readlink -f $(dirname $0))

cd $TEMP
if [ -r $WD/$FILE ]; then
  echo "    Copying $FILE..."
  cp $WD/$FILE .
else
  echo "    Downloading $FILE..."
  curl -s $URL -o $FILE || exit
fi
echo "    Unpacking ..."
VERSION="$( curl -s "$VURL" | grep -A2 EM100Pro-G2\ Soft|tail -1| cut -d\< -f1 | tr -d ' 	')"
echo "    Detected SPI flash database \"$VERSION\""

if ! msiextract $FILE > /dev/null ; then
  echo "    Could not unpack Windows installer..."
  rm -rf $TEMP
  exit 1
fi

echo "    Creating configs..."
mkdir -p $WD/configs
cp -a $TEMP/Program\ Files/DediProg/EM100/config/EM100Pro/*.cfg $WD/configs
echo -n "${VERSION}" > $WD/configs/VERSION
VERSION="${VERSION}" $WD/makechips $WD/configs/*.cfg > $WD/em100pro_chips.h

echo "    Extract firmware files..."
mkdir -p $WD/firmware
for i in $TEMP/Program\ Files/DediProg/EM100/firmware/EM100ProFW_*
do
  firmware=$( basename "$i" )
  tuple=${firmware#EM100ProFW_}
  v=${tuple: -3}
  voltage=${v/V/.}V
  mcu_version=${tuple: 1:1}.${tuple: 2:2}
  fpga_version=${tuple: 4:1}.${tuple: 5:2}

  $WD/makedpfw -m "$i/2.bin" -M $mcu_version -f "$i/1.bin" -F $fpga_version \
     -o $WD/firmware/em100pro_fw_${mcu_version}_${fpga_version}_${voltage}.dpfw
done
echo "${VERSION}" > $WD/firmware/VERSION

cd $WD
rm -rf $TEMP
