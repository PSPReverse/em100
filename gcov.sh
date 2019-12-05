#!/bin/bash

LOG=em100_test.log

i=0
banner_test()
{
	printf "\rRunning tests... %03d" $i
}

next_test()
{
	lcov --no-external --capture --directory . -t TEST_$i --output-file coverage.info >> $LOG
	i=$(( $i + 1))
}

printf "Building... "
echo "Test log" > $LOG
make clean >> $LOG
CFLAGS="-O2 -g -fomit-frame-pointer -fprofile-arcs -ftest-coverage" make >> $LOG
printf "ok\n";

banner_test
# some prep work
export EM100_HOME=$(mktemp -d)
i=$(( $i + 1))

banner_test
./em100 >>$LOG 2>&1
next_test

banner_test
./em100 --help >> $LOG 2>&1
next_test

banner_test
./em100 -U >> $LOG 2>&1
next_test

banner_test
./em100 --stop --set M25P80 -d file.bin -v --start >> $LOG 2>&1
next_test

banner_test
./em100 -U file.bin >> $LOG 2>&1
next_test

banner_test
./em100 --list-devices >> $LOG 2>&1
next_test

banner_test
./em100 --firmware-dump $EM100_HOME/test1.raw >> $LOG 2>&1
next_test

banner_test
./em100 --firmware-write $EM100_HOME/test2.dpfw >> $LOG 2>&1
next_test

banner_test
./em100 --firmware-update auto >> $LOG 2>&1
next_test

printf "\n\n"
printf "Now power cycle your EM100Pro\n"

genhtml coverage.info --output-directory out >> $LOG
rm *.gcno *.gcda xz/*.gcno xz/*.gcda

