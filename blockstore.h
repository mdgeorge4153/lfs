#ifndef __BLOCKSTORE_H__
#define __BLOCKSTORE_H__

#include "types.h"

extern void sync();
extern void initialize(char *disk_name, bool format);

extern block_t *find(block_id_t id);
extern block_t *touch(block_id_t id);

#endif
