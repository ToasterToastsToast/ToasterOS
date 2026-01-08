#include "mod.h"

extern super_block_t sb;

/* 内存中的inode资源集合 */
static inode_t inode_cache[N_INODE];
static spinlock_t lk_inode_cache;

/* inode_cache初始化 */
void inode_init()
{
	spinlock_init(&lk_inode_cache, "inode_cache");
	for (int i = 0; i < (int)N_INODE; i++)
	{
		inode_cache[i].ref = 0;
		inode_cache[i].valid_info = false;
		inode_cache[i].inode_num = INVALID_INODE_NUM;
		memset(&inode_cache[i].disk_info, 0, sizeof(inode_disk_t));
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
		return true;

	if (level == 0)
	{
		bitmap_free_block(block_num);
		return false;
	}

	buffer_t *buf = buffer_get(block_num);
	uint32 *index_list = (uint32 *)buf->data;
	uint32 n_index = BLOCK_SIZE / sizeof(uint32);
	bool meet_empty = false;

	for (int i = 0; i < n_index; i++)
	{
		meet_empty = __free_data_blocks(index_list[i], level - 1);
		index_list[i] = 0; // 清零索引
		if (meet_empty)
			break;
	}

	buffer_write(buf); // 写回清零后的索引
	buffer_put(buf);
	bitmap_free_block(block_num);
	return meet_empty;
}

/* 释放inode管理的blocks */
static void free_data_blocks(uint32 *inode_index)
{
	unsigned int i;
	bool meet_empty = false;

	for (i = 0; i < INODE_INDEX_1; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 0);
		if (meet_empty)
			return;
	}

	for (; i < INODE_INDEX_2; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 1);
		if (meet_empty)
			return;
	}

	for (; i < INODE_INDEX_3; i++)
	{
		meet_empty = __free_data_blocks(inode_index[i], 2);
		if (meet_empty)
			return;
	}

	panic("free_data_blocks: impossible!");
}

/* 获取inode第logical_block_num个block的物理序号block_num
   仅查找，不分配新块
   成功返回block_num, 不存在返回0
*/
static uint32 locate_block(uint32 *inode_index, uint32 logical_block_num)
{
	uint32 index_per_block = BLOCK_SIZE / sizeof(uint32);

	if (logical_block_num >= INODE_BLOCK_INDEX_3)
		return 0;

	// 直接映射
	if (logical_block_num < INODE_BLOCK_INDEX_1)
		return inode_index[logical_block_num];

	// 一级间接映射
	if (logical_block_num < INODE_BLOCK_INDEX_2)
	{
		uint32 rel_idx = logical_block_num - INODE_BLOCK_INDEX_1;
		uint32 l1_idx = rel_idx / index_per_block;
		uint32 l1_off = rel_idx % index_per_block;

		uint32 l1_block = inode_index[INODE_INDEX_1 + l1_idx];
		if (l1_block == 0)
			return 0;

		buffer_t *buf1 = buffer_get(l1_block);
		uint32 block_num = ((uint32 *)buf1->data)[l1_off];
		buffer_put(buf1);
		return block_num;
	}

	// 二级间接映射
	uint32 rel_idx = logical_block_num - INODE_BLOCK_INDEX_2;
	uint32 l1_idx = rel_idx / index_per_block;
	uint32 l1_off = rel_idx % index_per_block;

	uint32 l2_block = inode_index[INODE_INDEX_2];
	if (l2_block == 0)
		return 0;

	buffer_t *buf2 = buffer_get(l2_block);
	uint32 l1_block = ((uint32 *)buf2->data)[l1_idx];
	buffer_put(buf2);
	if (l1_block == 0)
		return 0;

	buffer_t *buf1 = buffer_get(l1_block);
	uint32 block_num = ((uint32 *)buf1->data)[l1_off];
	buffer_put(buf1);
	return block_num;
}

/* 获取inode第logical_block_num个block的物理序号block_num
   如果不存在则分配新块
   成功返回block_num, 失败返回-1
*/
static uint32 locate_or_add_block(uint32 *inode_index, uint32 logical_block_num)
{
	uint32 block_num;
	uint32 *index_table;
	buffer_t *buf1 = NULL, *buf2 = NULL;
	uint32 result = -1;
	uint32 index_per_block = BLOCK_SIZE / sizeof(uint32);

	if (logical_block_num >= INODE_BLOCK_INDEX_3)
		return (uint32)-1;

	// 直接映射
	if (logical_block_num < INODE_BLOCK_INDEX_1)
	{
		block_num = inode_index[logical_block_num];
		if (block_num == 0)
		{
			block_num = bitmap_alloc_block();
			if (block_num == (uint32)-1)
				return (uint32)-1;
			inode_index[logical_block_num] = block_num;
			buffer_t *new_buf = buffer_get(block_num);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}
		return block_num;
	}

	// 一级间接映射
	if (logical_block_num < INODE_BLOCK_INDEX_2)
	{
		uint32 rel_idx = logical_block_num - INODE_BLOCK_INDEX_1;
		uint32 l1_idx = rel_idx / index_per_block;
		uint32 l1_off = rel_idx % index_per_block;

		uint32 l1_block = inode_index[INODE_INDEX_1 + l1_idx];
		if (l1_block == 0)
		{
			l1_block = bitmap_alloc_block();
			if (l1_block == (uint32)-1)
				return -1;
			inode_index[INODE_INDEX_1 + l1_idx] = l1_block;

			buffer_t *new_buf = buffer_get(l1_block);
			memset(new_buf->data, 0, BLOCK_SIZE);
			buffer_write(new_buf);
			buffer_put(new_buf);
		}

		buf1 = buffer_get(l1_block);
		index_table = (uint32 *)buf1->data;
		block_num = index_table[l1_off];
		if (block_num == 0)
		{
			block_num = bitmap_alloc_block();
			if (block_num == (uint32)-1)
			{
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

	// 二级间接映射
	if (logical_block_num < INODE_BLOCK_INDEX_3)
	{
		uint32 rel_idx = logical_block_num - INODE_BLOCK_INDEX_2;

		uint32 l2_block = inode_index[INODE_INDEX_2];
		if (l2_block == 0)
		{
			l2_block = bitmap_alloc_block();
			if (l2_block == (uint32)-1)
				return -1;
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

		if (l1_block == 0)
		{
			l1_block = bitmap_alloc_block();
			if (l1_block == (uint32)-1)
			{
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

		if (block_num == 0)
		{
			block_num = bitmap_alloc_block();
			if (block_num == (uint32)-1)
			{
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

void inode_rw(inode_t *ip, bool write)
{
	assert(sleeplock_holding(&ip->slk), "inode_rw: need slk");
	assert(ip->inode_num != INVALID_INODE_NUM, "inode_rw: invalid inode_num");

	uint32 inodes_per_block = BLOCK_SIZE / sizeof(inode_disk_t);
	uint32 blk = sb.inode_firstblock + ip->inode_num / inodes_per_block;
	uint32 inode_offset = ip->inode_num % inodes_per_block;

	buffer_t *buf = buffer_get(blk);
	inode_disk_t *inodes_table = (inode_disk_t *)buf->data;

	if (write)
	{
		memmove(&inodes_table[inode_offset], &ip->disk_info, sizeof(inode_disk_t));
		buffer_write(buf);
	}
	else
	{
		memmove(&ip->disk_info, &inodes_table[inode_offset], sizeof(inode_disk_t));
		ip->valid_info = true;
	}
	buffer_put(buf);
}

/* 尝试在inode_cache里寻找是否存在目标inode
   如果不存在则申请一个空闲的inode
   如果没有空闲位置直接panic
   核心逻辑: ref++
*/
inode_t *inode_get(uint32 inode_num)
{
	inode_t *ip = NULL;

	spinlock_acquire(&lk_inode_cache);

	// cache hit
	for (int i = 0; i < (int)N_INODE; i++)
	{
		inode_t *tmp = &inode_cache[i];
		if (tmp->ref > 0 && tmp->inode_num == inode_num)
		{
			tmp->ref++;
			ip = tmp;
			spinlock_release(&lk_inode_cache);
			return ip;
		}
	}

	// cache miss: allocate empty slot
	inode_t *free_inode = NULL;
	for (int i = 0; i < (int)N_INODE; i++)
	{
		inode_t *tmp = &inode_cache[i];
		if (tmp->ref == 0)
		{
			free_inode = tmp;
			break;
		}
	}

	if (free_inode == NULL)
	{
		spinlock_release(&lk_inode_cache);
		panic("inode_get: no free inode");
	}

	free_inode->ref = 1;
	free_inode->inode_num = inode_num;
	free_inode->valid_info = false;
	ip = free_inode;

	spinlock_release(&lk_inode_cache);
	return ip;
}

/* 在磁盘里创建1个新的inode
   注意: 返回的inode未上锁
*/
inode_t *inode_create(uint16 type, uint16 major, uint16 minor)
{
	uint32 inode_num = bitmap_alloc_inode();
	assert(inode_num != (uint32)-1, "inode_create: alloc inode fail");

	inode_t *ip = inode_get(inode_num);

	sleeplock_acquire(&ip->slk);
	memset(&ip->disk_info, 0, sizeof(inode_disk_t));
	ip->disk_info.type = type;
	ip->disk_info.major = major;
	ip->disk_info.minor = minor;
	ip->disk_info.nlink = 1;
	ip->disk_info.size = 0;
	for (int i = 0; i < INODE_INDEX_3; i++)
		ip->disk_info.index[i] = 0;

	// For directory inode, allocate one data block for dentries.
	if (type == INODE_TYPE_DIR)
	{
		uint32 block_num = bitmap_alloc_block();
		if (block_num == (uint32)-1)
			panic("inode_create: no free data block for dir");
		ip->disk_info.index[0] = block_num;
		buffer_t *buf = buffer_get(block_num);
		memset(buf->data, 0, BLOCK_SIZE);
		buffer_write(buf);
		buffer_put(buf);
	}

	ip->valid_info = true;
	inode_rw(ip, true);
	sleeplock_release(&ip->slk);
	return ip;
}

inode_t *inode_dup(inode_t *ip)
{
	spinlock_acquire(&lk_inode_cache);
	assert(ip->ref > 0, "inode_dup: invalid ref");
	ip->ref++;
	spinlock_release(&lk_inode_cache);
	return ip;
}

void inode_lock(inode_t *ip)
{
	sleeplock_acquire(&ip->slk);
	if (!ip->valid_info)
	{
		inode_rw(ip, false);
		ip->valid_info = true;
	}
}

void inode_unlock(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "inode_unlock: slk");
	sleeplock_release(&ip->slk);
}

void inode_put(inode_t *ip)
{
	bool do_delete = false;

	spinlock_acquire(&lk_inode_cache);
	assert(ip->ref > 0, "inode_put: ref zero");
	ip->ref--;
	if (ip->ref == 0 && ip->valid_info && ip->disk_info.nlink == 0)
		do_delete = true;
	spinlock_release(&lk_inode_cache);

	if (do_delete)
	{
		inode_lock(ip);
		inode_delete(ip);
		ip->valid_info = false;
		ip->inode_num = INVALID_INODE_NUM;
		memset(&ip->disk_info, 0, sizeof(inode_disk_t));
		inode_unlock(ip);
	}
}

void inode_delete(inode_t *ip)
{
	assert(sleeplock_holding(&ip->slk), "inode_delete: need slk");
	assert(ip->inode_num != INVALID_INODE_NUM, "inode_delete: invalid inode_num");

	// free all data/index blocks managed by this inode
	free_data_blocks(ip->disk_info.index);

	// free inode bitmap
	bitmap_free_inode(ip->inode_num);

	// clear on-disk inode region (best-effort)
	memset(&ip->disk_info, 0, sizeof(inode_disk_t));
	ip->valid_info = true;
	inode_rw(ip, true);
}

/*----------------------基于inode的数据读写操作--------------------*/

uint32 inode_read_data(inode_t *ip, uint32 offset, uint32 len, void *dst, bool is_user_dst)
{
	assert(sleeplock_holding(&ip->slk), "inode_read_data: need slk");

	uint32 fsize = ip->disk_info.size;
	if (offset >= fsize)
		return 0;
	if (offset + len > fsize)
		len = fsize - offset;

	uint32 done = 0;
	proc_t *p = myproc();

	while (done < len)
	{
		uint32 off = offset + done;
		uint32 lbn = off / BLOCK_SIZE;
		uint32 boff = off % BLOCK_SIZE;
		uint32 take = BLOCK_SIZE - boff;
		if (take > (len - done))
			take = len - done;

		// 使用 locate_block 而非 locate_or_add_block，读取不应分配新块
		uint32 pbn = locate_block(ip->disk_info.index, lbn);
		if (pbn == 0)
			panic("inode_read_data: missing block");

		buffer_t *buf = buffer_get(pbn);
		if (is_user_dst)
			uvm_copyout(p->pgtbl, (uint64)dst + done, (uint64)(buf->data + boff), take);
		else
			memmove((uint8 *)dst + done, buf->data + boff, take);
		buffer_put(buf);
		done += take;
	}
	return done;
}

uint32 inode_write_data(inode_t *ip, uint32 offset, uint32 len, void *src, bool is_user_src)
{
	assert(sleeplock_holding(&ip->slk), "inode_write_data: need slk");

	// For stream data inode, do not allow holes.
	if (ip->disk_info.type == INODE_TYPE_DATA && offset > ip->disk_info.size)
		return 0;

	uint64 end = (uint64)offset + (uint64)len;
	if (end > INODE_MAX_SIZE)
		return 0;

	uint32 old_blocks = (ip->disk_info.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	uint32 new_blocks = ((uint32)end + BLOCK_SIZE - 1) / BLOCK_SIZE;
	if (new_blocks > INODE_BLOCK_INDEX_3)
		return 0;

	// allocate new blocks one-by-one to satisfy locate_or_add_block's contract
	for (uint32 lb = old_blocks; lb < new_blocks; lb++)
	{
		uint32 block_num = locate_or_add_block(ip->disk_info.index, lb);
		if (block_num == (uint32)-1)
			return 0;
	}

	uint32 done = 0;
	proc_t *p = myproc();

	while (done < len)
	{
		uint32 off = offset + done;
		uint32 lbn = off / BLOCK_SIZE;
		uint32 boff = off % BLOCK_SIZE;
		uint32 take = BLOCK_SIZE - boff;
		if (take > (len - done))
			take = len - done;

		uint32 pbn = locate_block(ip->disk_info.index, lbn);
		if (pbn == 0)
			panic("inode_write_data: missing block");

		buffer_t *buf = buffer_get(pbn);
		if (is_user_src)
			uvm_copyin(p->pgtbl, (uint64)(buf->data + boff), (uint64)src + done, take);
		else
			memmove(buf->data + boff, (uint8 *)src + done, take);
		buffer_write(buf);
		buffer_put(buf);
		done += take;
	}

	uint32 newsize = offset + done;
	if (newsize > ip->disk_info.size)
	{
		ip->disk_info.size = newsize;
		inode_rw(ip, true); // 更新磁盘上的size
	}
	return done;
}

static char *inode_type_list[] = {"DATA", "DIR", "DEVICE"};

/* 输出inode信息(for debug) */
void inode_print(inode_t *ip, char *name)
{
	assert(sleeplock_holding(&ip->slk), "inode_print: slk");

	/* 先在锁内读取所有数据 */
	spinlock_acquire(&lk_inode_cache);
	uint32 ref = ip->ref;
	uint32 inode_num = ip->inode_num;
	bool valid_info = ip->valid_info;
	spinlock_release(&lk_inode_cache);

	/* 读取磁盘信息 */
	uint16 type = ip->disk_info.type;
	uint16 major = ip->disk_info.major;
	uint16 minor = ip->disk_info.minor;
	uint16 nlink = ip->disk_info.nlink;
	uint32 size = ip->disk_info.size;
	uint32 index[INODE_INDEX_3];
	for (int i = 0; i < INODE_INDEX_3; i++)
	{
		index[i] = ip->disk_info.index[i];
	}

	/* 在没有锁的情况下打印 */
	printf("inode %s:\n", name);
	printf("ref = %d, inode_num = %d, valid_info = %d\n", ref, inode_num, valid_info);
	printf("type = %s, major = %d, minor = %d, nlink = %d, size = %d\n",
		   inode_type_list[type], major, minor, nlink, size);
	printf("index_list = [ ");
	for (int i = 0; i < INODE_INDEX_1; i++)
		printf("%d ", index[i]);
	printf("] [ ");
	for (int i = INODE_INDEX_1; i < INODE_INDEX_2; i++)
		printf("%d ", index[i]);
	printf("] [ ");
	for (int i = INODE_INDEX_2; i < INODE_INDEX_3; i++)
		printf("%d ", index[i]);
	printf("]\n\n");
}