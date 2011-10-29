/*
 * flashimg
 * Copyright (C) 2011  Yargil <yargil@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "config.h"

#define FLASH_TYPE_NAND	0
#define FLASH_TYPE_NOR	1


struct image {
	char *mem;
	size_t size;
};

struct action {
	char *part;
	char *file;
	char action;
};


void __nand_calculate_ecc(const unsigned char *buf, unsigned int eccsize,
		       unsigned char *code);

static void oob(const unsigned char *buf, size_t len, unsigned char *check)
{
	unsigned char code[32];

	memset(check, 0xff, 16);

	__nand_calculate_ecc(buf, 256, code);
	check[0] = code[0];
	check[1] = code[1];
	check[2] = code[2];

	__nand_calculate_ecc(buf+256, 256, code);
	check[3] = code[0];
	check[6] = code[1];
	check[7] = code[2];
}


struct partion {
	char *name;
	long off;
	long len;
};

static struct partion part_tab[32];
static int nb_part;
static int flash_type;

static void partition_file(const char *filename)
{
	int idx = 0;
	char name[64];
	long off, len;
	FILE *fp;

	/* 
	 * File format:
	 * <partion name> <offset> <length>
	 */
	printf("Partion list:\n");
	fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("Can't open partion file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	printf("name\toffset\t\tsize\n");
	do {
		int ret = fscanf(fp, "%s %li %li", name, &len, &off);
		if (ret != 3) break;
		part_tab[idx].name = strdup(name);
		part_tab[idx].off = off;
		part_tab[idx].len = len;
		printf("%s\t0x%08lx\t0x%08lx\n", name, off, len);
		idx++;
	} while(!feof(fp));
	nb_part = idx;

	fclose(fp);
}

static void partition_read(char *img, const char *part_name, const char *filename)
{
	char buf[512+16];
	int i, nb, ret,n, _nb;
	FILE *fp;
	unsigned long off;

	for(i=0;i<nb_part;i++) {
		if (!strcmp(part_tab[i].name, part_name)) {
			break;
		}
	}
	if (i==nb_part) return;

	printf("Partion %s found (0x%lx bytes @0x%lx)\n", part_name, part_tab[i].len, part_tab[i].off);
	n = i;

	if (flash_type == FLASH_TYPE_NAND)
		off = part_tab[i].off + (part_tab[i].off / 512) * 16;
	else
		off = part_tab[i].off;
	printf("off real=%lx\n", off);
	img += off;

	printf("Read partion:\n");
	fp = fopen(filename, "wb");
	if (fp == NULL) {
		printf("Can't open file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	_nb = nb = (part_tab[i].len + 511)/512;

	while (nb--) {
		memcpy(buf, img, 512);
		ret = fwrite(buf, 1, 512, fp);
		img += 512;
		if (flash_type == FLASH_TYPE_NAND)
			img += 16;
	}
	printf("Read %d blocks at %ld\n", _nb-nb, part_tab[i].off);

	fclose(fp);
}

static void partition_write(char *img, const char *part_name, const char *filename)
{
	unsigned char buf[512];
	unsigned char oob_buf[16];
	int i, nb, ret,n, _nb;
	FILE *fp;
	unsigned long off;

	printf("img=%p\n", img);
	for(i=0;i<nb_part;i++) {
		if (!strcmp(part_tab[i].name, part_name)) {
			break;
		}
	}
	if (i==nb_part) return;

	printf("Partion %s found (0x%lx bytes @0x%lx)\n", part_name, part_tab[i].len, part_tab[i].off);
	n = i;

	if (flash_type == FLASH_TYPE_NAND)
		off = part_tab[i].off + (part_tab[i].off / 512) * 16;
	else
		off = part_tab[i].off;
	printf("off real=%lx\n", off);

	printf("Erase partion\n");
	if (flash_type == FLASH_TYPE_NAND)
		memset(img + off, 0xff, part_tab[i].len);

	printf("Write partion:\n");
	fp = fopen(filename, "rb");
	if (fp == NULL) {
		printf("Can't open file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	_nb = nb = (part_tab[i].len + 511)/512;

	while (1) {
		memset(buf, 0, 512);
		ret = fread(buf, 1, 512, fp);
		if (ret <= 0) break;

		memcpy(img + off, buf, 512);
		off += 512;

		if (flash_type == FLASH_TYPE_NAND) {
			oob(buf, 512, oob_buf);
			memcpy(img + off, oob_buf, 16);
			off += 16;
		}

		if (ret != 512) break;
		nb--;
		if (nb == -1) {
			printf("File %s to big for the partion %s\n", filename, part_tab[i].name);
			exit(EXIT_FAILURE);
		}
	}
	printf("Write %d blocks at %ld\n", _nb-nb, part_tab[i].off);

	fclose(fp);
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [options]\n", name);
	printf("\t-v                    print version\n");
	printf("\t-s <size>\n");
	printf("\t-f <file>             image file\n");
	printf("\t-p <partition table file>\n");
	printf("\t-w <partition>,<file> write a partition\n");
	printf("\t-r <partition>,<file> read a partition\n");
	printf("\t-t <type>             flash type: nand or nor\n");
}

int main(int argc, char *argv[])
{
	int i;
	int opt;
	int fd_img;
	size_t img_size, len;
	char *filename = NULL, *p;
	char *img;
	struct action act_tab[32];
	int nb_act;

	nb_act = 0;
	img_size = 0;

	while ((opt = getopt(argc, argv, "vs:f:p:w:r:t:")) != -1) {
		switch (opt) {
			case 'v':
				printf(PACKAGE_NAME " version " VERSION "\n");
				break;
			case 's':
				img_size = atoi(optarg);
				switch (optarg[strlen(optarg)-1]) {
					case 'K':
					case 'k':
						img_size *= 1024;
						break;
					case 'M':
					case 'm':
						img_size *= 1024*1024;
						break;
					case 'G':
					case 'g':
						img_size *= 1024*1024*1024;
						break;
				}
				printf("size img = %ld\n", img_size);
				break;
			case 'f':
				filename = strdup(optarg);
				break;
			case 'w':
			case 'r':
				p = strchr(optarg, ',');
				*p = '\0';
				act_tab[nb_act].part = strdup(optarg);
				act_tab[nb_act].file = strdup(p+1);
				act_tab[nb_act].action = opt;
				nb_act++;
				break;
			case 'p':
				partition_file(optarg);
				break;
			case 't':
				if (!strcmp(optarg, "nand"))
					flash_type = FLASH_TYPE_NAND;
				if (!strcmp(optarg, "nor"))
					flash_type = FLASH_TYPE_NOR;
				break;
			default: /* '?' */
				usage(argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (!filename) {
		printf("Mising image file\n");
		return EXIT_FAILURE;
	}

	fd_img = open(filename, O_CREAT | O_RDONLY, 0666);
	len = lseek(fd_img, 0, SEEK_END);

	if (img_size == 0)
		img_size = len;

	if (img_size == 0) {
		printf("Image file is zero\n");
		return EXIT_FAILURE;
	}

	if (flash_type == FLASH_TYPE_NAND)
		img_size += img_size/512 * 16;

	img = malloc(img_size);
	if (img == NULL) {
		printf("Error malloc\n");
	}

	printf("Flash type: %s\n", flash_type == FLASH_TYPE_NAND ? "NAND": "NOR");
	memset(img, 0xFF, img_size);
	
	if (len) {
		printf("Read content file\n");
		lseek(fd_img, 0, SEEK_SET);
		read(fd_img, img, img_size);
	}
	close(fd_img);

	fd_img = open(filename, O_TRUNC | O_RDWR, 0666);

	for(i=0;i<nb_act;i++) {
		putchar('\n');
		if (act_tab[i].action == 'w')
			partition_write(img, act_tab[i].part, act_tab[i].file);
		else
			partition_read(img, act_tab[i].part, act_tab[i].file);
	}

	write(fd_img, img, img_size);
	close(fd_img);

	free(filename);

	return EXIT_SUCCESS;
}
