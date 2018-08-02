#ifndef __FILES_H__
#define __FILES_H__

#include <stdbool.h>

typedef unsigned int inode_num_t;

extern bool lfs_read   (inode_num_t file, char *buf, int pos, int len);
extern bool lfs_write  (inode_num_t file, char *buf, int pos, int len);

#endif
