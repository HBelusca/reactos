/*
 * ARCload (c) 2005 Stanislaw Skowronek
 */

#include <arclib/stdlib.h>
#include <arclib/string.h>
#include <arclib/stdio.h>
#include <arclib/arc.h>

#include <arcgrub/grub/arcgrub.h>

#include "block.h"

struct dev_inst {
	char *path;
	struct grub_disk disk;
	struct grub_device device;
	struct grub_file file;
	struct dev_inst *next;
	int refcnt;
};
typedef struct dev_inst *dev_inst_t;
static dev_inst_t dev_inst_head = NULL;

static grub_fs_t grub_fs_by_name(char *name);
INT bopen(BDEV *dev, CHAR *path)
{
	dev_inst_t i;
	char *lbra, *rbra, *dpath, *fpath;
	grub_fs_t fs;
	BDEV fsdev;

	for(i=dev_inst_head;i;i=i->next)
		if(!strcmp(i->path, path))
			break;
	if(!i) {
		lbra = strrchr(path, '[');
		rbra = strrchr(path, ']');
		if(!lbra && !rbra) {
			fs=grub_fs_by_name("*arcfs");
			dpath=NULL;
			fpath=path;
		} else if(!lbra || !rbra || (rbra < lbra)) {
			printf("Malformed path: '%s'.\n", path);
			return -1;
		} else {
			*rbra=0;
			fs=grub_fs_by_name(lbra+1);
			if(!fs) {
				printf("No such filesystem: '[%s]'.\n", lbra+1);
				*rbra=']';
				return -1;
			}
			*rbra=']';
			if(lbra>path) {
				dpath=strndup(path, lbra-path);
			} else {
				if(fs->name[0]!='*') {
					printf("Path '%s' contains a non-root filesystem at root.\n", path);
					return -1;
				}
				dpath=NULL;
			}
			fpath=rbra+1;
		}

		i = malloc(sizeof(struct dev_inst));
		i->next = dev_inst_head;
		dev_inst_head = i;
		i->refcnt = 0;

		i->path = strdup(path);
		i->disk.name = i->path;
		i->disk.data = i;
		i->device.disk = &(i->disk);
		i->device.net = NULL;
		if(dpath) {
			if(bopen(&fsdev, dpath))
				return -1;
			i->file.device = &(((dev_inst_t)fsdev.file)->device);
		} else
			i->file.device = NULL;
		i->file.fs = fs;
		i->file.offset = 0;
		i->file.size = 0;
		i->file.read_hook = NULL;

		if(fs->open(&(i->file), fpath)!=GRUB_ERR_NONE)
			return -1;
	}
	dev->pos = 0;
	dev->file = i;
	i->refcnt++;

	return 0;
}

VOID bseek(BDEV *dev, ULONG pos)
{
	dev->pos = pos;
}

INT bread(BDEV *dev, ULONG cnt, VOID *buf)
{
	INT count;
	dev_inst_t i = dev->file;
	i->file.offset = dev->pos;
	if(i->file.device && i->file.size-i->file.offset<cnt)
		cnt = i->file.size-i->file.offset;
	count = i->file.fs->read(&(i->file), buf, cnt);
	dev->pos += count;
	return count;
}

void bclose(BDEV *dev)
{
	/* We close only files on arcfs. It is pointless to close anything,
	   it's just that ARCS dies when Ethernet is opened twice. Oh well. */
	dev_inst_t i = dev->file;
	i->refcnt--;
	if(i->refcnt<=0 && !i->file.device) {
		i->file.fs->close(&(i->file));
		free(i->path);
		i->path = strdup("[*error]");
		i->disk.name = i->path;
	}
}

/* filesystem & disk management */

grub_err_t grub_disk_read(grub_disk_t disk, unsigned long sector,
	unsigned long offset, unsigned long size, char *buf)
{
	dev_inst_t i = disk->data;
	INT read;

	i->file.offset = sector*GRUB_DISK_SECTOR_SIZE + offset;
	if((read=i->file.fs->read(&(i->file), buf, size)) != size) {
		grub_error(GRUB_ERR_OUT_OF_RANGE, "disk read failed - %d", read);
		return grub_errno;
	}
	return GRUB_ERR_NONE;
}

static grub_fs_t grub_fs_head=NULL;
static grub_fs_t grub_fs_by_name(char *name)
{
	grub_fs_t i;
	for(i=grub_fs_head;i;i=i->next)
		if(!strcmp(i->name, name))
			return i;
	return NULL;
}
void grub_fs_register(grub_fs_t fs)
{
	fs->next = grub_fs_head;
	grub_fs_head = fs;
}

#define GRUB_CALL_INIT(name) void grub_mod_init_##name(grub_dl_t mod);
#include <arcgrub/fslist.h>
#undef GRUB_CALL_INIT

void binitfs(void)
{
#define GRUB_CALL_INIT(name) grub_mod_init_##name(NULL);
#include <arcgrub/fslist.h>
#undef GRUB_CALL_INIT
}
