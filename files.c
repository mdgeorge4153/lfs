#include <string.h>
#include <stdio.h>

#include "files.h"
#include "types.h"
#include "blockstore.h"

/**
 * Given the logical block number of a data block within a file,
 * output the id of the block within that file.  For example, this might
 * translate data block number 372 of file 8 to the 15th direct block inside
 * the second single indirect block of file 8.
 */
block_id_t datanum_to_block_id(inode_num_t inode, unsigned long block_num) {

	block_id_t result;
	if (inode > INODES_PER_FS) {
		result.non_null = false;
		return result;
	}
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

/**
 * helper function for read and write
 */
bool access (inode_num_t file, char *buf, int pos, int len, bool write) {
	if (len == 0)
		return true;

	int block_num = pos / BYTES_PER_BLOCK;
	int offset    = pos % BYTES_PER_BLOCK;
	block_id_t block_id = datanum_to_block_id (file, block_num);

	if (!block_id.non_null)
		return false;

	block_t *block = write ? touch(block_id) : find(block_id);

	if (block == NULL)
		return false;

	int avail = BYTES_PER_BLOCK - offset;
	if (len < avail)
		avail = len;

	if (write)
		memcpy(block, buf, avail);
	else
		memcpy(buf, block, avail);

	return access(file, buf + avail, pos + avail, len - avail, write);
}

bool lfs_read  (inode_num_t file, char *buf, int pos, int len) {
	return access(file, buf, pos, len, false);
}

bool lfs_write (inode_num_t file, char *buf, int pos, int len) {
	return access(file, buf, pos, len, true);
}



