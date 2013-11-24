#!/bin/sh

sudo apt-get install libtool automake 

autoreconf --install --force --symlink || exit 1

echo "Now run ./configure, make, and make install."
