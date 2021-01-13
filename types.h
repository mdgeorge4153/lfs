#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdbool.h>

#define SEGMENT_NUMBER_BITS 16
#define BLOCK_NUMBER_BITS   10
#define BLOCK_OFFSET_BITS   12
#define INODE_NUMBER_BITS   24

#define BYTES_PER_BLOCK    (1 << BLOCK_OFFSET_BITS)
#define BLOCKS_PER_SEGMENT (1 << BLOCK_NUMBER_BITS)
#define SEGMENTS_PER_DISK  (1 << SEGMENT_NUMBER_BITS)
#define INODES_PER_FS      (1 << INODE_NUMBER_BITS)

/*******************************************************************************
 * A block_id is a logical address within the filesystem.
 *
 * For example, a block might be the 42nd direct block pointed to by the second
 * indirect block inside of file #37.  In this case, layers[0..3] would contain
 * the inode number, layers[4] would contain the offset of the second indirect
 * block in the inode, and layers[5] would contain the offset of the 42nd
 * direct block in the indirect block.
 *
 * the depth indicates how far to traverse the tree: only layers[0..depth-1] are
 * used.
 */
typedef struct block_id {
	bool non_null       : 1;
	unsigned char depth : 3;
	unsigned char layers[7]; // inode # / inode # / inode # / triple # / double # / single # / data #
} block_id_t;

/** Compare two block ids for equality */
extern bool block_id_eq(block_id_t a, block_id_t b);

/** Return the id of the block that references this block */
extern block_id_t parent_id(block_id_t id);

/** Output the block id in a human-readable format */
extern void print_block_id(block_id_t id);

/*******************************************************************************
 * A block_addr is how we store a physical address on disk: it indicates a
 * segment number and block within that segment.
 */
typedef struct block_addr {
	bool non_null        : 1;
	unsigned int segment : SEGMENT_NUMBER_BITS;
	unsigned int block   : BLOCK_NUMBER_BITS;
} block_addr_t;


/*******************************************************************************
 * An INode is part indirect block, part data.  The first N_DIRECT pointers
 * point to data; the next S_INDIRECT point to single indirect blocks, and so
 * on.
 */

#define N_DIRECT    100
#define N_SINDIRECT 10
#define N_DINDIRECT 10
#define N_TINDIRECT 1

typedef enum filetype {
	DIRECTORY,
	NORMAL,
	LINK
} filetype_t;

typedef struct metadata {
	unsigned long size;
	filetype_t    type;
	unsigned int  permissions;
	unsigned int  owner;
	unsigned int  group;
	unsigned long modified;
	unsigned long created;
} metadata_t;

typedef struct inode {
	// blocks must be first; we cast this to an indirect block for easy lookup
	block_addr_t blocks[N_DIRECT + N_SINDIRECT + N_DINDIRECT + N_TINDIRECT];
	metadata_t metadata;
} inode_t;

/*******************************************************************************
 * A block is either data, an indirect block (which contains the addresses of
 * other blocks), or an inode (which has some of both).
 */
#define ADDRS_PER_BLOCK (BYTES_PER_BLOCK / sizeof(block_addr_t))

typedef union block {
	char         data     [BYTES_PER_BLOCK];
	block_addr_t indirect [ADDRS_PER_BLOCK];
	inode_t      inode;
} block_t;

#endif
