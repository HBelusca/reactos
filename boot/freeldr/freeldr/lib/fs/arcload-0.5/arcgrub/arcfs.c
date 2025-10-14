#include <grub/err.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/misc.h>
#include <grub/disk.h>
#include <grub/dl.h>
#include <grub/types.h>

#include <arclib/arc.h>

#ifndef GRUB_UTIL
static grub_dl_t my_mod;
#endif

struct grub_arcfs_data {
	ULONG id;
	ULONG pos;
};

static grub_err_t grub_arcfs_dir(grub_device_t device, const char *path, 
	int (*hook) (const char *filename, int dir))
{
	/* not supported here */
	return GRUB_ERR_NONE;
}

static grub_err_t grub_arcfs_open(struct grub_file *file, const char *name)
{
	struct grub_arcfs_data *data=grub_malloc(sizeof(struct grub_arcfs_data));
	FILEINFORMATION info;

	if(ArcOpen((char *)name, OpenReadOnly, &(data->id)) != ESUCCESS) {
		grub_error(GRUB_ERR_FILE_NOT_FOUND, "ArcOpen failed");
		return grub_errno;
	}
	data->pos = 0;
	ArcGetFileInformation(data->id, &info);
	file->size = info.EndingAddress.LowPart;
	file->data = data;

	return GRUB_ERR_NONE;
}


static grub_ssize_t grub_arcfs_read(grub_file_t file, char *buf, grub_ssize_t len)
{
	struct grub_arcfs_data *data=file->data;
	LARGEINTEGER li;
	ULONG n = len, c, p = 0, stat;

	if(data->pos != file->offset) {
		li.HighPart = 0;
		li.LowPart = file->offset;
		ArcSeek(data->id, &li, SeekAbsolute);
		data->pos = file->offset;
	}

	while(n > 0) {
		if((stat = ArcRead(data->id, (buf + p), n, &c)) != ESUCCESS) {
			grub_error(GRUB_ERR_READ_ERROR, "ArcRead failed with error %u", stat);
			return -1;
		}

		if(!c)
			return p;

		p += c;
		n -= c;
		data->pos += c;
	}
	return p;
}


static grub_err_t grub_arcfs_close(grub_file_t file)
{
	struct grub_arcfs_data *data=file->data;

	ArcClose(data->id);
	return GRUB_ERR_NONE;
}


static grub_err_t grub_arcfs_label(grub_device_t device __attribute ((unused)),
	char **label __attribute ((unused)))
{
	return GRUB_ERR_NONE;
}

static struct grub_fs grub_arcfs_fs={
	.name = "*arcfs",
	.dir = grub_arcfs_dir,
	.open = grub_arcfs_open,
	.read = grub_arcfs_read,
	.close = grub_arcfs_close,
	.label = grub_arcfs_label,
	.next = 0};

GRUB_MOD_INIT(arcfs)
{
	grub_fs_register (&grub_arcfs_fs);
#ifndef GRUB_UTIL
	my_mod = mod;
#endif
}

GRUB_MOD_FINI(arcfs)
{
	grub_fs_unregister (&grub_arcfs_fs);
}
