CFLAGS := -DSTANDALONE -ggdb3

all: flash

flash: main.o nand_ecc.o
	$(CC) -o $@ $^
