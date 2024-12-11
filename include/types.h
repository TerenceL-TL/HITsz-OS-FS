#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum nfs_file_type {
    NFS_REG_FILE,
    NFS_DIR,
    NFS_SYM_LINK
} NFS_FILE_TYPE;

/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NFS_MAGIC_NUM           0x52415459  
#define NFS_SUPER_OFS           0
#define NFS_ROOT_INO            0

#define NFS_SUPER_BLKS          1 
#define NFS_MAP_INODE_BLKS      1 // bitmap
#define NFS_MAP_DATA_BLKS       1 // bitmap
#define NFS_INODE_BLKS          585 
#define NFS_DATA_BLKS           3508 

#define NFS_ERROR_NONE          0
#define NFS_ERROR_ACCESS        EACCES
#define NFS_ERROR_SEEK          ESPIPE     
#define NFS_ERROR_ISDIR         EISDIR
#define NFS_ERROR_NOSPACE       ENOSPC
#define NFS_ERROR_EXISTS        EEXIST
#define NFS_ERROR_NOTFOUND      ENOENT
#define NFS_ERROR_UNSUPPORTED   ENXIO
#define NFS_ERROR_IO            EIO     /* Error Input/Output */
#define NFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NFS_MAX_FILE_NAME       128
#define NFS_INODE_PER_FILE      1
#define NFS_DATA_PER_FILE       6
#define NFS_DEFAULT_PERM        0777

#define NFS_IOC_MAGIC           'S'
#define NFS_IOC_SEEK            _IO(NFS_IOC_MAGIC, 0)

#define NFS_FLAG_BUF_DIRTY      0x1
#define NFS_FLAG_BUF_OCCUPY     0x2

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NFS_IO_SZ()                     (super.sz_io)
#define NFS_BLK_SZ()                    (super.sz_blks)
#define NFS_DISK_SZ()                   (super.sz_disk)
#define NFS_DRIVER()                    (super.fd)

#define NFS_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define NFS_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

#define NFS_BLKS_SZ(blks)               ((blks) * NFS_BLK_SZ())
#define NFS_ASSIGN_FNAME(psfs_dentry, _fname)  memcpy(psfs_dentry->fname, _fname, strlen(_fname))
#define NFS_INO_OFS(ino)                (super.ino_offset  + ino * NFS_BLK_SZ())
#define NFS_DATA_OFS(ino)               (super.data_offset + ino * NFS_BLK_SZ())

#define NFS_IS_DIR(pinode)              (pinode->dentry->ftype == NFS_DIR)
#define NFS_IS_REG(pinode)              (pinode->dentry->ftype == NFS_REG_FILE)
#define NFS_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == NFS_SYM_LINK)

/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct newfs_dentry;
struct newfs_inode;
struct newfs_super;

struct custom_options {
	const char*        device;
};

struct newfs_super {
    uint32_t magic;
    int      fd;

    /* 块信息 */
    int sz_io;
    int sz_disk;
    int sz_blks;
    int sz_usage;

    /* 磁盘布局分区信息 */
    int sb_offset;          // 超级块于磁盘中的偏移，通常默认为0
    int sb_blks;            // 超级块于磁盘中的块数，通常默认为1

    int max_ino;
    int ino_map_offset;     // 索引节点位图于磁盘中的偏移
    int ino_map_blks;       // 索引节点位图于磁盘中的块数
    uint8_t* ino_map;

    int data_map_offset;    // data位图于磁盘中的偏移
    int data_map_blks;      // data位图于磁盘中的块数
    uint8_t* data_map;

    int ino_offset;         // 索引节点于磁盘中的偏移
    int ino_blks;           // 索引节点于磁盘中的块数

    int data_offset;        // data于磁盘中的偏移
    int data_blks;          // data于磁盘中的块数

    /* 支持的限制 */
    int ino_max;            // 最大支持inode数
    int file_max;           // 支持文件最大大小

    /* 根目录索引 */
    struct newfs_dentry* root_dentry; // 根目录dentry

    /* 其他信息 */
    boolean is_mounted;
};

struct newfs_inode {
    uint32_t ino;

    int size;  /* 文件已占用空间 */
    int link;  // link number: 1
    // NFS_FILE_TYPE ftype;
    struct newfs_dentry* dentry; /* 指向该inode的dentry */
    struct newfs_dentry* dentrys; /* 所有目录项 */
    int data_blk_cnt; // data block used 
    int dir_cnt; 
    int block_pointer[6]; // to the data blocks
    int dirty[6]; // to the data blocks
    u_int8_t* data;
};

struct newfs_dentry {
    // in disk
    char     fname[NFS_MAX_FILE_NAME];
    uint32_t ino;
    NFS_FILE_TYPE ftype;

    // in memory only
    struct newfs_dentry *parent;  // father
    struct newfs_dentry *brother; // brother
    struct newfs_inode  *inode;   // related inode
};

static inline struct newfs_dentry* new_dentry(char * fname, NFS_FILE_TYPE ftype) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;    
    return dentry;
}

struct file_info {
    struct newfs_inode* inode;  // Pointer to the inode for this file
    off_t offset;               // Current offset in the file (for read/write operations)
    int open_flags;             // Flags to track how the file was opened 
};

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct newfs_super_d {
    uint32_t magic;
    int      fd;

    /* 磁盘布局分区信息 */
    int ino_map_offset;     // 索引节点位图于磁盘中的偏移
    int ino_map_blks;       // 索引节点位图于磁盘中的块数

    int data_map_offset;    // data位图于磁盘中的偏移
    int data_map_blks;      // data位图于磁盘中的块数

    int ino_offset;         // 索引节点于磁盘中的偏移
    int ino_blks;           // 索引节点于磁盘中的块数

    int data_offset;        // data于磁盘中的偏移
    int data_blks;          // data于磁盘中的块数

    /* 支持的限制 */
    int ino_max;            // 最大支持inode数
    int file_max;           // 支持文件最大大小

    int sz_usage;
};

struct newfs_inode_d {
    uint32_t ino;

    int size;  /* 文件已占用空间 */
    int link;  // link number: 1
    NFS_FILE_TYPE ftype;
    int data_blk_cnt; // data block used 
    int dir_cnt; 
    int block_pointer[6]; // to the data blocks
};

struct newfs_dentry_d {
    // in disk
    char     fname[NFS_MAX_FILE_NAME];
    uint32_t ino;
    NFS_FILE_TYPE ftype;
};

#endif /* _TYPES_H_ */