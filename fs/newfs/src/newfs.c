#define _XOPEN_SOURCE 700

#include "newfs.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options newfs_options;			 /* 全局选项 */
struct newfs_super super; 
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = newfs_init,						 /* mount文件系统 */		
	.destroy = newfs_destroy,				 /* umount文件系统 */
	.mkdir = newfs_mkdir,					 /* 建目录，mkdir */
	.getattr = newfs_getattr,				 /* 获取文件属性，类似stat，必须完成 */
	.readdir = newfs_readdir,				 /* 填充dentrys */
	.mknod = newfs_mknod,					 /* 创建文件，touch相关 */
	.write = NULL,								  	 /* 写入文件 */
	.read = NULL,								  	 /* 读文件 */
	.utimens = newfs_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,						  		 /* 改变文件大小 */
	.unlink = NULL,							  		 /* 删除文件 */
	.rmdir	= NULL,							  		 /* 删除目录， rm -r */
	.rename = NULL,							  		 /* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/

//read from driver
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = ROUND_DOWN(offset, IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = ROUND_UP((size + bias), IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_read(DRIVER(), cur, IO_SZ());
        cur          += IO_SZ();
        size_aligned -= IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}
//allocate datablk
int newfs_alloc_datablk() {
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int blk_cursor  = 0;
    bool is_find_free_entry = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < BLKS_SZ(super.data_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if(((int)(super.data_map[byte_cursor]) & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前blk_cursor位置空闲 */
                super.data_map[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            blk_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry  )
        return -NEWFS_ERROR_NOSPACE;

    return blk_cursor;
}
//allocate inode
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    bool is_find_free_entry = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < BLKS_SZ(super.ino_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if(((int)(super.ino_map[byte_cursor]) & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                super.ino_map[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry  )
        return -NEWFS_ERROR_NOSPACE;

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->ino = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    //inode->dentrys = NULL;
    
    for(int i=0;i<NEWFS_MAX_BLK_PER_FILE;++i) {
		inode->block_ptr[i] = -1;
	}

    return inode;
}
//write to driver
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = ROUND_DOWN(offset, IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = ROUND_UP((size + bias), IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_write(DRIVER(), cur, IO_SZ());
        cur          += IO_SZ();
        size_aligned -= IO_SZ();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}
//sync inode to disk recursively
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    uint32_t ino        = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    //memcpy(inode_d.target_path, inode->target_path, SFS_MAX_FILE_NAME);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset;
    /* 先写inode本身 */
    if (newfs_driver_write(INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    /* 再写inode下方的数据 */
    if (IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */                          
        dentry_cursor = inode->dentrys;
        int blk_id    = 0;
        offset        = 0;
        while (dentry_cursor != NULL)
        {
            memcpy(dentry_d.name, dentry_cursor->name, MAX_NAME_LEN);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            if (newfs_driver_write(DATA_OFS(inode->block_ptr[blk_id],offset), (uint8_t *)&dentry_d, 
                                 sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                return -NEWFS_ERROR_IO;                     
            }
            
            if (dentry_cursor->ino != NULL) {
                newfs_sync_inode(dentry_cursor->ino);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct newfs_dentry_d);
            if(offset >= super.blks_size) {
                offset = 0;
                blk_id++;
            }
        }
    }
    else if (IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接写即可 */
        int now_siz = 0;
        while(now_siz < inode->size) {
            int write_size = MIN(inode->size - now_siz, BLK_SZ());
            if (newfs_driver_write(DATA_OFS(inode->block_ptr[now_siz/BLK_SZ()], now_siz), inode->data + now_siz, write_size) != NEWFS_ERROR_NONE) {
                return -NEWFS_ERROR_IO;
            }
            now_siz += write_size;
        }
    }
    return NEWFS_ERROR_NONE;
}
// read inode from disk recursively
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    /* 从磁盘读索引结点 */
    if (newfs_driver_read(INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    //memcpy(inode->target_path, inode_d.target_path, SFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    /* 内存中的inode的数据或子目录项部分也需要读出 */
    if (IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
		struct dentry
        for (i = 0; i < dir_cnt; i++)
        {
            if (newfs_driver_read(DATA_OFS(ino) + i * sizeof(struct newfs_dentry_d), 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                return NULL;
            }
            sub_dentry = newfs_dentry(dentry_d.name, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            newfs_alloc_dentry(inode, sub_dentry);
        }
    }
    else if (IS_REG(inode)) {
        // inode->data = (uint8_t *)malloc(NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE));
        // if (newfs_driver_read(DATA_OFS(ino), (uint8_t *)inode->data, 
        //                     NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE)) != NEWFS_ERROR_NONE) {
        //     return NULL;                    
        // }
		//TODO : read inode data
    }
    return inode;
}
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* newfs_init(struct fuse_conn_info * conn_info) {
	/* TODO: 在这里进行挂载 */
	struct newfs_super_d sb_d;
	struct newfs_dentry* root_dentry;
	struct newfs_inode* root_inode;

	super.is_mounted = FALSE;
	bool init = FALSE;

	int driver_fd = ddriver_open(newfs_options.device);
	if(driver_fd < 0) {
		perror("Failed to open device");
		return NULL;
	}

	super.fd = driver_fd;

	root_dentry = newfs_dentry("/", DIR);

	if(newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t*)&sb_d, sizeof(struct newfs_super_d)) < NEWFS_ERROR_NONE) {
		perror("Failed to read superblock");
		return NULL;
	}
	if(sb_d.magic != NEWFS_MAGIC) {
		//calc memory layout
        sb_d.blks_size = 1024;
        sb_d.blks_nums = 4096;

        sb_d.sb_offset = 0;
        sb_d.sb_blks = 1;

        sb_d.ino_map_offset = sb_d.sb_offset + sb_d.sb_blks;
        sb_d.ino_map_blks = 1;

        sb_d.data_map_offset = sb_d.ino_map_offset + sb_d.ino_map_blks;
        sb_d.data_map_blks = 1;

        sb_d.ino_offset = sb_d.data_map_offset + sb_d.data_map_blks;
        sb_d.ino_blks = NEWFS_INODE_NUM;

        sb_d.data_offset = sb_d.ino_offset + sb_d.ino_blks;
        sb_d.data_blks = sb_d.blks_nums - (sb_d.sb_blks + sb_d.ino_map_blks + sb_d.data_map_blks + sb_d.ino_blks);

		init = TRUE;
    }

	// build in-mem 
	super.blks_nums = sb_d.blks_nums;
	super.blks_size = sb_d.blks_size;
	super.sb_offset = sb_d.sb_offset;
	super.sb_blks = sb_d.sb_blks;
	super.ino_map_offset = sb_d.ino_map_offset;
	super.ino_map_blks = sb_d.ino_map_blks;
	super.data_map_offset = sb_d.data_map_offset;
	super.data_map_blks = sb_d.data_map_blks;
	super.ino_offset = sb_d.ino_offset;
	super.ino_blks = sb_d.ino_blks;
	super.data_offset = sb_d.data_offset;
	super.data_blks = sb_d.data_blks;

    //init 2 maps
	super.ino_map = malloc(BLKS_SZ(sb_d.ino_map_blks));
	super.data_map = malloc(BLKS_SZ(sb_d.data_map_blks));

    // if not init ,read from disk
	if(!init) {
		if(newfs_driver_read(sb_d.ino_map_offset*sb_d.blks_size, (uint8_t*)super.ino_map, BLKS_SZ(sb_d.ino_map_blks)) < NEWFS_ERROR_NONE) {
			perror("Failed to read inode bitmap");
			return NULL;
		}
		if(newfs_driver_read(sb_d.data_map_offset*sb_d.blks_size, (uint8_t*)super.data_map, BLKS_SZ(sb_d.data_map_blks)) < NEWFS_ERROR_NONE) {
			perror("Failed to read data bitmap");
			return NULL;	
		}
	}

	// alloc root inode 
	if(init) {
		root_inode = newfs_alloc_inode(root_dentry);
		newfs_sync_inode(root_inode);
	}


	//read root inode
	root_inode = newfs_read_inode(root_dentry,NEWFS_ROOT_INO);
	root_dentry->inode = root_inode;
	super.root_dentry = root_dentry;
	super.is_mounted = TRUE;

	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void newfs_destroy(void* p) {
	/* TODO: 在这里进行卸载 */
	
	ddriver_close(super.fd);

	return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_mkdir(const char* path, mode_t mode) {
	/* TODO: 解析路径，创建目录 */
	return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param newfs_stat 返回状态
 * @return int 0成功，否则返回对应错误号
 */
int newfs_getattr(const char* path, struct stat * newfs_stat) {
	/* TODO: 解析路径，获取Inode，填充newfs_stat，可参考/fs/simplefs/sfs.c的sfs_getattr()函数实现 */
	return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */
    return 0;
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */
	return 0;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则返回对应错误号
 */
int newfs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int newfs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int newfs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 选做 */
	return size;			   
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int newfs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int newfs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则返回对应错误号
 */
int newfs_truncate(const char* path, off_t offset) {
	/* 选做 */
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则返回对应错误号
 */
int newfs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	newfs_options.device = strdup("TODO: 这里填写你的ddriver设备路径");

	if (fuse_opt_parse(&args, &newfs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}