#include "mod.h"



/*----------------dentry的查找、增加、删除操作-----------------*/

/*
	在目录ip中查找是否存在名字为name的目录项
	如果找到了返回目录项中存储的inode_num
	如果没找到返回INVALID_INODE_NUM
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_search(inode_t *ip, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_search: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_search: not dir!");

	if (ip->disk_info.index[0] == 0)
		return INVALID_INODE_NUM;

	buffer_t *buf = buffer_get(ip->disk_info.index[0]);
	dentry_t *de;

	for (de = (dentry_t *)buf->data; de < (dentry_t *)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] == 0)
			continue;
		if (strncmp(de->name, name, MAXLEN_FILENAME) == 0)
		{
			uint32 inode_num = de->inode_num;
			buffer_put(buf);
			return inode_num;
		}
	}

	buffer_put(buf);
	return INVALID_INODE_NUM;
}
/*
	在目录ip中查找是否存在序号为inode_num的目录项
	如果存在则将它的名字拷贝到name, 返回name_len
	如果不存在则返回-1
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_search_2(inode_t *ip, uint32 inode_num, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_search_2: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_search_2: not dir!");

	if (ip->disk_info.index[0] == 0)
		return (uint32)-1;

	buffer_t *buf = buffer_get(ip->disk_info.index[0]);
	dentry_t *de;
	for (de = (dentry_t *)(buf->data); de < (dentry_t *)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] == 0)
			continue;
		if (de->inode_num == inode_num)
		{
			int n = strlen(de->name);
			if (n >= MAXLEN_FILENAME)
				n = MAXLEN_FILENAME - 1;
			memmove(name, de->name, n);
			name[n] = 0;
			buffer_put(buf);
			return (uint32)n;
		}
	}
	buffer_put(buf);
	return (uint32)-1;
}

/*
	在目录ip中寻找空闲槽位, 插入新的dentry
	如果成功插入则返回这个目录项的偏移量(还需要更新size)
	如果插入失败(没有空间/发生重名)返回-1
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_create(inode_t *ip, uint32 inode_num, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_create: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_create: not dir!");
	assert(name != NULL && name[0] != 0, "dentry_create: empty name");

	// Directory inode should have exactly one data block.
	if (ip->disk_info.index[0] == 0)
		return (uint32)-1;

	buffer_t *buf = buffer_get(ip->disk_info.index[0]);
	dentry_t *de;
	dentry_t *empty_slot = NULL;

	for (de = (dentry_t *)buf->data; de < (dentry_t *)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] == 0)
		{
			if (empty_slot == NULL)
				empty_slot = de;
			continue;
		}
		if (strncmp(de->name, name, MAXLEN_FILENAME) == 0)
		{
			buffer_put(buf);
			return (uint32)-1;
		}
	}

	if (empty_slot == NULL)
	{
		buffer_put(buf);
		return (uint32)-1;
	}

	memset(empty_slot, 0, sizeof(dentry_t));
	int n = strlen(name);
	if (n >= MAXLEN_FILENAME)
		n = MAXLEN_FILENAME - 1;
	memmove(empty_slot->name, name, n);
	empty_slot->name[n] = 0;
	empty_slot->inode_num = inode_num;

	uint32 offset = (uint32)((uint8 *)empty_slot - buf->data);
	uint32 end = offset + sizeof(dentry_t);
	if (end > ip->disk_info.size)
		ip->disk_info.size = end;

	buffer_write(buf);
	buffer_put(buf);
	return offset;
}

/*
	在目录ip下删除名称为name的dentry, 返回它的inode_num
	如果匹配失败或者遇到非法情况返回INVALID_INODE_NUM
	注意: 调用者需要持有ip->slk
*/
uint32 dentry_delete(inode_t *ip, char *name)
{
	assert(sleeplock_holding(&ip->slk), "dentry_delete: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_delete: not dir!");
	assert(name != NULL && name[0] != 0, "dentry_delete: empty name");

	if (ip->disk_info.index[0] == 0)
		return INVALID_INODE_NUM;

	buffer_t *buf = buffer_get(ip->disk_info.index[0]);
	dentry_t *de;

	for (de = (dentry_t *)buf->data; de < (dentry_t *)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] == 0)
			continue;
		if (strncmp(de->name, name, MAXLEN_FILENAME) == 0)
		{
			uint32 inode_num = de->inode_num;
			memset(de, 0, sizeof(dentry_t));
			buffer_write(buf);
			buffer_put(buf);
			return inode_num;
		}
	}

	buffer_put(buf);
	return INVALID_INODE_NUM;
}
/*
	向缓冲区[dst, dst + len)中填充有效的dentry
	返回成功填充的数据量(字节)
	注意: 调用者需持有ip->slk
*/
uint32 dentry_transmit(inode_t *ip, uint64 dst, uint32 len, bool is_user_dst)
{
	assert(sleeplock_holding(&ip->slk), "dentry_transmit: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_transmit: not dir!");

	if (ip->disk_info.index[0] == 0)
		return 0;

	buffer_t *buf = buffer_get(ip->disk_info.index[0]);
	dentry_t *de;
	uint32 write_len = 0;
	proc_t *p = myproc();

	for (de = (dentry_t *)(buf->data); de < (dentry_t *)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] == 0)
			continue;
		if (write_len + sizeof(dentry_t) > len)
			break;
		if (is_user_dst)
		{
			uvm_copyout(p->pgtbl, dst, (uint64)de, sizeof(dentry_t));
		}
		else
		{
			memmove((void *)dst, de, sizeof(dentry_t));
		}
		dst += sizeof(dentry_t);
		write_len += sizeof(dentry_t);
	}

	buffer_put(buf);
	return write_len;
}

/* 输出目录中所有有效目录项的信息 (for debug) */
void dentry_print(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "dentry_print: slk!");
	assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_print: not dir!");

	if (ip->disk_info.index[0] == 0)
		panic("dentry_print: invalid index[0]!");

	printf("inode_num = %d, dentries:\n", ip->inode_num);

	buffer_t *buf = buffer_get(ip->disk_info.index[0]);
	dentry_t *de;

	for (de = (dentry_t *)buf->data; de < (dentry_t *)(buf->data + BLOCK_SIZE); de++)
	{
		if (de->name[0] != 0)
		{
			printf("dentry: offset = %d, inode_num = %d, name = %s\n",
				   (uint32)((uint8 *)de - buf->data), de->inode_num, de->name);
		}
	}

	buffer_put(buf);
	printf("\n");
}

/*------------------从文件名到文件路径-----------------*/

/*
	Examples:
	get_element("a/bb/c", name) = "bb/c" + name = "a"
	get_element("///aa//bb", name) = "bb" + name = "aa"
	get_element("aaa", name) = "" + name = "aaa"
	get_element("", name) = NULL + name = ""
	get_element("//", name) = NULL + name = ""
*/
static char *get_element(char *path, char *name)
{
	/* 跳过前置的'/' */
	while (*path == '/')
		path++;

	/* 如果遇到末尾了则返回 */
	if (*path == 0)
	{
		name[0] = 0;
		return NULL;
	}

	/* 记录起点位置 */
	char *start = path;

	/* 推进path直到遇到'/'或者到达末尾 */
	while (*path != '/' && *path != 0)
		path++;

	/* 提取到的name的长度 */
	int len = path - start;
	len = MIN(len, MAXLEN_FILENAME - 1);

	/* 设置name */
	memmove(name, start, len);
	name[len] = 0;

	/* 跳过后置的'/' */
	while (*path == '/')
		path++;

	return path;
}

/*
	根据文件路径(/A/B/C)查找对应inode(inode_B or inode_C)
	如果find_parent_inode == true, 返回父节点inode, name为下一级子节点的名字
	如果find_parent_inode == false, 返回子节点inode, name无意义
	如果失败返回NULL
	注意: 返回的inode未上锁，调用者需要根据需要加锁
*/
static inode_t *__path_to_inode(char *path, char *name, bool find_parent_inode)
{
	inode_t *ip = inode_get(ROOT_INODE);
	char elem[MAXLEN_FILENAME];
	char *p = path;

	while ((p = get_element(p, elem)) != NULL)
	{
		if (find_parent_inode && p[0] == 0)
		{
			// elem is the last component
			memmove(name, elem, MAXLEN_FILENAME);
			return ip;
		}

		inode_lock(ip);
		if (ip->disk_info.type != INODE_TYPE_DIR)
		{
			inode_unlock(ip);
			inode_put(ip);
			return NULL;
		}
		uint32 next_inode_num = dentry_search(ip, elem);
		inode_unlock(ip);

		if (next_inode_num == INVALID_INODE_NUM)
		{
			inode_put(ip);
			return NULL;
		}

		inode_t *next_ip = inode_get(next_inode_num);
		inode_put(ip);
		ip = next_ip;
	}

	if (find_parent_inode)
	{
		inode_put(ip);
		name[0] = 0;
		return NULL;
	}

	return ip;
}

/*
	基于path寻找inode
	失败返回NULL
	注意: 返回的inode未上锁
*/
inode_t *path_to_inode(char *path)
{
	char name[MAXLEN_FILENAME];
	return __path_to_inode(path, name, false);
}

/*
	基于path寻找inode->parent, 将inode->name放入name
	失败返回NULL, 同时name无效
	注意: 返回的inode未上锁
*/
inode_t *path_to_parent_inode(char *path, char *name)
{
	return __path_to_inode(path, name, true);
}
/*
	将inode对应的完整路径填入path中(缓冲区长度为len)
	成功返回偏移量(从path+offset开始有效), 失败返回-1
*/
uint32 inode_to_path(inode_t *ip, char *path, uint32 len)
{
	if (len == 0)
		return (uint32)-1;

	path[len - 1] = 0;
	uint32 pos = len - 1;
	inode_t *cur = inode_dup(ip);
	bool is_first = true;

	while (1)
	{
		if (cur->inode_num == ROOT_INODE)
		{
			if (pos == 0) {
				inode_put(cur);
				return (uint32)-1;
			}
			path[--pos] = '/';
			inode_put(cur);
			return pos;
		}

		inode_lock(cur);
		uint32 parent_inum = dentry_search(cur, "..");
		inode_unlock(cur);

		if (parent_inum == INVALID_INODE_NUM) {
			inode_put(cur);
			return (uint32)-1;
		}

		inode_t *parent = inode_get(parent_inum);
		char name_buf[MAXLEN_FILENAME];
		
		inode_lock(parent);

		uint32 name_len = dentry_search_2(parent, cur->inode_num, name_buf);
		inode_unlock(parent);

		if ((int)name_len < 0) {
			inode_put(parent);
			inode_put(cur);
			return (uint32)-1;
		}

		if (!is_first) {
			if (pos == 0) {
				inode_put(parent);
				inode_put(cur);
				return (uint32)-1;
			}
			path[--pos] = '/';
		}
		is_first = false;

		if (name_len > pos) {
			inode_put(parent);
			inode_put(cur);
			return (uint32)-1;
		}
		pos -= name_len;
		memmove(path + pos, name_buf, name_len);

		inode_put(cur);
		cur = parent;
	}
}

/*
	基于path创建新的inode
	成功返回inode, 失败返回NULL
*/
inode_t *path_create_inode(char *path, uint16 type, uint16 major, uint16 minor)
{
	char name[MAXLEN_FILENAME];
	inode_t *parent = path_to_parent_inode(path, name);
	if (parent == NULL)
		return NULL;

	if (name[0] == 0) {
		inode_put(parent);
		return NULL;
	}

	inode_t *ip = inode_create(type, major, minor);
	if (ip == NULL) {
		inode_put(parent);
		return NULL;
	}

	uint32 parent_inum = parent->inode_num;
	inode_lock(parent);
	if (dentry_create(parent, ip->inode_num, name) == (uint32)-1) {
		inode_unlock(parent);
		// 创建失败，通过销毁链接来回收
		inode_lock(ip);
		ip->disk_info.nlink = 0;
		inode_unlock(ip);
		inode_put(ip);
		inode_put(parent);
		return NULL;
	}
	inode_rw(parent, true);
	inode_unlock(parent);
	inode_put(parent);

	if (type == INODE_TYPE_DIR) {
		inode_lock(ip);
		// 目录需要创建 . 和 ..
		if (dentry_create(ip, ip->inode_num, ".") == (uint32)-1 ||
			dentry_create(ip, parent_inum, "..") == (uint32)-1) {
			ip->disk_info.nlink = 0;
			inode_unlock(ip);
			inode_put(ip);
			return NULL;
		}
		inode_rw(ip, true);
		inode_unlock(ip);
	}

	return ip;
}

/*
	构建文件硬链接 (new_path 指向 old_path 指向的 inode)
	核心操作包括 nlink++ 和 dentry_create()
*/
uint32 path_link(char *old_path, char *new_path)
{
	inode_t *old_ip = path_to_inode(old_path);
	if (old_ip == NULL)
		return (uint32)-1;

	inode_lock(old_ip);
	if (old_ip->disk_info.type == INODE_TYPE_DIR) {
		inode_unlock(old_ip);
		inode_put(old_ip);
		return (uint32)-1;
	}
	old_ip->disk_info.nlink++;
	inode_rw(old_ip, true);
	inode_unlock(old_ip);

	char name[MAXLEN_FILENAME];
	inode_t *parent = path_to_parent_inode(new_path, name);
	if (parent == NULL || name[0] == 0) {
		goto link_fail;
	}

	inode_lock(parent);
	if (dentry_create(parent, old_ip->inode_num, name) == (uint32)-1) {
		inode_unlock(parent);
		inode_put(parent);
		goto link_fail;
	}
	inode_rw(parent, true);
	inode_unlock(parent);

	inode_put(parent);
	inode_put(old_ip);
	return 0;

link_fail:
	inode_lock(old_ip);
	old_ip->disk_info.nlink--;
	inode_rw(old_ip, true);
	inode_unlock(old_ip);
	inode_put(old_ip);
	return (uint32)-1;
}

/*
	解除文件硬链接
	成功返回0, 失败返回-1
*/
uint32 path_unlink(char *path)
{
	char name[MAXLEN_FILENAME];
	inode_t *parent = path_to_parent_inode(path, name);
	if (parent == NULL || name[0] == 0)
		return (uint32)-1;

	inode_lock(parent);
	uint32 inum = dentry_delete(parent, name);
	inode_rw(parent, true);
	inode_unlock(parent);

	if (inum == INVALID_INODE_NUM) {
		inode_put(parent);
		return (uint32)-1;
	}

	inode_t *ip = inode_get(inum);
	inode_lock(ip);
	
	// 如果是目录，需检查是否为空（除了 . 和 .. 外无内容）
	if (ip->disk_info.type == INODE_TYPE_DIR) {
		if (ip->disk_info.index[0] != 0) {
			buffer_t *buf = buffer_get(ip->disk_info.index[0]);
			dentry_t *de;
			for (de = (dentry_t *)buf->data; de < (dentry_t *)(buf->data + BLOCK_SIZE); de++) {
				if (de->name[0] == 0) continue;
				if (strncmp(de->name, ".", MAXLEN_FILENAME) == 0 ||
					strncmp(de->name, "..", MAXLEN_FILENAME) == 0)
					continue;
				// 目录不为空，不能删除
				buffer_put(buf);
				inode_unlock(ip);
				inode_put(ip);
				inode_put(parent);
				return (uint32)-1;
			}
			buffer_put(buf);
		}
	}

	if (ip->disk_info.nlink > 0)
		ip->disk_info.nlink--;
	
	inode_rw(ip, true);
	inode_unlock(ip);

	inode_put(ip);
	inode_put(parent);
	return 0;
}