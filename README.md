Simple LFS implementation
=========================

This project contains a simple implementation of a log-structured filesystem; it
was developed as a demonstration for CS 4410.  The focus is on the disk
organization, so the user interface is very simple: there are only methods
`lfs_write` and `lfs_read`; to create a file, you call write with a new inode
number.

In addition to showing how the data is laid out in a log-structured filesystem,
this example also shows how memory-mapped I/O can be used to efficiently manage
large and complex data structures on disk.  There is no code for managing the
buffer cache and encoding blocks to disk, because the buffer cache is managed
by the host operating system's memory management, and the in-memory data
structures are identical to those on disk.

Several features are unimplemented:

 - There is currently no support for a hierarchical file structure; directories
   would be implemented on top of this interface by encoding lists of files in
   a data structure stored in a file.  To support this, INodes contain a
   metadata section.

 - There is currently no support for file removal or compaction.  The necessary
   metadata for tracking compaction is stored in the file system, but it is
   currently unused.

Code organization
-----------------

There are three modules:

 - `types.h/c` specifies the sizes and types of various disk data structures,
   including blocks, block identifiers, and inodes.  There are two types of
   block addresses defined here: a `block_id` is a kind of logical address,
   identifying the logical position of the block within the filesystem.  A
   `block_addr` is a kind of physical address, identifying the location of
   the block on the physical disk.

 - `blockstore.h/c` contains the meat of the project: it manages the underlying
   organization of blocks on the physical disk.  This is where the
   memory-mapped file is set up, and where the underlying segment data
   structures are maintained.

 - `files.h/c` implements the rudimentary user interface.  This is where the
   INode structure is maintained.

The remaining file, `main.c`, just contains an entry point and some code to
play around with the filesystem.

Building and running
--------------------

Just run `make`, which will output the `main` executable.

Note that the disk image is stored as a large file with holes; running the
program on a host filesystem that doesn't support holes (like HFS+) will create
a very large file, and take a long time to intiialize.

