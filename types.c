#include "types.h"
#include <stdio.h>

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

void print_block_id(block_id_t id) {
	for (int i = 0; i < id.depth; i++)
		printf("%i >", id.layers[i]);
}

