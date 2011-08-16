/*
 *
 *
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>



struct image {
	char *mem;
	size_t size;
};

struct action {
	char *part;
	char *file;
	char action;
};

static void oob(const char *buf, size_t len, unsigned char *check)
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
		int ret = fscanf(fp, "%s %i %i", name, &len, &off);
		if (ret != 3) break;
		part_tab[idx].name = strdup(name);
		part_tab[idx].off = off;
		part_tab[idx].len = len;
		printf("%s\t0x%08x\t0x%08x\n", name, off, len);
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

	printf("Partion %s found (0x%xbytes @0x%x)\n", part_name, part_tab[i].len, part_tab[i].off);
	n = i;

	off = part_tab[i].off + part_tab[i].off / 512 * 16;
	printf("off real=%x\n", off);
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
		img += 512+16;
	}
	printf("Read %d blocks at %d\n", _nb-nb, part_tab[i].off);

	fclose(fp);
}

static void partition_write(char *img, const char *part_name, const char *filename)
{
	char buf[512+16];
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

	printf("Partion %s found (0x%xbytes @0x%x)\n", part_name, part_tab[i].len, part_tab[i].off);
	n = i;

	off = part_tab[i].off + (part_tab[i].off / 512) * 16;
	printf("off real=%x\n", off);

	printf("Erase partion\n");
	img += off;
	memset(img, 0xff, part_tab[i].len);

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

		oob(buf, 512, buf+512);

		memcpy(img, buf, 512+16);
		img += 512+16;
		if (ret != 512) break;
		nb--;
		if (nb == -1) {
			printf("File %s to big for the partion %s\n", filename, part_tab[i].name);
			exit(EXIT_FAILURE);
		}
	}
	printf("Write %d blocks at %d\n", _nb-nb, part_tab[i].off);

	fclose(fp);
}


int main(int argc, char *argv[])
{
	unsigned char buf[512];
	unsigned char check1[16], check2[16];
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

	while ((opt = getopt(argc, argv, "m:i:p:w:r:")) != -1) {
		switch (opt) {
			case 'm':
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
				printf("size img = %d\n", img_size);
				break;
			case 'i':
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
			default: /* '?' */
				fprintf(stderr, "Usage: %s [-t nsecs] [-n] name\n",
						argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (!filename) {
		printf("Mising image file\n");
		return EXIT_FAILURE;
	}

	fd_img = open(filename, O_CREAT | O_RDONLY, 0666);
	len = lseek(fd_img, 0, SEEK_END);
	printf("len=%d\n",len);

	if (img_size == 0)
		img_size = len;

	if (img_size == 0) {
		printf("Image file is zero\n");
		return EXIT_FAILURE;
	}
	img = malloc(img_size);
printf("malloc(%d) %p\n", img_size, img);
	if (img == NULL) {
		printf("Error malloc\n");
	}

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
//	memset(img+0x200, 0xff, 16);

	write(fd_img, img, img_size);
	close(fd_img);

	free(filename);

	return EXIT_SUCCESS;
}
