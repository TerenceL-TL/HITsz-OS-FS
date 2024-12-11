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
	.write = newfs_write,								  	 /* 写入文件 */
	.read = newfs_read,								  	 /* 读文件 */
	.utimens = newfs_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = newfs_truncate,						  		 /* 改变文件大小 */
	.unlink = newfs_unlink,							  		 /* 删除文件 */
	.rmdir	= newfs_rmdir,							  		 /* 删除目录， rm -r */
	.rename = newfs_rename,							  		 /* 重命名，mv */

	.open = newfs_open,							
	.opendir = newfs_opendir,
	.access = newfs_access
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* newfs_init(struct fuse_conn_info * conn_info) {
	/* 在这里进行挂载 */

	// /* 下面是一个控制设备的示例 */
	// super.fd = ddriver_open(newfs_options.device);
	if (newfs_mount(newfs_options) != NFS_ERROR_NONE) 
	{
		// NFS_DBG("[%s] mount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	}
	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void newfs_destroy(void* p) {
	/* 在这里进行卸载 */
	
	// ddriver_close(super.fd);
	if (newfs_umount(newfs_options) != NFS_ERROR_NONE)
	{
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	}
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
	/* 解析路径，创建目录 */
	(void)mode;
	boolean is_find, is_root;
	char* fname;
	struct newfs_dentry* last_dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_dentry* dentry;
	struct newfs_inode*  inode;

	// 目录已经存在
	if (is_find) {
		return -NFS_ERROR_EXISTS;
	}

	// former is regular file
	if (NFS_IS_REG(last_dentry->inode)) {
		return -NFS_ERROR_UNSUPPORTED;
	}
	
	// 创建一个新目录 
	fname  = newfs_get_fname(path);
	dentry = new_dentry(fname, NFS_DIR); 
	dentry->parent = last_dentry;
	inode  = newfs_alloc_inode(dentry);
	newfs_alloc_dentry(last_dentry->inode, dentry);
	
	return NFS_ERROR_NONE;
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
	boolean	is_find, is_root;
	// 路径解析
	struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
	if (is_find == FALSE) { // 找不到对应文件
		return -NFS_ERROR_NOTFOUND;
	}

	if (NFS_IS_DIR(dentry->inode)) {
		newfs_stat->st_mode = S_IFDIR | NFS_DEFAULT_PERM;
		newfs_stat->st_size = dentry->inode->dir_cnt * sizeof(struct newfs_dentry_d);
	}
	else if (NFS_IS_REG(dentry->inode)) {
		newfs_stat->st_mode = S_IFREG | NFS_DEFAULT_PERM;
		newfs_stat->st_size = dentry->inode->size;
	}

	newfs_stat->st_nlink = dentry->inode->link;
	newfs_stat->st_uid 	 = getuid();
	newfs_stat->st_gid 	 = getgid();
	newfs_stat->st_atime   = time(NULL);
	newfs_stat->st_mtime   = time(NULL);
	newfs_stat->st_blksize = NFS_BLK_SZ();
	newfs_stat->st_blocks  = dentry->inode->data_blk_cnt;

	if (is_root) {
		newfs_stat->st_size	= super.sz_usage; 
		newfs_stat->st_blocks = NFS_DISK_SZ() / NFS_BLK_SZ();
		newfs_stat->st_nlink  = 2;		/* 根目录link数为2 */
	}

	return NFS_ERROR_NONE;
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
    boolean	is_find, is_root;
	int		cur_dir = offset;

	struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
	// printf("ISFIND: %d\n", is_find);
	struct newfs_dentry* sub_dentry;
	struct newfs_inode*  inode;
	if (is_find) {
		inode = dentry->inode;
		sub_dentry = newfs_get_dentry(inode, cur_dir);
		if (sub_dentry) {
			filler(buf, sub_dentry->fname, NULL, ++offset);
		}
		return NFS_ERROR_NONE;
	}
	return -NFS_ERROR_NOTFOUND;
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
	boolean is_find,is_root;
	struct newfs_dentry* f_dentry = newfs_lookup(path,&is_find,&is_root);
	struct newfs_dentry* dentry;
	struct newfs_inode* inode;

	if(is_find){
		return -NFS_ERROR_EXISTS;
	}
	if(NFS_IS_REG(f_dentry->inode)){
		return -NFS_ERROR_UNSUPPORTED;
	}

	char* fname = newfs_get_fname(path);
	dentry = new_dentry(fname, NFS_REG_FILE);
	inode = newfs_alloc_inode(dentry);
	newfs_alloc_dentry(f_dentry->inode, dentry);
	return NFS_ERROR_NONE;
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
	boolean is_find,is_root;
	struct newfs_dentry* dentry = newfs_lookup(path,&is_find,&is_root);
	struct newfs_inode* inode;

	if (!is_find) {
		return -NFS_ERROR_NOTFOUND;
	}

	inode = dentry->inode;

	if (NFS_IS_DIR(inode)) {
		return -NFS_ERROR_ISDIR;	
	}

	if(inode->size < offset)
	{
		return -NFS_ERROR_SEEK;
	}

	memcpy(inode->data + offset, buf, size);
	if(inode->size < offset + size)
	{
		inode->size = offset + size;
	}

	int l_block = (int)(NFS_ROUND_DOWN(offset, NFS_BLK_SZ())/NFS_BLK_SZ());
	int r_block = (int)(NFS_ROUND_UP(offset + size, NFS_BLK_SZ())/NFS_BLK_SZ());

	// dirty
	for(int blk_cnt = l_block; blk_cnt < r_block && blk_cnt < NFS_DATA_PER_FILE; blk_cnt++) 
	{
		inode->dirty[blk_cnt] = 1;
	}

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
	boolean is_find,is_root;
	struct newfs_dentry* dentry = newfs_lookup(path,&is_find,&is_root);
	struct newfs_inode* inode;

	if (!is_find) {
		return -NFS_ERROR_NOTFOUND;
	}

	inode = dentry->inode;

	if (NFS_IS_DIR(inode)) {
		return -NFS_ERROR_ISDIR;	
	}

	if(inode->size < offset)
	{
		return -NFS_ERROR_SEEK;
	}

	memcpy(buf, inode->data + offset, size);

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
	boolean is_find,is_root;
	struct newfs_dentry* dentry = newfs_lookup(path,&is_find,&is_root);
	struct newfs_inode* inode;

	if (!is_find) {
		return -NFS_ERROR_NOTFOUND;
	}

	inode = dentry->inode;

	if (NFS_IS_DIR(inode)) {
		return -NFS_ERROR_ISDIR;	
	}

	newfs_drop_inode(inode);
	newfs_drop_dentry(dentry->parent->inode, dentry);

	free(dentry);
	
	return NFS_ERROR_NONE;
}

int newfs_rmdir_rs(struct newfs_inode* inode)
{
    struct newfs_dentry* dentry = inode->dentry;
	struct newfs_dentry* dentry_cursor = NULL;
	struct newfs_dentry* old_dentry = NULL;

	if (!NFS_IS_DIR(inode)) {
        return -NFS_ERROR_UNSUPPORTED; // Path is not a directory
    }

	dentry_cursor = inode->dentrys;

	while(dentry_cursor != NULL)
	{
		struct newfs_inode* sub_inode = dentry_cursor->inode;
		if(NFS_IS_REG(sub_inode))
		{
			newfs_drop_inode(sub_inode);
			newfs_drop_dentry(inode, dentry_cursor);
			old_dentry = dentry_cursor;
			dentry_cursor = dentry_cursor->brother;
			free(old_dentry);
			continue;
		}
		else if(NFS_IS_DIR(sub_inode))
		{
			newfs_rmdir_rs(sub_inode);
		}
	}

	// Drop the inode and dentry
    newfs_drop_inode(inode);
    newfs_drop_dentry(dentry->parent->inode, dentry);

	return NFS_ERROR_NONE;
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
	boolean is_find, is_root;
    struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_inode* inode;

    if (!is_find) {
        return -NFS_ERROR_NOTFOUND;
    }

    inode = dentry->inode;

	int ret = newfs_rmdir_rs(inode);
	if(ret != NFS_ERROR_NONE)
	{
		return ret;
	}

    return NFS_ERROR_NONE; // Successfully removed the directory
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
	boolean is_find_from, is_find_to, is_root_from, is_root_to;
    struct newfs_dentry* dentry_from = newfs_lookup(from, &is_find_from, &is_root_from);
    struct newfs_dentry* dentry_to = newfs_lookup(to, &is_find_to, &is_root_to);

    if (!is_find_from) {
        return -NFS_ERROR_NOTFOUND; // Source file not found
    }

	if(strcmp(from,to) == 0){
		return NFS_ERROR_NONE;
	}

    if (is_find_to) {
        return -NFS_ERROR_EXISTS; // Target file already exists
    }

	memcpy(dentry_from->fname, to, NFS_MAX_FILE_NAME);
    
    // Update the parent directory of the "to" dentry
    newfs_drop_dentry(dentry_from->parent->inode, dentry_from);
    newfs_alloc_dentry(dentry_to->inode, dentry_from);

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
	boolean is_find, is_root;
    struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);

    if (!is_find) {
        return -NFS_ERROR_NOTFOUND; // File not found
    }

	struct file_info* f_info = malloc(sizeof(struct file_info));
    if (!f_info) {
        return -NFS_ERROR_NOSPACE; // Allocation failed
    }

	f_info->inode = dentry->inode;
    fi->fh = (uintptr_t)f_info;

	return NFS_ERROR_NONE;
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
	boolean is_find, is_root;
    struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);

    if (!is_find) {
        return -NFS_ERROR_NOTFOUND; // Directory not found
    }

    if (!NFS_IS_DIR(dentry->inode)) {
        return -NFS_ERROR_UNSUPPORTED; // Path is not a directory
    }

	struct file_info* f_info = malloc(sizeof(struct file_info));
    if (!f_info) {
        return -NFS_ERROR_NOSPACE; // Allocation failed
    }

	f_info->inode = dentry->inode;
    fi->fh = (uintptr_t)f_info;

	return 0;
}

void shrink_data_map(struct newfs_inode* inode){
	int data_ino = inode->ino * NFS_DATA_PER_FILE;
	if(NFS_IS_REG(inode)){
		inode->data_blk_cnt = NFS_ROUND_UP(inode->size, NFS_BLK_SZ()) / NFS_BLK_SZ();
		for(int i=0;i<super.file_max;i++,data_ino++){
			if(i < inode->data_blk_cnt){
				super.data_map[data_ino / 8] |= (0x1 << (data_ino % 8));
			}
			else{
				super.data_map[data_ino / 8] &= (uint8_t)(~(0x1 << (data_ino % 8)));
			}
		}
	}
	return; 
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
	boolean	is_find, is_root;
	struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_inode*  inode;

	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}
	inode = dentry->inode;
	if (NFS_IS_DIR(inode)) {
		return -NFS_ERROR_ISDIR;
	}

	inode->size = offset;  // 改变文件的大小
	shrink_data_map(inode);
	return NFS_ERROR_NONE;
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
	boolean	is_find, is_root, is_access_ok = FALSE;
	struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_inode*  inode;
	switch (type)
	{
		case R_OK:
			is_access_ok = TRUE;
			break;
		case F_OK:
			if (is_find) {
				is_access_ok = TRUE;
			}
			break;
		case W_OK:
			is_access_ok = TRUE;
			break;
		case X_OK:
			is_access_ok = TRUE;
			break;
		default:
			break;
	}
	return is_access_ok ? NFS_ERROR_NONE : -NFS_ERROR_ACCESS;
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