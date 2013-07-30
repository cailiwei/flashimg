# what flags you want to pass to the C compiler & linker
AM_CFLAGS = --pedantic -Wall -O2 -DSTANDALONE --std=c99 -D_GNU_SOURCE
AM_LDFLAGS =

# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
bin_PROGRAMS = flashimg
flashimg_SOURCES = main.c nand_ecc.c
