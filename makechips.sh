#!/bin/bash
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

URL="https://www.dediprog.com/save/78.msi/to/EM100Pro.msi"
VURL="https://www.dediprog.com/download?u=42&l=EM100Pro+SPI+Flash+Emulator"

if ! which curl > /dev/null; then
  echo "Install curl to run this script."
  exit 1;
fi
if ! which 7z > /dev/null; then
  echo "Install 7z (aka p7zip-full on Ubuntu, p7zip-plugins on fedora) to run this script."
  exit 1
fi

FILE=$(basename $URL)
TEMP=$(mktemp -d /tmp/makech.XXXXXX)
WD=$(readlink -f $(dirname $0))

cd $TEMP
if [ -r $WD/$FILE ]; then
  echo Copying $FILE...
  cp $WD/$FILE .
else
  echo Downloading $FILE...
  curl -s $URL -o $FILE || exit
fi
echo Unpacking configs...
VERSION="$( curl -s "$VURL" | grep -A1 EM100Pro\ Soft | tail -1 | cut -d\> -f2 | cut -d\< -f1 )"
echo "Detected SPI flash database \"$VERSION\""

if ! 7z x $FILE PRO_* > /dev/null ; then
  echo "No PRO_* components found..."
  rm -rf $TEMP
  exit 1
fi
echo  Copying configs...
mkdir -p $WD/configs
for i in PRO_*; do
  cp $i $WD/configs/${i#PRO_}.cfg
done
echo "${VERSION}" > $WD/configs/VERSION

echo Extract firmware files...
mkdir -p $WD/firmware
if ! 7z x $FILE F? F?? > /dev/null ; then
  echo "No F* components found..."
  rm -rf $TEMP
  exit 1
fi

cat $WD/firmware.txt | while read md5 type size version voltage
do
  if [ "$md5" == "--" ]; then
    $WD/makedpfw -m $MCU_FILE -M $MCU_VERSION \
                -f $FPGA_18V_FILE -F $FPGA_18V_VERSION \
                -o $WD/firmware/em100pro_fw_${MCU_VERSION}_${FPGA_18V_VERSION}_1.8V.dpfw
    $WD/makedpfw -m $MCU_FILE -M $MCU_VERSION \
                -f $FPGA_33V_FILE -F $FPGA_33V_VERSION \
                -o $WD/firmware/em100pro_fw_${MCU_VERSION}_${FPGA_33V_VERSION}_3.3V.dpfw
    continue
  fi
  for i in F? F??; do
    if [ "$( md5sum $i | cut -f1 -d\  )" == "$md5" ]; then
      if [ $type == "MCU" ]; then
        MCU_FILE=$i
        MCU_VERSION=$version
      fi
      if [ $type == "FPGA" ]; then
        if [ "$voltage" == "1.8V" ]; then
          FPGA_18V_FILE=$i
          FPGA_18V_VERSION=$version
        else
          FPGA_33V_FILE=$i
          FPGA_33V_VERSION=$version
        fi
      fi
      break
    fi
  done
done
echo "${VERSION}" > $WD/firmware/VERSION

cd $WD
rm -rf $TEMP
echo Done...

