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

URL=http://www.dediprog.com/save/78.zip/to/EM100_4.2.19.zip

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
if ! 7z x $FILE ${FILE%.zip}.msi > /dev/null ; then
  echo "No msi component found. Is ${URL} a correct url?" >&2
  echo -n "check http://www.dediprog.com/download?u=42&l=EM100Pro and edit " >&2
  echo "$0 to use the latest archive URL" >&2
  rm -rf $TEMP
  exit 1
fi
if ! 7z x ${FILE%.zip}.msi PRO_* > /dev/null ; then
  echo "No PRO_* components found..."
  rm -rf $TEMP
  exit 1
fi
echo  Copying configs...
mkdir -p $WD/configs
for i in PRO_*; do
  cp $i $WD/configs/${i#PRO_}.cfg
done
cd $WD
rm -rf $TEMP
echo Done...

