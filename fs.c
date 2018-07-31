#include <sys/mman.h>

typedef struct block_addr {
	unsigned int segment : 16;
	unsigned int block   : 16;
} block_addr_t;
	
typedef char inode_num_t[3];

#define BYTES_PER_BLOCK    1024
#define BYTES_PER_ADDRESS  sizeof(block_addr_t)
#define ADDRS_PER_BLOCK    (BYTES_PER_BLOCK / BYTES_PER_ADDRESS)
#define SEGMENTS_PER_DISK  (1 << 16)
// NOTE: does not include segment table!
#define BLOCKS_PER_SEGMENT 1024


#define N_DIRECT    100
#define N_SINDIRECT 10
#define N_DINDIRECT 10
#define N_TINDIRECT 1

typedef struct inode {
	block_addr_t blocks[N_DIRECT + N_SINDIRECT + N_DINDIRECT + N_TINDIRECT];

	long     size;
	// other metadata: permissions, modified time, etc.
} inode_t;

typedef char block_t[BYTES_PER_BLOCK];

void fail(char *msg) {
	printf("error: %s\n", msg);
	printf("quitting.");
	exit(1);
}

/** Block identifiers **********************************************************
 *
 * A block_id_t describes a logical block within a file's inode structure.  Each
 * block that makes up a file has a distinct block id that indicates whether it
 * is an inode, a data block, or an indirect block, and where it is positioned
 * in the tree.
 */

typedef struct block_id {
	bool used;
	unsigned char depth : 3;
	unsigned char layers[7]; // inode # / inode # / inode # / triple # / double # / single # / data #
} block_id_t;

bool block_id_eq(block_id a, block_id b) {
	if (!a.used || !b.used)
		return false;
	if (a.depth != b.depth)
		return false;
	for (int i = 0; i < a.depth; i++)
		if (a.layers[i] != b.layers[i])
			return false;
	return true;
}

/**
 * Given the logical block number of a data block within a file,
 * output the id of the block within that file.
 */
block_id_t datanum_to_block_id(inode_num_t inode, long block_num) {
	block_id_t result;
	result.used = true;
	memcpy(result.layers,inode,sizeof(inode_num_t));

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
	result.depth = 8;
	return result;
}

/** Filesystem ****************************************************************/

typedef struct superblock {
	unsigned int current_segment : 16;
	unsigned int last_free       : 16;
} superblock_t;

typedef struct segment {
	block_id_t segment_table [BLOCKS_PER_SEGMENT];
	block_t    blocks        [BLOCKS_PER_SEGMENT];
} segment_t;


typedef struct disk {
	segment_t    segments[SEGMENTS_PER_DISK];
	superblock_t superblock;
} disk_t;

/******************************************************************************/

disk_t       *the_disk;
unsigned int  next_segment;
unsigned int  next_block;

block_t *lookup(block_addr_t addr) {
	return the_disk->segments[addr.segment].blocks[addr.block];
}

block_addr_t[ADDRS_PER_BLOCK] lookup_indirect(block_addr_t addr) {
	return (block_addr_t[]) lookup(addr);
}

bool dirty(block_id_t id, block_addr_t* result) {
	block_id_t[] segment_table = the_disk->segments[next_segment].segment_table;
	for (int i = 0; i < BLOCKS_PER_SEGMENT; i++)
		if (!segment_table[i].used)
			return false;
		if (block_id_eq(segment_table[i],id)) {
			result.segment = next_superblock.current_segment;
			result.block   = i;
			return true;
		}

	return false;
}

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

	block_addr_t result = lookup_indirect(parent)[id.layers[id.depth]];

	if (!touch)
		return result;

	/* if touching, copy this block into cache and update the parent */ 
	block_addr_t new_block;
	new_block.segment = next_segment;
	new_block.block   = next_block++;
	memcpy(lookup(result), lookup(new_block), sizeof(block_t));
	lookup_indirect(parent)[id.layers[id.depth]] = new_block;
	return new_block;
}

/** User API ******************************************************************/

void initialize(char *disk_name) {
	int fd = open(disk_name, O_CREAT | O_LARGEFILE);
	the_disk = mmap(NULL, sizeof(disk_t), PROT_READ, MAP_SHARED, fd, 0);

	next_segment = the_disk->superblock.current_segment;
	sync();
}

void create(inode_num_t file, long size) {

}

void format() {

}

void 

void read(inode_num_t file, long offset, long length, char buffer[length]) {
	
}

void write(inode_num_t file, long offset, long length, char buffer[length]) {

}

void delete(inode_num_t file) {

}

void sync() {
	// write current segment 
	msync(&the_disk.segments[next_segment], sizeof(segment_t), MS_SYNC);
	mprotect(&the_disk.segments[next_segment], sizeof(segment_t), PROT_READ);

	// update the superblock
	mprotect(&the_disk->superblock, sizeof(superblock_t), PROT_READ | PROT_WRITE);
	the_disk->superblock.current_segment = next_segment;
	msync(&the_disk.superblock, sizeof(superblock_t), MS_ASYNC);
	mprotect(&the_disk->superblock, sizeof(superblock_t), PROT_READ);

	// set up the next segment
	// TODO: hope the disk isn't full
	old_segment = next_segment;
	next_segment = (next_segment + 1) % SEGMENTS_PER_DISK;
	next_block = 1;
	mprotect(&the_disk.segments[next_segment], sizeof(segment_t), PROT_READ | PROT_WRITE);
	
	// copy in inode map root
	memcpy(&the_disk.segments[old_segment].blocks[0],
	       &the_disk.segments[next_segment].blocks[0],
	       sizeof(block_t));
	block_id_t segment_table[] = the_disk->segments[new_segment].segment_table;

	// set up segment table
	memzero(segment_table, sizeof(segment.segment_table));
	segment_table[0].used  = true;
	segment_table[0].depth = 0;
}

