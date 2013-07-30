#!/bin/sh

autoreconf --install --force --symlink || exit 1

echo "Now run ./configure, make, and make install."
