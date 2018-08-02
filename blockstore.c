#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>

#include "types.h"

/** Disk organization *********************************************************/

/**
 * the disk is divided up into a bunch of segments and a superblock; each
 * segment contains a bunch of blocks and a segment table.
 */

typedef struct superblock {
	unsigned int current_segment : SEGMENT_NUMBER_BITS;
	unsigned int last_free       : SEGMENT_NUMBER_BITS;
} superblock_t;

/* we maintain the invariant that the first block in every segment is the root
 * inode map (i.e. segment_table[0].depth = 0)
 */
typedef struct segment {
	block_id_t segment_table [BLOCKS_PER_SEGMENT];
	block_t    blocks        [BLOCKS_PER_SEGMENT];
} segment_t;

typedef struct disk {
	segment_t    segments[SEGMENTS_PER_DISK];
	superblock_t superblock;
} disk_t;

disk_t       *the_disk;
unsigned int next_block;
unsigned int next_segment;

/** Managing the logical blocks on the disk ***********************************/

/** returns a block_t */
block_t* lookup(block_addr_t addr) {
	return &the_disk->segments[addr.segment].blocks[addr.block];
}

/**
 * If the block identified by id is in the next segment, set result to its
 * location and return true.  Otherwise return false.
 */
bool is_dirty(block_id_t id, block_addr_t* result) {
	block_id_t *segment_table = the_disk->segments[next_segment].segment_table;
	for (int i = 0; i < BLOCKS_PER_SEGMENT; i++) {
		if (!segment_table[i].non_null)
			return false;
		if (block_id_eq(segment_table[i],id)) {
			result->segment = next_segment;
			result->block   = i;
			return true;
		}
	}

	return false;
}

/**
 * Return a pointer to the given block, or NULL if the block doesn't exist
 */
block_t *find(block_id_t id) {
	block_addr_t result;

	if (is_dirty(id, &result))
		return lookup(result);

	block_t *parent = find(parent_id(id));


	if (parent == NULL)
		return NULL;

	block_addr_t addr = parent->indirect[id.layers[id.depth]];

	if (addr.non_null)
		return lookup(addr);
	else
		return NULL;
}

/**
 * Ensure that the given block (and all ancestors) are in the cache, and return
 * a pointer to its location.  Creates nonexistent blocks as necessary.
 */
block_t *touch(block_id_t id) {
	block_addr_t result;

	if (is_dirty(id, &result))
		return lookup(result);

	block_t *parent = touch(parent_id(id));

	result.non_null = true;
	result.segment  = next_segment;
	result.block    = next_block++;

	// TODO: flush if full

	block_addr_t old_addr = parent->indirect[id.layers[id.depth]];

	if (old_addr.non_null)
		memcpy(lookup(result), lookup(old_addr), sizeof(block_t));
	else
		memset(lookup(result), 0, sizeof(block_t));

	the_disk->segments[result.segment].segment_table[result.block] = id;
	parent->indirect[id.layers[id.depth]] = result;

	return lookup(result);
}

/** Managing the data on the actual disk **************************************/ 

/**
 * We use memory mapped I/O to access the contents of the disk.  In this way,
 * the operating system manages the cache for us.  As we access blocks, the
 * virtual memory system will load them into memory for us; and it will also
 * take care of evicting them when necessary.
 *
 * We use memory segmentation (via the mprotect system call) to mark the disk
 * as read-only, except for the next segment we are writing.  This enforces
 * the append-only discipline.
 */

void sync() {
	// write current segment 
	msync(&the_disk->segments[next_segment], sizeof(segment_t), MS_SYNC);
	mprotect(&the_disk->segments[next_segment], sizeof(segment_t), PROT_READ);

	// update the superblock
	mprotect(&the_disk->superblock, sizeof(superblock_t), PROT_READ | PROT_WRITE);
	the_disk->superblock.current_segment = next_segment;
	msync(&the_disk->superblock, sizeof(superblock_t), MS_SYNC);
	mprotect(&the_disk->superblock, sizeof(superblock_t), PROT_READ);

	// set up the next segment
	// TODO: hope the disk isn't full
	unsigned int old_segment = next_segment;
	next_segment = (next_segment + 1) % SEGMENTS_PER_DISK;
	next_block = 1;
	mprotect(&the_disk->segments[next_segment], sizeof(segment_t), PROT_READ | PROT_WRITE);

	// copy in inode map root
	memcpy(&the_disk->segments[next_segment].blocks[0],
	       &the_disk->segments[old_segment].blocks[0],
	       sizeof(block_t));

	// set up segment table
	block_id_t *segment_table = the_disk->segments[next_segment].segment_table;
	memset(segment_table, 0, sizeof(block_id_t)*BLOCKS_PER_SEGMENT);
	segment_table[0].non_null  = 1;
	segment_table[0].depth = 0;
}

void initialize(char *disk_name, bool format) {
	int fd = open(disk_name, O_CREAT | O_RDWR, 0600);
	lseek(fd, sizeof(disk_t) - 1, SEEK_SET);
	write(fd, "", 1);

	the_disk = mmap(NULL, sizeof(disk_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (format)
		next_segment = SEGMENTS_PER_DISK - 1;
	else
		next_segment = the_disk->superblock.current_segment;
	sync();
}

