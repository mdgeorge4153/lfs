#ifndef __BLOCKSTORE_H__
#define __BLOCKSTORE_H__

#include "types.h"

/** Ensure that all contents of the buffer cache are stored on disk */
extern void sync();

/**
 * Prepare the disk and in-memory data structures.  If format is true, then
 * the disk will be formatted.  Otherwise, it will be loaded from a file with
 * the given name.
 */
extern void initialize(char *disk_name, bool format);

/** Return a pointer to the given block, or NULL if the block doesn't exist.  */
extern block_t *find(block_id_t id);

/** 
* Ensure that the given block (and all ancestors) are in the cache, and return
* a pointer to its location.  Creates nonexistent blocks as necessary.
*/
extern block_t *touch(block_id_t id);

#endif
