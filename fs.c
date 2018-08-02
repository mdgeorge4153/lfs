#include <unistd.h>
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
#define BLOCK_OFFSET_BITS   12
#define INODE_NUMBER_BITS   24

#define SEGMENTS_PER_DISK  (1 << SEGMENT_NUMBER_BITS)
#define BLOCKS_PER_SEGMENT (1 << BLOCK_NUMBER_BITS)
#define BYTES_PER_BLOCK    (1 << BLOCK_OFFSET_BITS)
#define INODES_PER_FS      (1 << INODE_NUMBER_BITS)

/** Logical and physical addresses ********************************************/

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
	bool non_null       : 1;
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

block_id_t parent_id(block_id_t addr) {
	addr.depth--;
	return addr;
}

/** Disk organization *********************************************************/

/**
 * the disk is divided up into a bunch of segments and a superblock; each
 * segment contains a bunch of blocks and a segment table.
 */

typedef union block {
	char bytes[BYTES_PER_BLOCK];
} block_t;

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

/** Managing the logical blocks on the disk ***********************************/

/**
 * A block_addr is how we store a physical address on disk: it indicates a
 * segment number and block within that segment.
 */
typedef struct block_addr {
	bool non_null        : 1;
	unsigned int segment : SEGMENT_NUMBER_BITS;
	unsigned int block   : BLOCK_NUMBER_BITS;
} block_addr_t;

#define ADDRS_PER_BLOCK (BYTES_PER_BLOCK/sizeof(block_addr_t)) 

typedef union indirect_block {
	block_addr_t addrs[ADDRS_PER_BLOCK];
} indirect_block_t;

/** returns a block_t */
block_t* lookup(block_addr_t addr) {
	return &the_disk->segments[addr.segment].blocks[addr.block];
}

/** get block address.  Only used for debugging */
block_addr_t debug_location(block_t *block) {
	block_addr_t result;

	if (block == NULL) {
		result.non_null = false;
		return result;
	}

	off_t offset = (void *) block - (void *) the_disk;
	result.segment = offset / sizeof(segment_t);
	result.block   = offset % sizeof(segment_t) / sizeof(block_t);
	return result;
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

	indirect_block_t *parent = (indirect_block_t*) find(parent_id(id));

	if (parent == NULL)
		return NULL;

	block_addr_t addr = parent->addrs[id.layers[id.depth]];
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

	indirect_block_t *parent = (indirect_block_t *) touch(parent_id(id));

	result.non_null = true;
	result.segment  = next_segment;
	result.block    = next_block++;

	// TODO: flush if full

	block_addr_t old_addr = parent->addrs[id.layers[id.depth]];

	if (old_addr.non_null)
		memcpy(lookup(result), lookup(old_addr), sizeof(block_t));
	else
		memset(lookup(result), 0, sizeof(block_t));

	the_disk->segments[result.segment].segment_table[result.block] = id;
	parent->addrs[id.layers[id.depth]] = result;

	return lookup(result);
}

/** Inodes ********************************************************************/

/** These constants should be chosen so that sizeof(inode_t) <= sizeof(block_t) */
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

int main (int argc, char** argv) {
	initialize("disk.lfs", true);

	block_id_t root_id;
	root_id.non_null = true;
	root_id.depth    = 0;

	block_t *root  = find(root_id); 
	block_addr_t loc = debug_location(root);
	printf("root:  s%i  b%i \n", loc.segment, loc.block);
	printf("*root: \"%s\"\n", root->bytes);
	printf("\n");

	root = touch(root_id);
	loc  = debug_location(root);
	printf("root:  s%i  b%i \n", loc.segment, loc.block);
	printf("*root:    %s\n", root->bytes);
	printf("\n");

	strcpy(root->bytes, "hello world\0");
	sync();

	return 0;
}

/** IO ************************************************************************/

void fail(char *msg) {
	printf("error: %s\n", msg);
	printf("quitting.");
	exit(1);
}


