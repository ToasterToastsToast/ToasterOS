#include "mod.h"

/*----------------dentry的查找、增加、删除操作-----------------*/

uint32 dentry_search(inode_t *ip, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_search: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_search: not dir!");

	// 目前简化假设目录只占用一个 block (index[0])
	uint32 block_num = ip->disk_info.index[0];
	if (block_num == 0)
		return INVALID_INODE_NUM;

	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;

	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		if (de[i].name[0] != 0 && strncmp(de[i].name, name, MAXLEN_FILENAME) == 0) {
			uint32 inode_num = de[i].inode_num;
			buffer_put(buf);
			return inode_num;
		}
	}

	buffer_put(buf);
	return INVALID_INODE_NUM; 
}

uint32 dentry_create(inode_t *ip, uint32 inode_num, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_create: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_create: not dir!");

	if (dentry_search(ip, name) != INVALID_INODE_NUM) {
		return -1; // 重名
	}

	uint32 block_num = ip->disk_info.index[0];
    if (block_num == 0) {
		block_num = bitmap_alloc_block();
		if (block_num == (uint32)-1) {
			return -1; 
		}
		ip->disk_info.index[0] = block_num;
		buffer_t *buf = buffer_get(block_num);
        memset(buf->data, 0, BLOCK_SIZE);
        buffer_write(buf);
        buffer_put(buf);
		ip->disk_info.size = BLOCK_SIZE;
        inode_rw(ip, true); // 写回 inode 元数据
	}

	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;
	int empty_slot = -1;

	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		if (de[i].name[0] == 0) {
			empty_slot = i;
			break;
		}
	}

	if (empty_slot == -1) {
		buffer_put(buf);
		return -1; 
	}

	memmove(de[empty_slot].name, name, MAXLEN_FILENAME);
    de[empty_slot].inode_num = inode_num;

	buffer_write(buf);
	buffer_put(buf);
	return (uint32)(empty_slot * sizeof(dentry_t)); 
}

uint32 dentry_delete(inode_t *ip, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_delete: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_delete: not dir!");

	uint32 block_num = ip->disk_info.index[0];
	if (block_num == 0)
		return INVALID_INODE_NUM; 
	
	buffer_t *buf = buffer_get(block_num);
	dentry_t *de = (dentry_t *)buf->data;

	for (int i = 0; i < DENTRY_PER_BLOCK; i++) {
		if (de[i].name[0] != 0 && strncmp(de[i].name, name, MAXLEN_FILENAME) == 0) {
			uint32 inode_num = de[i].inode_num;
			memset(de[i].name, 0, MAXLEN_FILENAME);
			de[i].inode_num = 0;

			buffer_write(buf);
			buffer_put(buf);
			return inode_num; 
		}
	}

	buffer_put(buf);
	return INVALID_INODE_NUM; 
}

void dentry_print(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "dentry_print: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_print: not dir!");

	dentry_t *de;
	buffer_t *buf;

	if (ip->disk_info.index[0] == 0)
		panic("dentry_print: invalid index[0]!");
	
	printf("inode_num = %d, dentries:\n", ip->inode_num);

	buf = buffer_get(ip->disk_info.index[0]);
	for (de = (dentry_t*)(buf->data); de < (dentry_t*)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] != 0) {
			printf("dentry: offset = %d, inode_num = %d, name = %s\n",
				(uint32)((uint8*)de - buf->data), de->inode_num, de->name);
		}
	}
	buffer_put(buf);
	printf("\n");
}

/*------------------从文件名到文件路径-----------------*/

static char* get_element(char *path, char *name)
{
    while (*path == '/') path++;
    if (*path == 0) {
		name[0] = 0;
		return NULL;
	}
    char *start = path;
	while (*path != '/' && *path != 0) path++;
    int len = path - start;
	len = MIN(len, MAXLEN_FILENAME-1);
	memmove(name, start, len);
	name[len] = 0;
    while (*path == '/') path++;
    return path;
}

static inode_t* __path_to_inode(char *path, char *name, bool find_parent_inode)
{
	inode_t *ip, *next_ip;

	ip = inode_get(ROOT_INODE);
	inode_lock(ip);

	while ((path = get_element(path, name)) != NULL) {
		if (find_parent_inode && *path == '\0') {
            inode_unlock(ip); 
			return ip; 
		}

		if (ip->disk_info.type != INODE_TYPE_DIR) {
			inode_unlock(ip);
			inode_put(ip);
			return NULL;
		}

		uint32 next_inode_num = dentry_search(ip, name);
		if (next_inode_num == INVALID_INODE_NUM) {
			inode_unlock(ip);
			inode_put(ip);
			return NULL;
		}

		inode_unlock(ip); // Hand-over-hand locking
		next_ip = inode_get(next_inode_num);
		inode_put(ip); 

		ip = next_ip;
		inode_lock(ip); 
	}

	if (find_parent_inode) {
		inode_unlock(ip);
		inode_put(ip);
		return NULL;
	}

	inode_unlock(ip);
	return ip;
}

inode_t* path_to_inode(char *path)
{
	char name[MAXLEN_FILENAME];
	return __path_to_inode(path, name, false);
}

inode_t* path_to_parent_inode(char *path, char *name)
{
	return __path_to_inode(path, name, true);
}