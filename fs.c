#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>

#include <sys/mman.h>

/** Forward declarations ******************************************************/

void fail(char *msg);

/** Disk organization *********************************************************/

#define SEGMENT_NUMBER_BITS 16
#define BLOCK_NUMBER_BITS   10
#define BLOCK_OFFSET_BITS   10
#define INODE_NUMBER_BITS   24

#define SEGMENTS_PER_DISK  (1 << SEGMENT_NUMBER_BITS)
#define BLOCKS_PER_SEGMENT (1 << BLOCK_NUMBER_BITS)
#define BYTES_PER_BLOCK    (1 << BLOCK_OFFSET_BITS)
#define INODES_PER_FS      (1 << INODE_NUMBER_BITS)

/** Logical and physical addresses ********************************************/

/**
 * A block_addr is a physical address on disk: it indicates a segment number
 * and block within that segment
 */
typedef struct block_addr {
	bool non_null;
	unsigned int segment : SEGMENT_NUMBER_BITS;
	unsigned int block   : BLOCK_NUMBER_BITS;
} block_addr_t;


/**
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
	bool non_null;
	unsigned char depth : 3;
	unsigned char layers[7]; // inode # / inode # / inode # / triple # / double # / single # / data #
} block_id_t;

/**
 * return 1 if a and b represent the same block
 */
bool block_id_eq(block_id_t a, block_id_t b) {
	if (!a.non_null || !b.non_null)
		return false;
	if (a.depth != b.depth)
		return false;
	for (int i = 0; i < a.depth; i++)
		if (a.layers[i] != b.layers[i])
			return false;
	return true;
}


/** Disk organization *********************************************************/

/**
 * the disk is divided up into a bunch of segments and a superblock; each
 * segment contains a bunch of blocks and a segment table.
 */

#define ADDRS_PER_BLOCK (BYTES_PER_BLOCK/sizeof(block_addr_t))

typedef char          data_block_t[BYTES_PER_BLOCK];
typedef block_addr_t indir_block_t[ADDRS_PER_BLOCK];

typedef struct superblock {
	unsigned int current_segment : SEGMENT_NUMBER_BITS;
	unsigned int last_free       : SEGMENT_NUMBER_BITS;
} superblock_t;

/* we maintain the invariant that the first block in every segment is the root
 * inode map (i.e. segment_table[0].depth = 0)
 */
typedef struct segment {
	block_id_t   segment_table [BLOCKS_PER_SEGMENT];
	data_block_t blocks        [BLOCKS_PER_SEGMENT];
} segment_t;

typedef struct disk {
	segment_t    segments[SEGMENTS_PER_DISK];
	superblock_t superblock;
} disk_t;

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

disk_t       *the_disk;
unsigned int  next_segment;
unsigned int  next_block;

void sync() {
	// write current segment 
	msync(&the_disk->segments[next_segment], sizeof(segment_t), MS_SYNC);
	mprotect(&the_disk->segments[next_segment], sizeof(segment_t), PROT_READ);

	// update the superblock
	mprotect(&the_disk->superblock, sizeof(superblock_t), PROT_READ | PROT_WRITE);
	the_disk->superblock.current_segment = next_segment;
	msync(&the_disk->superblock, sizeof(superblock_t), MS_ASYNC);
	mprotect(&the_disk->superblock, sizeof(superblock_t), PROT_READ);

	// set up the next segment
	// TODO: hope the disk isn't full
	unsigned int old_segment = next_segment;
	next_segment = (next_segment + 1) % SEGMENTS_PER_DISK;
	next_block = 1;
	mprotect(&the_disk->segments[next_segment], sizeof(segment_t), PROT_READ | PROT_WRITE);
	
	// copy in inode map root
	memcpy(&the_disk->segments[old_segment].blocks[0],
	       &the_disk->segments[next_segment].blocks[0],
	       sizeof(data_block_t));

	// set up segment table
	block_id_t *segment_table = the_disk->segments[next_segment].segment_table;
	memset(segment_table, 0, sizeof(block_id_t)*BLOCKS_PER_SEGMENT);
	segment_table[0].non_null  = 1;
	segment_table[0].depth = 0;
}

void initialize(char *disk_name, bool format) {
	int fd = open(disk_name, O_CREAT);
	the_disk = mmap(NULL, sizeof(disk_t), PROT_READ, MAP_SHARED, fd, 0);

	if (format)
		the_disk->superblock.current_segment = SEGMENTS_PER_DISK - 1;

	next_segment = the_disk->superblock.current_segment;
	sync();
}

char *lookup(block_addr_t addr) {
	if (!addr.non_null)
		fail("tried to access null disk address");
	return the_disk->segments[addr.segment].blocks[addr.block];
}

block_addr_t *lookup_indirect(block_addr_t addr) {
	return (block_addr_t*) lookup(addr);
}

/**
 * If the block identified by id is in the next segment, set result to its
 * location and return true.  Otherwise return false.
 */
bool dirty(block_id_t id, block_addr_t* result) {
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
 * Return the address of the given block.  If touch is true, ensure that the
 * returned address is in the next segment, along with all blocks on the path
 * to it.
 *
 * This may cause a sync if there is not enough space in the cache.
 */
block_addr_t find(block_id_t id, bool touch) {
	/* return immediately if cached */
	block_addr_t result;

	if (dirty(id, &result))
		return result;

	/* make sure there's space in the cache if necessary */
	if (touch && next_block + id.depth >= BLOCKS_PER_SEGMENT)
		sync();

	/* look up this address in the parent */
	block_id_t parent_id = id;
	parent_id.depth--;

	block_addr_t parent = find(parent_id, touch);
	result = lookup_indirect(parent)[id.layers[id.depth]];

	if (!touch)
		return result;

	/* if touching, copy this block into cache and update the parent */ 
	block_addr_t new_block;
	new_block.segment = next_segment;
	new_block.block   = next_block++;
	if (result.non_null)
		memcpy(lookup(result), lookup(new_block), sizeof(data_block_t));
	else
		memset(lookup(new_block), 0, sizeof(data_block_t));
	lookup_indirect(parent)[id.layers[id.depth]] = new_block;
	return new_block;
}


/** Inodes ********************************************************************/

/** These constants should be chosen so that sizeof(inode_t) <= sizeof(data_block_t) */
#define N_DIRECT    100
#define N_SINDIRECT 10
#define N_DINDIRECT 10
#define N_TINDIRECT 1

typedef struct inode {
	block_addr_t blocks[N_DIRECT + N_SINDIRECT + N_DINDIRECT + N_TINDIRECT];

	long     size;
	// other metadata: permissions, modified time, etc.
} inode_t;

typedef unsigned int inode_num_t;

/**
 * Given the logical block number of a data block within a file,
 * output the id of the block within that file.  For example, this might
 * translate data block number 372 of file 8 to the 15th direct block inside
 * the second single indirect block of file 8.
 */
block_id_t datanum_to_block_id(inode_num_t inode, unsigned long block_num) {
	if (inode > INODES_PER_FS)
		fail("inode number out of range");

	block_id_t result;
	result.non_null = true;
	result.layers[0] = inode >> 16 & 0xff;
	result.layers[1] = inode >> 8  & 0xff;
	result.layers[2] = inode >> 0  & 0xff;

	int offset   = 0;

	if (block_num < N_DIRECT) {
		result.depth = 4;
		result.layers[0] = offset + block_num;
		return result;
	}

	block_num -= N_DIRECT;
	offset    += N_DIRECT;
	if (block_num < N_SINDIRECT * ADDRS_PER_BLOCK) {
		result.depth     = 5;
		result.layers[0] = block_num / ADDRS_PER_BLOCK + offset;
		result.layers[1] = block_num % ADDRS_PER_BLOCK;
		return result;
	}

	block_num -= N_SINDIRECT * ADDRS_PER_BLOCK;
	offset    += N_SINDIRECT;
	if (block_num < N_DINDIRECT * ADDRS_PER_BLOCK * ADDRS_PER_BLOCK) {
		result.depth = 6;
		result.layers[0] = block_num / ADDRS_PER_BLOCK / ADDRS_PER_BLOCK + offset;
		result.layers[1] = block_num / ADDRS_PER_BLOCK % ADDRS_PER_BLOCK;
		result.layers[2] = block_num % ADDRS_PER_BLOCK;
		return result;
	}

	block_num -= N_DINDIRECT * ADDRS_PER_BLOCK * ADDRS_PER_BLOCK;
	offset    += N_DINDIRECT;
	if (block_num < N_TINDIRECT * ADDRS_PER_BLOCK * ADDRS_PER_BLOCK * ADDRS_PER_BLOCK) {
		result.depth = 7;
		result.layers[0] = block_num / ADDRS_PER_BLOCK / ADDRS_PER_BLOCK / ADDRS_PER_BLOCK + offset;
		result.layers[1] = block_num / ADDRS_PER_BLOCK / ADDRS_PER_BLOCK % ADDRS_PER_BLOCK;
		result.layers[2] = block_num / ADDRS_PER_BLOCK % ADDRS_PER_BLOCK;
		result.layers[3] = block_num % ADDRS_PER_BLOCK;
		return result;
	}

	// beyond max file size
	result.non_null = false;
	return result;
}

/** User API ******************************************************************/

void create(inode_num_t file, long size) {

}

void format() {

}

void read(inode_num_t file, long offset, long length, char buffer[length]) {
	
}

void write(inode_num_t file, long offset, long length, char buffer[length]) {

}

void delete(inode_num_t file) {

}

/** IO ************************************************************************/

void fail(char *msg) {
	printf("error: %s\n", msg);
	printf("quitting.");
	exit(1);
}
