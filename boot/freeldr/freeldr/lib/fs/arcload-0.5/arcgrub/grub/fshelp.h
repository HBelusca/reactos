/* fshelp.h -- Filesystem helper functions */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2004, 2005  Free Software Foundation, Inc.
 *
 *  GRUB is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GRUB_FSHELP_HEADER
#define GRUB_FSHELP_HEADER	1

#include <grub/disk.h>

typedef struct grub_fshelp_node *grub_fshelp_node_t;

enum grub_fshelp_filetype {
    GRUB_FSHELP_UNKNOWN,
    GRUB_FSHELP_REG,
    GRUB_FSHELP_DIR,
    GRUB_FSHELP_SYMLINK
};

grub_err_t grub_fshelp_find_file (const char *path,
				    grub_fshelp_node_t rootnode,
				    grub_fshelp_node_t *foundnode,
				    int (*iterate_dir) (grub_fshelp_node_t dir,
							int
							(*hook) (const char *filename,
								 enum grub_fshelp_filetype filetype,
								 grub_fshelp_node_t node)),
				    char *(*read_symlink) (grub_fshelp_node_t node),
				    enum grub_fshelp_filetype expect);


grub_ssize_t grub_fshelp_read_file (grub_disk_t disk, grub_fshelp_node_t node,
				    void (*read_hook) (unsigned long sector,
						       unsigned offset,
						       unsigned length),
				    int pos, unsigned int len, char *buf,
				    int (*get_block) (grub_fshelp_node_t node,
						      int block),
				    unsigned int filesize, int log2blocksize);

unsigned int grub_fshelp_log2blksize (unsigned int blksize,
				      unsigned int *pow);

#endif /* ! GRUB_FSHELP_HEADER */
