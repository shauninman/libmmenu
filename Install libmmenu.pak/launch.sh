#!/bin/sh

{
	DIR=$(dirname "$0")
	cd "$DIR"

	cp ./libmmenu.so /usr/trimui/lib
	chmod 0755 /usr/trimui/lib/libmmenu.so
	sync
} &> "/mnt/SDCARD/Logs/Install libmmenu.txt"
