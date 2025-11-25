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
        return NULL;

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
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
    memcpy(inode_d.block_ptr, inode->block_ptr, sizeof(inode->block_ptr));
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
            
            if (dentry_cursor->inode != NULL) {
                newfs_sync_inode(dentry_cursor->inode);
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
            if (newfs_driver_write(DATA_OFS(inode->block_ptr[now_siz/BLK_SZ()], now_siz % BLK_SZ()), inode->data + now_siz, write_size) != NEWFS_ERROR_NONE) {
                return -NEWFS_ERROR_IO;
            }
            now_siz += write_size;
        }
    }
    return NEWFS_ERROR_NONE;
}
//
int insert_dentry_to_inode(struct newfs_inode* inode,struct newfs_dentry* dentry) {
    if(inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    int bid = inode->size /super.blks_size;
    if(inode->block_ptr[bid] == -1) {
        inode->block_ptr[bid] = newfs_alloc_datablk();
    }
    inode->dir_cnt ++ ;
    inode->size += sizeof(struct newfs_dentry_d);
    return inode->dir_cnt;
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
    inode->dir_cnt = inode_d.dir_cnt;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    //memcpy(inode->target_path, inode_d.target_path, SFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for(int i=0;i<NEWFS_MAX_BLK_PER_FILE;++i) {
        inode->block_ptr[i] = inode_d.block_ptr[i];
    }
    /* 内存中的inode的数据或子目录项部分也需要读出 */
    if (IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        for (i = 0; i < dir_cnt; i++)
        {
            int blk_id = (i * sizeof(struct newfs_dentry_d)) / super.blks_size;
            if (newfs_driver_read(DATA_OFS(inode->block_ptr[blk_id],i*sizeof(struct newfs_dentry_d)), 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                return NULL;
            }
            sub_dentry = newfs_dentry(dentry_d.name, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            //insert_dentry_to_inode(inode, sub_dentry);
            if(inode->dentrys == NULL) {
                inode->dentrys = sub_dentry;
            }else {
                sub_dentry->brother = inode->dentrys;
                inode->dentrys = sub_dentry;
            }
        }
    }
    else if (IS_REG(inode)) {
        inode->data = (uint8_t*)malloc(inode->size);
        int now_siz = 0;
        while(now_siz < inode->size) {
            int read_size = MIN(inode->size - now_siz, BLK_SZ());
            if (newfs_driver_read(DATA_OFS(inode->block_ptr[now_siz/BLK_SZ()], now_siz % BLK_SZ()), (uint8_t*)(inode->data + now_siz), read_size) != NEWFS_ERROR_NONE) {
                return NULL;
            }
            now_siz += read_size;
        }
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

        sb_d.sz_usage = 0;
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

    super.sz_usage = sb_d.sz_usage;
    //init 2 maps
	super.ino_map = malloc(BLKS_SZ(sb_d.ino_map_blks));
    memset(super.ino_map,0,BLKS_SZ(sb_d.ino_map_blks));
	super.data_map = malloc(BLKS_SZ(sb_d.data_map_blks));
    memset(super.data_map,0,BLKS_SZ(sb_d.data_map_blks));

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
	} else {
        //read root inode
	    root_inode = newfs_read_inode(root_dentry,NEWFS_ROOT_INO);
	    root_dentry->inode = root_inode;
    }

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
	if(!super.is_mounted) {
        return ;
    }

    newfs_sync_inode(super.root_dentry->inode);

    struct newfs_super_d sb_d; 

    sb_d.magic = NEWFS_MAGIC;
    sb_d.blks_size = super.blks_size;
    sb_d.blks_nums = super.blks_nums;
    sb_d.sb_offset = super.sb_offset;
    sb_d.sb_blks = super.sb_blks;
    sb_d.ino_map_offset = super.ino_map_offset;
    sb_d.ino_map_blks = super.ino_map_blks;
    sb_d.data_map_offset = super.data_map_offset;
    sb_d.data_map_blks = super.data_map_blks;
    sb_d.ino_offset = super.ino_offset;
    sb_d.ino_blks = super.ino_blks;
    sb_d.data_offset = super.data_offset;
    sb_d.data_blks = super.data_blks;
    sb_d.sz_usage = super.sz_usage;

    //write sb_d

    if(newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t*)&sb_d, sizeof(struct newfs_super_d)) < NEWFS_ERROR_NONE) {
        perror("Failed to write superblock");
        return;
    }

    //write 2 maps

    if(newfs_driver_write(super.ino_map_offset*super.blks_size, (uint8_t*)super.ino_map, BLKS_SZ(super.ino_map_blks)) < NEWFS_ERROR_NONE) {
        perror("Failed to write inode bitmap");
        return;
    }

    if(newfs_driver_write(super.data_map_offset*super.blks_size, (uint8_t*)super.data_map, BLKS_SZ(super.data_map_blks)) < NEWFS_ERROR_NONE) {
        perror("Failed to write data bitmap");
        return;	
    }

    free(super.ino_map);
    free(super.data_map);

	ddriver_close(super.fd);

	return;
}
int newfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}
struct newfs_dentry* newfs_lookup(const char * path, bool* is_find, bool* is_root) {
    struct newfs_dentry* dentry_cursor = super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    bool is_hit;
    char* fname = NULL;
    //char* path_cpy = (char*)malloc(sizeof(path));
    char* path_cpy = strdup(path);
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (IS_REG(inode) && lvl < total_lvl) {
            perror(" not a dir\n");
            dentry_ret = inode->dentry;
            break;
        }
        if (IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)   /* 遍历子目录项 */
            {
                if (strcmp(fname, dentry_cursor->name) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                perror(" not found\n");
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
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
    bool is_find = FALSE,is_root = FALSE;
    char* fname;
    struct newfs_dentry* parent_dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_dentry* new_dentry;
    struct newfs_inode*  inode;  
    if(is_find) {
        return -NEWFS_ERROR_EXISTS;
    }
    if(IS_REG(parent_dentry->inode)) {
        return -NEWFS_ERROR_UNSUPPORTED;
    }

    fname = newfs_get_fname(path);
    new_dentry = newfs_dentry(fname, DIR);
    new_dentry->parent = parent_dentry;
    inode = newfs_alloc_inode(new_dentry);
    insert_dentry_to_inode(parent_dentry->inode, new_dentry);
	return NEWFS_ERROR_NONE;
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
    bool is_find = FALSE,is_root = FALSE;
    struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);

    if(!is_find) {
        return -NEWFS_ERROR_NOTFOUND;
    }

    struct newfs_inode* inode = dentry->inode;
    if(IS_DIR(inode)) {
        newfs_stat->st_mode = S_IFDIR | NEWFS_DEFAULT_PERM;
        newfs_stat->st_size = inode->size;
    } else if(IS_REG(inode)) {
        newfs_stat->st_mode = S_IFREG | NEWFS_DEFAULT_PERM;
        newfs_stat->st_size = inode->size;
    }

    newfs_stat->st_nlink = 1;
    newfs_stat->st_uid   = getuid();
    newfs_stat->st_gid   = getgid();
    newfs_stat->st_atime = time(NULL);
    newfs_stat->st_mtime = time(NULL);
    newfs_stat->st_blksize = BLK_SZ();

    if(is_root) {
        newfs_stat->st_size = super.sz_usage;
        newfs_stat->st_blocks = super.blks_nums;
        newfs_stat->st_nlink = 2;
    }
	return 0;
}
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
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
    bool is_find = FALSE,is_root = FALSE;
    int cur_dir = offset;

    struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_dentry* sub_dentry;
	struct newfs_inode* inode;

    if(is_find) {
        inode = dentry->inode;
        sub_dentry = newfs_get_dentry(inode,cur_dir);
        if(sub_dentry) {
            filler(buf,sub_dentry->name,NULL,cur_dir+1);
        }
        return NEWFS_ERROR_NONE;
    }

    return -NEWFS_ERROR_NOTFOUND;
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
    bool is_find = FALSE,is_root = FALSE;

    struct newfs_dentry* parent_dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_dentry* new_dentry;
    struct newfs_inode*  inode;
    char* fname;

    if(is_find) {
        return -NEWFS_ERROR_EXISTS;
    }

    fname = newfs_get_fname(path);
    if(S_ISREG(mode)) {
        new_dentry = newfs_dentry(fname, REG);
    } else if(S_ISDIR(mode)) {
        new_dentry = newfs_dentry(fname,DIR);
    }

    new_dentry->parent = parent_dentry;
    inode = newfs_alloc_inode(new_dentry);
    printf("parent ino:%d\n",parent_dentry->ino);
    fprintf(stderr, "parent ino: %d\n", parent_dentry->ino);
    insert_dentry_to_inode(parent_dentry->inode, new_dentry);

	return NEWFS_ERROR_NONE;
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
	return NEWFS_ERROR_NONE;
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