/*
 * flashimg
 * Copyright (C) 2011-2012  Yargil <yargil@free.fr>
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

struct ecc_info {
	int page_size;
	int oob_size;
	int ecc_nb;
	int ecc_pos[24];
};

struct ecc_info const ecc_tab[] = {
	{
	.page_size = 256,
	.oob_size = 8,
	.ecc_nb = 3,
	.ecc_pos = { 0, 1, 2 },
	},

	{
	.page_size = 512,
	.oob_size = 16,
	.ecc_nb = 6,
	.ecc_pos = { 0, 1, 2, 3, 6, 7 },
	},

	{
	.page_size = 2048,
	.oob_size = 64,
	.ecc_nb = 24,
	.ecc_pos = {
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61, 62, 63 },
	},
};


struct partition {
	char *name;
	long off;
	long len;
};

static struct ecc_info const *ecc = NULL;
static int page_size;
static struct partition part_tab[32];
static int nb_part;
static int flash_type;

void __nand_calculate_ecc(const unsigned char *buf, unsigned int eccsize,
		       unsigned char *code);

static void oob(const unsigned char *buf, size_t len, unsigned char *check)
{
	int i;
	unsigned char code[32], *_code;

	memset(check, 0xff, 16);

	_code = code;
	for (i=0;i<len/256;i++) {
		__nand_calculate_ecc(buf+i*256, 256, _code);
		check[0] = _code[0];
		check[1] = _code[1];
		check[2] = _code[2];
		_code += 3;
	}

	for (i=0;i<ecc->ecc_nb;i++)
		check[ecc->ecc_pos[i]] = code[i];
}

/*
 * Parse the partition file
 */
static int partition_file(const char *filename)
{
	int idx = 0;
	char name[64];
	long off, len;
	FILE *fp;
	int retval = 0;

	/* 
	 * File format:
	 * <partition name> <offset> <length>
	 */
	printf("Partition list:\n");
	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "Can't open partition file %s\n", filename);
		return -1;
	}

	printf("name\toffset\t\tsize\n");
	do {
		int ret = fscanf(fp, "%s %li %li", name, &len, &off);
		if (ret == -1) break;
		if (ret != 3) {
			retval = -1;
			fprintf(stderr, "Error in partition file\n");
			break;
		}
		part_tab[idx].name = strdup(name);
		part_tab[idx].off = off;
		part_tab[idx].len = len;
		printf("%s\t0x%08lx\t0x%08lx\n", name, off, len);
		idx++;
	} while(!feof(fp));
	nb_part = idx;

	fclose(fp);

	return retval;
}

/*
 * Read data from image file
 */
static void partition_read(struct image *img, const char *part_name, const char *filename)
{
	char *buf;
	int i, nb_page, pages;
	FILE *fp;
	unsigned long off;

	buf = malloc(page_size);
	for (i=0;i<nb_part;i++) {
		if (!strcmp(part_tab[i].name, part_name)) {
			break;
		}
	}
	if (i == nb_part) return;

	printf("Partion %s found (0x%lx bytes @0x%lx)\n",
			part_name, part_tab[i].len, part_tab[i].off);

	if (flash_type == FLASH_TYPE_NAND)
		off = part_tab[i].off + (part_tab[i].off / ecc->page_size) * ecc->oob_size;
	else
		off = part_tab[i].off;
	printf("off real=%lx\n", off);
	img->mem += off;

	printf("Read partition:\n");
	fp = fopen(filename, "wb");
	if (fp == NULL) {
		fprintf(stderr, "Can't open file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	pages = nb_page = (part_tab[i].len + page_size - 1) / ecc->page_size;

	while (nb_page--) {
		memcpy(buf, img->mem, page_size);
		fwrite(buf, 1, page_size, fp);
		img->mem += page_size;
		if (flash_type == FLASH_TYPE_NAND)
			img->mem += ecc->oob_size;
	}
	printf("Read %d blocks at %ld\n", pages-nb_page, part_tab[i].off);

	fclose(fp);
}

/*
 * Write data to image file
 */
static void partition_write(struct image *img, const char *part_name, const char *filename)
{
	unsigned char *buf;
	unsigned char oob_buf[64];
	int i, nb_page, ret, pages;
	FILE *fp;
	unsigned long off;
	size_t part_len;
	struct stat _stat;

	buf = malloc(page_size);
	for(i=0;i<nb_part;i++) {
		if (!strcmp(part_tab[i].name, part_name)) {
			break;
		}
	}
	if (i==nb_part) return;

	printf("Partition %s found (0x%lx bytes @0x%lx)\n",
			part_name, part_tab[i].len, part_tab[i].off);

	pages = nb_page = (part_tab[i].len + page_size - 1) / page_size;

	if (flash_type == FLASH_TYPE_NAND) {
		off = part_tab[i].off + (part_tab[i].off / ecc->page_size) * ecc->oob_size;
		part_len = nb_page * (page_size + ecc->oob_size);
	} else {
		off = part_tab[i].off;
		part_len = nb_page * page_size;
	}
	printf("off real=%lx\n", off);

	if (img->size < off) {
		fprintf(stderr, "Error: image file too small\n");
		exit(EXIT_FAILURE);
	}
	if (img->size-off < part_len) {
		fprintf(stderr, "Error: partition too big\n");
		exit(EXIT_FAILURE);
	}

	ret = stat(filename, &_stat);
	printf("  st_size=%zd part_len=%zd\n", _stat.st_size, part_len);
	if (_stat.st_size > part_len) {
		fprintf(stderr, "Error: file too big\n");
		exit(EXIT_FAILURE);
	}

	printf("Erase partition\n");
	memset(img->mem + off, 0xFF, part_len);

	printf("Write partition:\n");
	fp = fopen(filename, "rb");
	if (fp == NULL) {
		fprintf(stderr, "Error: can't open file %s\n", filename);
		exit(EXIT_FAILURE);
	}

	while (1) {
		memset(buf, 0xFF, page_size);
		ret = fread(buf, 1, page_size, fp);
		if (ret <= 0) break;

		memcpy(img->mem + off, buf, page_size);
		off += page_size;

		if (flash_type == FLASH_TYPE_NAND) {
			oob(buf, ecc->page_size, oob_buf);
			memcpy(img->mem + off, oob_buf, ecc->oob_size);
			off += ecc->oob_size;
		}

		if (ret != page_size) break;
		nb_page--;
		if (nb_page == -1) {
			fprintf(stderr, "File %s to big for the partition %s\n",
						filename, part_tab[i].name);
			exit(EXIT_FAILURE);
		}
	}
	printf("Write %d blocks at %ld\n", pages-nb_page, part_tab[i].off);

	fclose(fp);
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [options]\n", name);
	printf("\t-v                    print version\n");
	printf("\t-s <size>             size of the image file\n");
	printf("\t-f <file>             image file\n");
	printf("\t-p <partition table file>\n");
	printf("\t-w <partition>,<file> write a partition\n");
	printf("\t-r <partition>,<file> read a partition\n");
	printf("\t-t <type>             flash type: nand or nor\n");
	printf("\t-z <page size>        page size of the NAND flash\n");
	printf("\t                      valid values are 256, 512 and 2048\n");
}

int main(int argc, char *argv[])
{
	int i;
	int opt;
	int fd_img;
	size_t len;
	char *filename = NULL, *p;
	struct image img;
	struct action act_tab[32];
	int nb_act;
	int err = 0;

	nb_act = 0;
	img.size = 0;

	while ((opt = getopt(argc, argv, "vs:f:p:w:r:t:z:")) != -1) {
		int retval;

		switch (opt) {
			case 'v':
				printf(PACKAGE_NAME " version " VERSION "\n");
				break;
			case 's':
				img.size = atoi(optarg);
				switch (optarg[strlen(optarg)-1]) {
					case 'K':
					case 'k':
						img.size *= 1024;
						break;
					case 'M':
					case 'm':
						img.size *= 1024*1024;
						break;
					case 'G':
					case 'g':
						img.size *= 1024*1024*1024;
						break;
				}
				printf("size img = %zd\n", img.size);
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
				retval = partition_file(optarg);
				if (retval != 0) err++;
				break;
			case 't':
				if (!strcmp(optarg, "nand"))
					flash_type = FLASH_TYPE_NAND;
				else if (!strcmp(optarg, "nor")) {
					flash_type = FLASH_TYPE_NOR;
					page_size = 4096;
				} else
					fprintf(stderr, "Wrong page size\n");
				break;
			case 'z':
				page_size = atoi(optarg);
				for(i=0;i<3;i++) {
					if (ecc_tab[i].page_size == page_size) {
						ecc = &ecc_tab[i];
						break;
					}
				}
				if (ecc == NULL) {
					err++;
					fprintf(stderr, "Wrong page size\n");
				}
				break;
			default: /* '?' */
				usage(argv[0]);
				err++;
		}
	}
	if (flash_type == FLASH_TYPE_NAND && ecc == NULL) {
		fprintf(stderr, "Missing page size for NAND flash\n");
		err++;
	}

	if (!filename) {
		fprintf(stderr, "Mising image file\n");
		err++;
	}

	if (err)
		return EXIT_FAILURE;

	fd_img = open(filename, O_CREAT | O_RDONLY, 0666);
	len = lseek(fd_img, 0, SEEK_END);

	if (img.size == 0)
		img.size = len;

	if (img.size == 0) {
		fprintf(stderr, "Error: image file is zero\n");
		return EXIT_FAILURE;
	}

	if (flash_type == FLASH_TYPE_NAND)
		img.size += img.size / page_size * ecc->oob_size;

	img.mem = malloc(img.size);
	if (img.mem == NULL) {
		fprintf(stderr, "Error: malloc\n");
		return EXIT_FAILURE;
	}

	printf("Flash type: %s\n", flash_type==FLASH_TYPE_NAND ? "NAND": "NOR");
	memset(img.mem, 0xFF, img.size);
	
	if (len) {
		printf("Read content file\n");
		lseek(fd_img, 0, SEEK_SET);
		read(fd_img, img.mem, img.size);
	}
	close(fd_img);

	fd_img = open(filename, O_TRUNC | O_RDWR, 0666);

	for(i=0;i<nb_act;i++) {
		putchar('\n');
		if (act_tab[i].action == 'w')
			partition_write(&img, act_tab[i].part, act_tab[i].file);
		else
			partition_read(&img, act_tab[i].part, act_tab[i].file);
	}

	write(fd_img, img.mem, img.size);
	close(fd_img);

	free(filename);

	return EXIT_SUCCESS;
}
