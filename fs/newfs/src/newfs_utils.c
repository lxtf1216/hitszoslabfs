#include "../include/newfs.h"

extern struct newfs_super super;
extern struct custom_options newfs_options;

int newfs_mount(struct custom_options* options) {
    newfs_options = *options;

    super.fd = ddriver_open(newfs_options.device);
    if (super.fd < 0) {
        perror("Failed to open device");
        return -1;
    }

    // Initialize superblock and other structures here
    if(NEWFS_MAGIC != super.magic) {
        super.magic = NEWFS_MAGIC;
        super.blks_size = 1024;
        super.blks_nums = 4096;

        super.sb_offset = 0;
        super.sb_blks = 1;

        super.ino_map_offset = super.sb_offset + super.sb_blks;
        super.ino_map_blks = 1;

        super.data_map_offset = super.ino_map_offset + super.ino_map_blks;
        super.data_map_blks = 1;

        super.ino_offset = super.data_map_offset + super.data_map_blks;
        super.ino_blks = 29;

        super.data_offset = super.ino_offset + super.ino_blks;
        super.data_blks = super.blks_nums - (super.sb_blks + super.ino_map_blks + super.data_map_blks + super.ino_blks);
    }

    return 0;
}