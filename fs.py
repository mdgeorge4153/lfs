from ctypes import *

c_disk_addr     = c_long

bytes_per_block   = 1024
bytes_per_address = sizeof(c_disk_addr)
addrs_per_block   = bytes_per_block // bytes_per_address

## Inode structure #############################################################

class FileMetadata(Structure):
    _fields_ = [("size",        c_long),
                ("modified",    c_long),
                ("permissions", c_int)]

num_sindirect = 10
num_dindirect = 10
num_tindirect = 1

class Indirects(Structure):
    _fields_ = [("single",   c_disk_addr*num_sindirect),
                ("double",   c_disk_addr*num_dindirect),
                ("triple",   c_disk_addr*num_tindirect)
               ]

# fill up the inode with direct blocks
num_direct    = (bytes_per_block - sizeof(FileMetadata) - sizeof(Indirects)) // sizeof(c_disk_addr)

class DataBlock(Structure):
    _fields_ = [("data",  c_char*bytes_per_block)]

class IndirectBlock(Structure):
    _fields_ = [("block", c_disk_addr*addrs_per_block)]

class InodeBlock(Structure):
    _fields_ = [("metadata", FileMetadata),
                ("direct",   c_disk_addr*num_direct),
                ("indirect", Indirects)]

class Block(Union):
    _fields_ = [("inode",    InodeBlock),
                ("indirect", IndirectBlock),
                ("data",     DataBlock)
               ]


max_data_blocks_per_file = \
  num_direct + \
  num_sindirect * addrs_per_block + \
  num_dindirect * addrs_per_block * addrs_per_block + \
  num_tindirect * addrs_per_block * addrs_per_block

max_filesize = \
  max_data_blocks_per_file * bytes_per_block

################################################################################

num_inodes         = 1 << 20

class BlockIdentifier(Structure):
    """For each of these, the maximum value indicates that there is no such thing.
    For example, the inode itself has all fields at maximum value (except the
    inode number)"""
    _fields_ = [("inode_num",        c_int, int.bit_length(num_inodes)),
                ("sindir_block_num", c_int, int.bit_length(num_sindirect)),
                ("dindir_block_num", c_int, int.bit_length(num_dindirect)),
                ("tindir_block_num", c_int, int.bit_length(num_tindirect)),
                ("direct_block_num", c_int, int.bit_length(addrs_per_block))]

class Segment(Structure):
    _fields_ = [("blocks", Block*blocks_per_segment),
                ("segment_table", BlockIdentifier*blocks_per_segment)]

################################################################################

def datanum_to_blockid(inode_num, block_num):
    result = BlockIdentifier()
    result.inode_num = inode_num
    result.tindir_block_num = num_tindirect
    result.dindir_block_num = num_dindirect
    result.sindir_block_num = num_sindirect

    # block is a direct block
    if block_num < num_direct_blocks:
        result.direct_block_num = block_num
        return result

    # data is in single indirect section
    block_num -= num_direct_blocks
    if block_num < num_sindirect * addrs_per_block:
        result.sindir_block_num, rest = divmod(block_num, addrs_per_block)
        result.direct_block_num = rest
        return result

    # data is in double indirect section
    block_num -= num_sindirect * addrs_per_block
    if block_num < num_dindirect * addrs_per_block * addrs_per_block:
        rest, result.direct_block_num = divmod(block_num, addrs_per_block)
        rest, result.sindir_block_num = divmod(rest,      addrs_per_block)
        result.dindir_block_num = rest
        return result

    # data is in triple indirect section
    block_num -= num_dindirect * addrs_per_block * addrs_per_block
    if block_num < num_tindirect * addrs_per_block * addrs_per_block * addrs_per_block:
        rest, result.direct_block_num = divmod(block_num, addrs_per_block)
        rest, result.sindir_block_num = divmod(rest,      addrs_per_block)
        rest, result.dindir_block_num = divmod(rest,      addrs_per_block)
        result.tindir_block_num = rest
        return result

################################################################################

blocks_per_segment = 1024  # NOTE: not including segment table!

class Filesystem(object):
    def __init__(self):
        self.segment   = Segment.from_bytes(disk, offset)
        self.contents  = dict()
        self.next      = 0

    def read(self, inode, position, buf, length):
        pass

    def write(self, inode, position, buf, length):
        pass

    def create(self, inode):
        pass

    def __read_block(self, blockid):
        inode_map = self.contents[0]

    def __get_datablock(self, inode, block_num):
        pass

# vim: ts=4 sw=4 ai et

