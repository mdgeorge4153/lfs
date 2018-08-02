#include <stdio.h>
#include <string.h>
#include "files.h"
#include "blockstore.h"


/** Inodes ********************************************************************/

int main (int argc, char** argv) {
	initialize("disk.lfs", false);

	char buf[30];

	if (lfs_read(17, buf, 100000000, 30))
		printf("%s\n", buf);

	lfs_write(17, "hello world\0", 100000000, 30);
	sync();

	return 0;
}

