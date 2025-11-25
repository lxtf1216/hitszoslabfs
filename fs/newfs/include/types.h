#ifndef _TYPES_H_
#define _TYPES_H_

#define MAX_NAME_LEN    128  

typedef int                 bool;
//macro
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NEWFS_MAGIC_NUM           0x52415453  
#define NEWFS_SUPER_OFS           0
#define NEWFS_ROOT_INO            0



#define NEWFS_ERROR_NONE          0
#define NEWFS_ERROR_ACCESS        EACCES
#define NEWFS_ERROR_SEEK          ESPIPE     
#define NEWFS_ERROR_ISDIR         EISDIR
#define NEWFS_ERROR_NOSPACE       ENOSPC
#define NEWFS_ERROR_EXISTS        EEXIST
#define NEWFS_ERROR_NOTFOUND      ENOENT
#define NEWFS_ERROR_UNSUPPORTED   ENXIO
#define NEWFS_ERROR_IO            EIO     /* Error Input/Output */
#define NEWFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NEWFS_MAX_FILE_NAME       128
#define NEWFS_INODE_PER_FILE      1
#define NEWFS_DATA_PER_FILE       16
#define NEWFS_DEFAULT_PERM        0777

#define NEWFS_IOC_MAGIC           'S'
#define NEWFS_IOC_SEEK            _IO(NEWFS_IOC_MAGIC, 0)

#define NEWFS_FLAG_BUF_DIRTY      0x1
#define NEWFS_FLAG_BUF_OCCUPY     0x2

#define NEWFS_MAX_BLK_PER_FILE    2048
#define NEWFS_INODE_NUM 585
//macro funcs
#define IO_SZ()                     512
#define BLK_SZ()                    1024
#define DISK_SZ()                   (super.sz_disk)
#define DRIVER()                    (super.fd)

#define ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

#define BLKS_SZ(blks)               ((blks) * BLK_SZ())

#define INO_OFS(ino)                ((super.ino_offset+(ino))*super.blks_size)
#define DATA_OFS(blk_id,off)               ((super.data_offset + blk_id)*super.blks_size + off)

#define IS_DIR(pinode)              (pinode->dentry->ftype == DIR)
#define IS_REG(pinode)              (pinode->dentry->ftype == REG)
//#define IS_SYM_LINK(pinode)         (pinode->dentry->ftype == SFS_SYM_LINK)

#define MIN(a,b)                    ((a)<(b)?(a):(b))
typedef enum {
    REG,
    DIR
}FILE_TYPE;

struct custom_options {
	const char*        device;
};

struct newfs_super {
    uint32_t magic;
    int      fd;
    bool is_mounted;
    /* TODO: Define yourself */

    int blks_size;
    int blks_nums;

    int sb_offset;
    int sb_blks;

    uint8_t* ino_map;
    int ino_map_offset;
    int ino_map_blks;

    uint8_t* data_map;
    int data_map_offset;
    int data_map_blks;

    int ino_offset;
    int ino_blks;

    int data_offset;
    int data_blks;

    struct newfs_dentry* root_dentry;

    int sz_usage;

};
// 8*4B = 32B 1024/32*29 = 928 >> 
struct newfs_inode {
    uint32_t ino;
    /* TODO: Define yourself */
    int size;

    FILE_TYPE ftype;

    int block_ptr[NEWFS_MAX_BLK_PER_FILE];

    int dir_cnt;

    struct newfs_dentry* dentry;
    struct newfs_dentry* dentrys;

    uint8_t* data;
};

struct newfs_dentry {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    FILE_TYPE ftype;
    
    struct newfs_dentry* brother;
    struct newfs_inode* inode;
    struct newfs_dentry* parent;
    /* TODO: Define yourself */
};

static inline struct newfs_dentry* newfs_dentry(char *fname,FILE_TYPE ftype) {
    struct newfs_dentry* dentry = malloc(sizeof(struct newfs_dentry));
    strncpy(dentry->name, fname, MAX_NAME_LEN);
    dentry->ftype = ftype;
    dentry->ino = -1;
    dentry->inode = NULL;
    return dentry;
}
struct newfs_super_d {
    uint32_t magic;

    int blks_size;
    int blks_nums;

    int sb_offset;
    int sb_blks;

    int ino_map_offset;
    int ino_map_blks;

    int data_map_offset;
    int data_map_blks;

    int ino_offset;
    int ino_blks;

    int data_offset;
    int data_blks;

    int sz_usage;
};
struct newfs_inode_d {
    uint32_t ino;
    /* TODO: Define yourself */

    int size;

    FILE_TYPE ftype;

    int block_ptr[NEWFS_MAX_BLK_PER_FILE];

    int dir_cnt;

};
struct newfs_dentry_d {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    FILE_TYPE ftype;
    /* TODO: Define yourself */
};
#endif /* _TYPES_H_ */