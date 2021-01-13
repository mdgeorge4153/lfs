#ifndef __FILES_H__
#define __FILES_H__

#include <stdbool.h>

typedef unsigned int inode_num_t;

/**
 * Copy data from the given file into the provided buffer.  Fails if the file
 * does not exist.  Returns true on success.
 */
extern bool lfs_read   (inode_num_t file, char *buf, int pos, int len);

/**
 * Copy data from the provided buffer into the given file.  The file is created
 * and/or extended as necessary.  Returns true on success.
 */
extern bool lfs_write  (inode_num_t file, char *buf, int pos, int len);

#endif
