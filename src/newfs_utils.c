#include "../include/newfs.h"

extern struct newfs_super      super; 
extern struct custom_options newfs_options;

/*
1. need dump map
2. need to calculate the blocks

*/

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
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
/**
 * @brief find a free data block
 * 
 * @return int: the number of the datablock
 */
int newfs_alloc_datab(struct newfs_inode * inode)
{
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int data_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    if (inode->data_blk_cnt == NFS_DATA_PER_FILE)
    {
        return -NFS_ERROR_NOSPACE;
    }

    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.ino_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.data_map[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                super.data_map[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            data_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || data_cursor >= super.data_blks) {
        return -NFS_ERROR_NOSPACE;
    }

    inode->block_pointer[inode->data_blk_cnt++] = data_cursor;
    return data_cursor;
}
/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    // printf("%d\n", NFS_BLK_SZ());
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(NFS_DRIVER(), cur, NFS_IO_SZ());
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(NFS_DRIVER(), cur, NFS_IO_SZ());
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}
/**
 * @brief 将denry插入到inode中，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    inode->size += sizeof(struct newfs_dentry);

    return inode->dir_cnt;
}
/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 一个目录的索引结点
 * @param dentry 该目录下的一个目录项
 * @return int 
 */
int newfs_drop_dentry(struct newfs_inode * inode, struct newfs_dentry * dentry) {
    boolean is_find = FALSE;
    struct newfs_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find) {
        return -NFS_ERROR_NOTFOUND;
    }
    inode->dir_cnt--;
    return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.ino_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.ino_map[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
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


    if (!is_find_free_entry || ino_cursor >= super.ino_max)
        return -NFS_ERROR_NOSPACE;

    printf("ino_cursor = %d\n", ino_cursor);

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    inode->dentrys = NULL;
    
    inode->dir_cnt = 0;

    inode->data_blk_cnt = 0;

    for(int i = 0; i < NFS_DATA_PER_FILE; i ++) {
        inode->block_pointer[i] = -1; // not alloc
        inode->dirty[i] = 0;
    }
    
    if (NFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(NFS_BLKS_SZ(NFS_DATA_PER_FILE));
    }

    return inode;
}
/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int offset;
    int blk_cnt;

    // newfs_dump_imap();

    /* 再写inode下方的数据 */  // need change
    if (NFS_IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */                          
        dentry_cursor = inode->dentrys;
        blk_cnt = 0;
        if(inode->block_pointer[blk_cnt] == -1)
        {
            newfs_alloc_datab(inode);
        }
        offset       = NFS_DATA_OFS(inode->block_pointer[blk_cnt]);
        int offset_l = offset;
        int offset_r = offset + NFS_BLK_SZ();
        int dentry_cnt = 0;
        // newfs_dump_dmap();
        while (dentry_cursor != NULL)
        {
            if(offset_r <= offset + sizeof(struct newfs_dentry_d)) // switch block
            {
                blk_cnt ++;
                if(inode->block_pointer[blk_cnt] == -1)
                {
                    newfs_alloc_datab(inode);
                }
                offset = NFS_DATA_OFS(inode->block_pointer[blk_cnt]);
                int offset_l = offset;
                int offset_r = offset + NFS_BLK_SZ();
            }
            memcpy(dentry_d.fname, dentry_cursor->fname, NFS_MAX_FILE_NAME);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                 sizeof(struct newfs_dentry_d)) != NFS_ERROR_NONE) {
                // NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;                     
            }
            
            if (dentry_cursor->inode != NULL) {
                newfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct newfs_dentry_d);
        }
    }
    else if (NFS_IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接写即可 */
        for(int blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++)
        {   
            if (inode->dirty[blk_cnt] == 0)
            {
                continue;
            }

            if (inode->block_pointer[blk_cnt] == -1)
            {
                newfs_alloc_datab(inode);
                // continue;
            }
            // printf("writing: %d\n", inode->block_pointer[blk_cnt]);
            if (newfs_driver_write(NFS_DATA_OFS(inode->block_pointer[blk_cnt]), inode->data + blk_cnt * NFS_BLK_SZ(), 
                             NFS_BLK_SZ()) != NFS_ERROR_NONE) {
                // NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;
            }
        }   
    }
    /* Lastly: 写inode本身 */
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.link        = inode->link;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    inode_d.data_blk_cnt = inode->data_blk_cnt;

    for (blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++) {
        inode_d.block_pointer[blk_cnt] = inode->block_pointer[blk_cnt];
    }
    if (newfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                    sizeof(struct newfs_inode_d)) != NFS_ERROR_NONE) {
        // NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;
    }
    // newfs_dump_dmap();
    // newfs_dump_imap();
    return NFS_ERROR_NONE;
}
/**
 * @brief 删除内存中的一个inode
 * Case 1: Reg File
 * 
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 * 
 *  1) Step 1. Erase Bitmap     
 *  2) Step 2. Free Inode                      (Function of newfs_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 * 
 *   Recursive
 * @param inode 
 * @return int 
 */
int newfs_drop_inode(struct newfs_inode * inode) {
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry*  dentry_to_free;
    struct newfs_inode*   inode_cursor;

    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    int data_cursor = 0;
    boolean is_find = FALSE;

    if (inode == super.root_dentry->inode) {
        return NFS_ERROR_INVAL;
    }

    if (NFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
                                                      /* 递归向下drop */
        while (dentry_cursor)
        {   
            inode_cursor = dentry_cursor->inode;
            newfs_drop_inode(inode_cursor);
            newfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }

        for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.ino_map_blks); 
            byte_cursor++)                            /* 调整inodemap */
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     super.ino_map[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }

        // Clean the data map for directories
        for (int blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++) {
            if (inode->block_pointer[blk_cnt] == -1) {
                continue;
            }
            is_find = FALSE;
            data_cursor = 0;
            for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.data_map_blks); 
            byte_cursor++)                            /* 调整datamap */
            {
                for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                    if (data_cursor == inode->block_pointer[blk_cnt]) {
                        super.data_map[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                        is_find = TRUE;
                        break;
                    }
                    data_cursor++;
                }
                if (is_find == TRUE) {
                    break;
                }
            }
        }
    }
    else if (NFS_IS_REG(inode) || NFS_IS_SYM_LINK(inode)) {
        for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.ino_map_blks); 
            byte_cursor++)                            /* 调整inodemap */
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     super.ino_map[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }
        for (int blk_cnt = 0; blk_cnt < 6; blk_cnt++) {
            if (inode->block_pointer[blk_cnt] == -1) {
                continue;
            }
            is_find = FALSE;
            data_cursor = 0;
            for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.data_map_blks); 
            byte_cursor++)                            /* 调整datamap */
            {
                for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                    if (data_cursor == inode->block_pointer[blk_cnt]) {
                        super.data_map[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                        is_find = TRUE;
                        break;
                    }
                    data_cursor++;
                }
                if (is_find == TRUE) {
                    break;
                }
            }
        }
        if (inode->data)
            free(inode->data);
        free(inode);
    }
    return NFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    /* 从磁盘读索引结点 */
    if (newfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NFS_ERROR_NONE) {
        // NFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->data_blk_cnt = inode_d.data_blk_cnt;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for (int blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++) {
        inode->block_pointer[blk_cnt] = inode_d.block_pointer[blk_cnt];
    }
    /* 内存中的inode的数据或子目录项部分也需要读出 */
    if (NFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        int blk_cnt = 0;
        int offset   = NFS_DATA_OFS(inode->block_pointer[blk_cnt]);
        int offset_l = offset;
        int offset_r = offset + NFS_BLK_SZ();
        for (i = 0; i < dir_cnt; i++)
        {
            if (offset_r <= offset + sizeof(struct newfs_dentry_d)) // switch block
            {
                blk_cnt++;
                if(inode->block_pointer[blk_cnt] == -1)
                {
                    // wasted
                    return NULL;
                }
                offset = NFS_DATA_OFS(inode->block_pointer[blk_cnt]);
                offset_l = offset;
                offset_r = offset + NFS_BLK_SZ();
            }
            if (newfs_driver_read(offset, (uint8_t *)&dentry_d, 
                                sizeof(struct newfs_dentry_d)) != NFS_ERROR_NONE) {
                // NFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            newfs_alloc_dentry(inode, sub_dentry);
            offset += sizeof(struct newfs_dentry_d);
        }
    }
    else if (NFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(NFS_BLKS_SZ(NFS_DATA_PER_FILE));
        for (int blk_cnt = 0; blk_cnt < NFS_DATA_PER_FILE; blk_cnt++) {
            if (inode->block_pointer[blk_cnt] != -1) { // Check if the block is allocated
                if (newfs_driver_read(NFS_DATA_OFS(inode->block_pointer[blk_cnt]) , 
                                       inode->data + blk_cnt * NFS_BLK_SZ(), 
                                       NFS_BLK_SZ()) != NFS_ERROR_NONE) {
                    // NFS_DBG("[%s] io error\n", __func__);
                    free(inode->data);
                    return NULL;                    
                }
            }
        }
    }
    return inode;
}
/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
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
 * @brief 查找文件或目录
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *  
 * 
 * 如果能查找到，返回该目录项
 * 如果查找不到，返回的是上一个有效的路径
 * 
 * path: /a/b/c
 *      1) find /'s inode     lvl = 1
 *      2) find a's dentry 
 *      3) find a's inode     lvl = 2
 *      4) find b's dentry    如果此时找不到了，is_find=FALSE且返回的是a的inode对应的dentry
 * 
 * @param path 
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
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

        if (NFS_IS_REG(inode) && lvl < total_lvl) {
            // NFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)   /* 遍历子目录项 */
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                // NFS_DBG("[%s] not found %s\n", __func__, fname);
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
/**
 * @brief 挂载sfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data |
 * 
 * IO_SZ = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int                 ret = NFS_ERROR_NONE;
    int                 driver_fd;
    struct newfs_super_d  super_d; 
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;

    int                 ino_map_blks;
    int                 data_map_blks;
    int                 inode_blks;
    int                 data_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    super.is_mounted = FALSE;

    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    super.fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &super.sz_io);
    super.sz_blks = 2 * super.sz_io;
    
    root_dentry = new_dentry("/", NFS_DIR);     /* 根目录项每次挂载时新建 */

    if (newfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&super_d), 
                        sizeof(struct newfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }   
                                                /* 读取super */
    if (super_d.magic != NFS_MAGIC_NUM) {     /* 幻数不正确，初始化 */
                                                /* 估算各部分大小 */
        super_blks    = NFS_SUPER_BLKS;
        ino_map_blks  = NFS_MAP_INODE_BLKS;
        data_map_blks = NFS_MAP_DATA_BLKS;
        inode_blks    = NFS_INODE_BLKS;
        data_blks     = NFS_DATA_BLKS; // need calculation
        // printf("Hello\n");

                                                /* 布局layout */
        super.ino_max           = inode_blks;
        super_d.ino_map_offset  = NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks);
        super_d.ino_map_blks    = ino_map_blks;
        super_d.data_map_offset = super_d.ino_map_offset + NFS_BLKS_SZ(ino_map_blks);
        super_d.data_map_blks   = data_map_blks;
        super_d.ino_offset      = super_d.data_map_offset + NFS_BLKS_SZ(data_map_blks);
        super_d.ino_blks        = inode_blks;
        super_d.data_offset     = super_d.ino_offset + NFS_BLKS_SZ(inode_blks);
        super_d.data_blks       = data_blks;
        super_d.sz_usage        = 0;
        // NFS_DBG("inode map blocks: %d\n", ino_map_blks);
        is_init = TRUE;
    }

    super.sz_usage        = super_d.sz_usage;
    
    super.ino_map         = (uint8_t *)malloc(NFS_BLKS_SZ(super_d.ino_map_blks));
    super.data_map        = (uint8_t *)malloc(NFS_BLKS_SZ(super_d.data_map_blks));
    
    super.ino_map_offset  = super_d.ino_map_offset;
    super.ino_map_blks    = super_d.ino_map_blks;
    super.data_map_offset = super_d.data_map_offset;
    super.data_map_blks   = super_d.data_map_blks;
    super.ino_offset      = super_d.ino_offset;
    super.ino_blks        = super_d.ino_blks;
    super.data_offset     = super_d.data_offset;
    super.data_blks       = super_d.data_blks;

    // newfs_dump_imap();

	printf("\n--------------------------------------------------------------------------------\n\n");

    if (newfs_driver_read(super_d.ino_map_offset, (uint8_t *)(super.ino_map), 
                        NFS_BLKS_SZ(super_d.ino_map_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    } // read inode map

    if (newfs_driver_read(super_d.data_map_offset, (uint8_t *)(super.data_map), 
                         NFS_BLKS_SZ(super_d.data_map_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    } // read data map

    if (is_init) {                                    /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    
    root_inode            = newfs_read_inode(root_dentry, NFS_ROOT_INO);  /* 读取根目录 */
    root_dentry->inode    = root_inode;
    super.root_dentry = root_dentry;
    super.is_mounted  = TRUE;

    newfs_dump_imap();
    newfs_dump_dmap();
    return ret;
}
/**
 * @brief 
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d super_d; 

    if (!super.is_mounted) {
        return NFS_ERROR_NONE;
    }
    // newfs_dump_dmap();                           

    newfs_sync_inode(super.root_dentry->inode);     /* 从根节点向下刷写节点 */   

    super_d.magic           = NFS_MAGIC_NUM;
    super_d.ino_map_offset  = super.ino_map_offset;
    super_d.ino_map_blks    = super.ino_map_blks;
    super_d.data_map_offset = super.data_map_offset;
    super_d.data_map_blks   = super.data_map_blks;
    super_d.ino_offset      = super.ino_offset;
    super_d.ino_blks        = super.ino_blks;
    super_d.data_offset     = super.data_offset;
    super_d.data_blks       = super.data_blks;
    super_d.sz_usage        = super.sz_usage;

    newfs_dump_imap();
    newfs_dump_dmap(); 

    if (newfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&super_d, 
                     sizeof(struct newfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    } // write super block

    if (newfs_driver_write(super_d.ino_map_offset, (uint8_t *)(super.ino_map), 
                         NFS_BLKS_SZ(super_d.ino_map_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    } // write inode map

    if (newfs_driver_write(super_d.data_map_offset, (uint8_t *)(super.data_map), 
                         NFS_BLKS_SZ(super_d.data_map_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    } // write data map

    free(super.ino_map);
    free(super.data_map);
    ddriver_close(NFS_DRIVER());

    return NFS_ERROR_NONE;
}
