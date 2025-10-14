#ifndef ARCGRUB_H
#define ARCGRUB_H

#include <stdint.h>
#include <arclib/stdio.h>
#include <arclib/string.h>
#include <arclib/stdlib.h>

typedef int8_t grub_int8_t;
typedef int16_t grub_int16_t;
typedef int32_t grub_int32_t;
typedef int64_t grub_int64_t;
typedef uint8_t grub_uint8_t;
typedef uint16_t grub_uint16_t;
typedef uint32_t grub_uint32_t;
typedef uint64_t grub_uint64_t;
typedef long grub_ssize_t;
typedef unsigned long grub_size_t;
#define grub_be_to_cpu16(x) x
#define grub_be_to_cpu32(x) x
#define grub_be_to_cpu64(x) x
#define grub_cpu_to_be16(x) x
#define grub_cpu_to_be32(x) x
#define grub_cpu_to_be64(x) x
grub_uint16_t grub_le_to_cpu16(grub_uint16_t x);
grub_uint32_t grub_le_to_cpu32(grub_uint32_t x);
grub_uint64_t grub_le_to_cpu64(grub_uint64_t x);

typedef enum
{
    GRUB_ERR_NONE = 0,
    GRUB_ERR_TEST_FAILURE,
    GRUB_ERR_BAD_MODULE,
    GRUB_ERR_OUT_OF_MEMORY,
    GRUB_ERR_BAD_FILE_TYPE,
    GRUB_ERR_FILE_NOT_FOUND,
    GRUB_ERR_FILE_READ_ERROR,
    GRUB_ERR_BAD_FILENAME,
    GRUB_ERR_UNKNOWN_FS,
    GRUB_ERR_BAD_FS,
    GRUB_ERR_BAD_NUMBER,
    GRUB_ERR_OUT_OF_RANGE,
    GRUB_ERR_UNKNOWN_DEVICE,
    GRUB_ERR_BAD_DEVICE,
    GRUB_ERR_READ_ERROR,
    GRUB_ERR_WRITE_ERROR,
    GRUB_ERR_UNKNOWN_COMMAND,
    GRUB_ERR_INVALID_COMMAND,
    GRUB_ERR_BAD_ARGUMENT,
    GRUB_ERR_BAD_PART_TABLE,
    GRUB_ERR_UNKNOWN_OS,
    GRUB_ERR_BAD_OS,
    GRUB_ERR_NO_KERNEL,
    GRUB_ERR_BAD_FONT,
    GRUB_ERR_NOT_IMPLEMENTED_YET,
    GRUB_ERR_SYMLINK_LOOP,
    GRUB_ERR_BAD_GZIP_DATA,
} grub_err_t;
extern grub_err_t grub_errno;
grub_err_t grub_error(grub_err_t n, const char *fmt, ...);

#define GRUB_DISK_SECTOR_SIZE	0x200
#define GRUB_DISK_SECTOR_BITS	9
struct grub_disk {
  const char *name;
  void (*read_hook) (unsigned long sector, unsigned offset, unsigned length);
  void *data;
};
typedef struct grub_disk *grub_disk_t;

struct grub_device {
  struct grub_disk *disk;
  void *net;
};
typedef struct grub_device *grub_device_t;

struct grub_file;
struct grub_fs {
  const char *name;
  grub_err_t (*dir) (grub_device_t device, const char *path,
		     int (*hook) (const char *filename, int dir));
  grub_err_t (*open) (struct grub_file *file, const char *name);
  grub_ssize_t (*read) (struct grub_file *file, char *buf, grub_ssize_t len);
  grub_err_t (*close) (struct grub_file *file);
  grub_err_t (*label) (grub_device_t device, char **label);
  struct grub_fs *next;
};
typedef struct grub_fs *grub_fs_t;

struct grub_file {
  grub_device_t device;
  grub_fs_t fs;
  grub_ssize_t offset;
  grub_ssize_t size;
  void *data;
  void (*read_hook) (unsigned long sector, unsigned offset, unsigned length);
};
typedef struct grub_file *grub_file_t;
void grub_fs_register(grub_fs_t fs);
grub_err_t grub_disk_read(grub_disk_t disk, unsigned long sector,
	unsigned long offset, unsigned long size, char *buf);

#define grub_memcpy memcpy
#define grub_memset memset
#define grub_malloc malloc
#define grub_realloc realloc
#define grub_free free
#define grub_strncat strncat
#define grub_strchr strchr
#define grub_strrchr strrchr
#define grub_strcpy strcpy
#define grub_strncpy strncpy
#define grub_strcmp strcmp
#define grub_strncmp strncmp
#define grub_strdup strdup
#define grub_strndup strndup
#define grub_strlen strlen
grub_uint8_t *grub_utf16_to_utf8 (grub_uint8_t *dest, grub_uint16_t *src,
		    grub_size_t size);
char grub_tolower(char x);
int grub_isspace(char x);

typedef void *grub_dl_t;
#define grub_dl_ref(x)
#define grub_dl_unref(x)
#define GRUB_MOD_INIT(name)	void grub_mod_init_##name(grub_dl_t mod)
#define GRUB_MOD_FINI(name)	void grub_mod_fini_##name(grub_dl_t mod)
#define NESTED_FUNC_ATTR

#define grub_fs_unregister(x)

#endif
