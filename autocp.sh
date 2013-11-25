#!/bin/bash

bin="flashimg"
dir="../temp"

if [ ! -d "$dir" ]; then
	echo "---> mkdir `pwd`/../temp/ ..."
	mkdir ../temp
fi

if [ ! -f "$bin" ]; then 
	echo "Please exec the 'make' in first!"
else
	echo "---> Now cp the '$bin' and 'uboot.part' to temp dir."
	cp flashimg ../temp/
	cp uboot.part ../temp/
fi
