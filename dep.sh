#!/bin/sh

if [ ! -d ../libmsettings ]; then
	cd ../ && git clone https://github.com/shauninman/libmsettings.git
fi

if [ ! -f ../libmsettings/libmsettings.so ]; then
	cd ../libmsettings && make
fi