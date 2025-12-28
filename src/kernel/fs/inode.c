#include "mod.h"

extern super_block_t sb;

/* 内存中的inode资源集合 */
static inode_t inode_cache[N_INODE];
static spinlock_t lk_inode_cache;

/* inode_cache初始化 */
void inode_init()
{
	spinlock_init(&lk_inode_cache, "inode_cache");
	for (int i = 0; i < (int)N_INODE; i++) {
		inode_cache[i].ref = 0;
		inode_cache[i].valid_info = false;
		inode_cache[i].inode_num = INVALID_INODE_NUM;
		sleeplock_init(&inode_cache[i].slk, "inode");
	}
} 

/*--------------------关于inode->index的增删查操作-----------------*/

/* 供free_data_blocks使用
	递归删除inode->index中的一个元素
	返回删除过程中是否遇到空的block_num (文件末尾)
*/
static bool __free_data_blocks(uint32 block_num, uint32 level)
{
	if (block_num == 0)
		return true; // 遇到空的block_num，说明是文件末尾

	// level 0：数据块，直接释放
	if (level == 0) {
		bitmap_free_block(block_num);
		return false; // 不是文件末尾
	}

	// level > 0：索引块，需要递归释放其指向的子块
	buffer_t *buf = buffer_get(block_num);
	uint32 *index_list = (uint32 *)buf->data;
	// 一个 block 中包含 BLOCK_SIZE / 4 个 uint32 索引
	uint32 n_index = BLOCK_SIZE / sizeof(uint32);
	bool meet_empty = false;

	// 递归释放子块
	for (int i = 0; i < n_index; i++) {
		if (__free_data_blocks(index_list[i], level - 1)) {
			meet_empty = true;
			break; // 遇到空的block_num，停止释放
		}
	}

	buffer_put(buf);
	// 释放当前索引块本身
	bitmap_free_block(block_num);
	return meet_empty;
}

/* 释放inode管理的blocks
*/
static void free_data_blocks(uint32 *inode_index)
{
	unsigned int i;
	bool meet_empty = false;

	/* step-1: 释放直接映射的block */
	for (i = 0; i < INODE_INDEX_1; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 0);
		if (meet_empty) return;
	}

	/* step-2: 释放一级间接映射的block */
	for (; i < INODE_INDEX_2; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 1);
		if (meet_empty) return;
	}

	/* step-3: 释放二级间接映射的block */
	for (; i < INODE_INDEX_3; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 2);
		if (meet_empty) return;		
	}

	panic("free_data_blocks: impossible!");
}

/*
	获取inode第logical_block_num个block的物理序号block_num
	调用者保证输入的logical_block_num只有两种情况:
	1. 属于已经分配的区域 (返回block_num)
	2. 将已经分配出去的区域往外扩展1个block (申请block并返回block_num) 
	成功返回block_num, 失败返回-1
*/
static uint32 locate_or_add_block(uint32 *inode_index, uint32 logical_block_num)
{
	uint32 block_num;
	uint32 *index_table;
	buffer_t *buf1 = NULL, *buf2 = NULL;
    uint32 result = -1;
	// 每个 block 能存放的索引数量 (1024)
    uint32 index_per_block = BLOCK_SIZE / sizeof(uint32);

	// 1. 直接映射范围 (0 ~ 9)
    if (logical_block_num < INODE_INDEX_1){
		block_num = inode_index[logical_block_num];
		if (block_num == 0) {
			// 分配新的数据块
			block_num = bitmap_alloc_block();
			if (block_num == (uint32)-1)
				return (uint32)-1;
			inode_index[logical_block_num] = block_num;
			// 新分配的块需要清零
			buffer_t *new_buf = buffer_get(block_num);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}
		return block_num;
	}

	// 2. 一级间接映射范围
	if (logical_block_num < INODE_BLOCK_INDEX_2){
		// 计算相对于一级映射起始位置的偏移
        uint32 rel_idx = logical_block_num - INODE_BLOCK_INDEX_1;
		uint32 l1_idx = rel_idx / index_per_block; // 第几个一级索引块
        uint32 l1_off = rel_idx % index_per_block; // 块内偏移

		// 检查一级索引块是否存在
		uint32 l1_block = inode_index[INODE_INDEX_1 + l1_idx];
		if (l1_block == 0) {
			l1_block = bitmap_alloc_block();
			if (l1_block == (uint32)-1) return -1; 
			inode_index[INODE_INDEX_1 + l1_idx] = l1_block;
			
			buffer_t *new_buf = buffer_get(l1_block);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}

		// 读取一级索引块
		buf1 = buffer_get(l1_block);
		index_table = (uint32 *)buf1->data;
		block_num = index_table[l1_off];
		if (block_num == 0) {
			block_num = bitmap_alloc_block();
			if (block_num == (uint32)-1) {
				result = -1;
				buffer_put(buf1);
				return result;
			}
			index_table[l1_off] = block_num;
			buffer_write(buf1); 

			buffer_t *new_buf = buffer_get(block_num);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}

		result = block_num;
		buffer_put(buf1);
		return result;
	}

	// 3. 二级间接映射范围
	if (logical_block_num < INODE_BLOCK_INDEX_3){
        uint32 rel_idx = logical_block_num - INODE_BLOCK_INDEX_2;
        
		uint32 l2_block = inode_index[INODE_INDEX_2];
		if (l2_block == 0) {
			l2_block = bitmap_alloc_block();
			if (l2_block == (uint32)-1) return -1;
			inode_index[INODE_INDEX_2] = l2_block;
			
			buffer_t *new_buf = buffer_get(l2_block);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}

		uint32 l1_idx = rel_idx / index_per_block; 
		uint32 l1_off = rel_idx % index_per_block; 

		buf2 = buffer_get(l2_block);
		uint32 *l2_table = (uint32 *)buf2->data;
		uint32 l1_block = l2_table[l1_idx];

		if (l1_block == 0) {
			l1_block = bitmap_alloc_block();
			if (l1_block == (uint32)-1) {
				result = -1;
				buffer_put(buf2);
				return result;
			}
			l2_table[l1_idx] = l1_block;
			buffer_write(buf2); 

			buffer_t *new_buf = buffer_get(l1_block);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}

		buf1 = buffer_get(l1_block);
		index_table = (uint32 *)buf1->data;
		block_num = index_table[l1_off];

		if (block_num == 0) {
			block_num = bitmap_alloc_block();
			if (block_num == (uint32)-1) {
				result = -1;
				buffer_put(buf1);
				buffer_put(buf2);
				return result;
			}
			index_table[l1_off] = block_num;
			buffer_write(buf1); 

			buffer_t *new_buf = buffer_get(block_num);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}

		result = block_num;
		buffer_put(buf1);
		buffer_put(buf2);
		return result;
	}

	return -1;
}

/*---------------------关于inode的管理: get dup lock unlock put----------------------*/

/* 磁盘里的inode <-> 内存里的inode
	调用者需要持有ip->slk并设置合理的inode_num
*/
void inode_rw(inode_t *ip, bool write)
{
	assert(sleeplock_holding(&ip->slk), "inode_rw: need slk");
	assert(ip->inode_num != INVALID_INODE_NUM, "inode_rw: invalid inode_num");

	uint32 inodes_per_block = BLOCK_SIZE / sizeof(inode_disk_t);
	uint32 blk = sb.inode_firstblock + ip->inode_num / inodes_per_block;
	uint32 inode_offset = ip->inode_num % inodes_per_block;

	buffer_t *buf = buffer_get(blk);
	inode_disk_t *inodes_table = (inode_disk_t*)buf->data;

	if (write) { // 写回磁盘
		memmove(&inodes_table[inode_offset], &ip->disk_info, sizeof(inode_disk_t));
		buffer_write(buf);
	} else { // 从磁盘读取
		memmove(&ip->disk_info, &inodes_table[inode_offset], sizeof(inode_disk_t));
		ip->valid_info = true;
	}
	buffer_put(buf);
}

/*
	尝试在inode_cache里寻找是否存在目标inode
	如果不存在则申请一个空闲的inode
	如果没有空闲位置直接panic
	核心逻辑: ref++
*/
inode_t *inode_get(uint32 inode_num)
{
	spinlock_acquire(&lk_inode_cache);
	inode_t *free_inode = NULL;
	for (int i = 0; i < (int)N_INODE; i++) {
		inode_t *ip = &inode_cache[i];
		if (ip->ref > 0 && ip->inode_num == inode_num) {
			ip->ref++;
			spinlock_release(&lk_inode_cache);
			return ip;
		}
		if (free_inode == NULL && ip->ref == 0)
			free_inode = ip; 
	}
	/* cache未命中，分配新inode */
	if (free_inode == NULL)
		panic("inode_get: no free inode");
	
	free_inode->ref = 1;
	free_inode->inode_num = inode_num;
	free_inode->valid_info = false;
	spinlock_release(&lk_inode_cache);
	
	sleeplock_acquire(&free_inode->slk);
	inode_rw(free_inode, false);
	sleeplock_release(&free_inode->slk);
	return free_inode;
}

/*
	在磁盘里创建1个新的inode
*/
inode_t *inode_create(uint16 type, uint16 major, uint16 minor)
{
	uint32 inode_num = bitmap_alloc_inode();
	assert(inode_num != (uint32)-1, "inode_create: alloc inode fail");
	
	inode_t *ip = inode_get(inode_num);
	
	sleeplock_acquire(&ip->slk);
	ip->disk_info.type = type;
	ip->disk_info.major = major;
	ip->disk_info.minor = minor;
	ip->disk_info.nlink = 1;
	ip->disk_info.size = 0;
	for (int i = 0; i < INODE_INDEX_3; i++)
		ip->disk_info.index[i] = 0;
	ip->valid_info = true;
	
	inode_rw(ip, true);
	sleeplock_release(&ip->slk);
	return ip;
}

inode_t* inode_dup(inode_t* ip)
{
	spinlock_acquire(&lk_inode_cache);
	ip->ref++;
	spinlock_release(&lk_inode_cache);
	return ip;
}

void inode_lock(inode_t* ip)
{
	sleeplock_acquire(&ip->slk);
	if (!ip->valid_info) {
		inode_rw(ip, false);
		ip->valid_info = true;
	}
}

void inode_unlock(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "inode_unlock: slk");
	sleeplock_release(&ip->slk);
}

void inode_put(inode_t* ip)
{
	spinlock_acquire(&lk_inode_cache);
	assert(ip->ref > 0, "inode_put: ref zero");
	ip->ref--;
	uint32 ref = ip->ref;
	spinlock_release(&lk_inode_cache);
	/* 当引用为0且nlink为0，触发删除 */
	if (ref == 0 && ip->disk_info.nlink == 0) {
		inode_lock(ip);
		inode_delete(ip);
		inode_unlock(ip);
		
		spinlock_acquire(&lk_inode_cache);
		ip->valid_info = false;
		ip->inode_num = INVALID_INODE_NUM;
		spinlock_release(&lk_inode_cache);
	}
}

void inode_delete(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "inode_delete: need slk");
	free_data_blocks(ip->disk_info.index);
	memset(&ip->disk_info, 0, sizeof(ip->disk_info));
	inode_rw(ip, true);
	bitmap_free_inode(ip->inode_num);
}

/*----------------------基于inode的数据读写操作--------------------*/

uint32 inode_read_data(inode_t *ip, uint32 offset, uint32 len, void *dst, bool is_user_dst)
{
	uint32 fsize = ip->disk_info.size;
	if (offset >= fsize) return 0;
	if (offset + len > fsize) len = fsize - offset;

	uint32 done = 0;
	while (done < len) {
		uint32 off = offset + done;
		uint32 lbn = off / BLOCK_SIZE;
		uint32 boff = off % BLOCK_SIZE;
		uint32 take = BLOCK_SIZE - boff;
		if (take > (len - done)) take = len - done;
		
		uint32 pbn = locate_or_add_block(ip->disk_info.index, lbn);
		if (pbn == (uint32)-1) break;
		
		buffer_t *buf = buffer_get(pbn);
		if (is_user_dst)
			uvm_copyout(myproc()->pgtbl, (uint64)dst + done, (uint64)(buf->data + boff), take);
		else
			memmove((uint8*)dst + done, buf->data + boff, take);
		buffer_put(buf);
		done += take;
	}
	return done;
}

uint32 inode_write_data(inode_t *ip, uint32 offset, uint32 len, void *src, bool is_user_src)
{
	uint32 done = 0;
	while (done < len) {
		uint32 off = offset + done;
		uint32 lbn = off / BLOCK_SIZE;
		uint32 boff = off % BLOCK_SIZE;
		uint32 take = BLOCK_SIZE - boff;
		if (take > (len - done)) take = len - done;
		
		uint32 pbn = locate_or_add_block(ip->disk_info.index, lbn);
		if (pbn == (uint32)-1) break;
		
		buffer_t *buf = buffer_get(pbn);
		if (is_user_src)
			uvm_copyin(myproc()->pgtbl, (uint64)(buf->data + boff), (uint64)src + done, take);
		else
			memmove(buf->data + boff, (uint8*)src + done, take);
		buffer_write(buf);
		buffer_put(buf);
		done += take;
	}
	/* 更新文件大小 */
	uint32 newsize = offset + done;
	if (newsize > ip->disk_info.size)
		ip->disk_info.size = newsize;
	return done;
}

static char *inode_type_list[] = {"DATA", "DIR", "DEVICE"};

void inode_print(inode_t *ip, char* name)
{
	assert(sleeplock_holding(&ip->slk), "inode_print: slk");
	spinlock_acquire(&lk_inode_cache);
	printf("inode %s:\n", name);
	printf("ref = %d, inode_num = %d, valid_info = %d\n", ip->ref, ip->inode_num, ip->valid_info);
	printf("type = %s, major = %d, minor = %d, nlink = %d, size = %d\n", inode_type_list[ip->disk_info.type],
		ip->disk_info.major, ip->disk_info.minor, ip->disk_info.nlink, ip->disk_info.size);
	printf("index_list = [ ");
	for (int i = 0; i < INODE_INDEX_1; i++) printf("%d ", ip->disk_info.index[i]);
	printf("] [ ");
	for (int i = INODE_INDEX_1; i < INODE_INDEX_2; i++) printf("%d ", ip->disk_info.index[i]);
	printf("] [ ");
	for (int i = INODE_INDEX_2; i < INODE_INDEX_3; i++) printf("%d ", ip->disk_info.index[i]);
	printf("]\n\n");
	spinlock_release(&lk_inode_cache);
}