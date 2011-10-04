CFLAGS := -DSTANDALONE -Wall

all: flashimg

flashimg: main.o nand_ecc.o
	$(CC) -o $@ $^

clean:
	rm *.o flashimg
