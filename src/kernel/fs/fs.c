#include "mod.h"

super_block_t sb; /* 超级块 */
static bool fs_initialized = false;

/* 基于superblock输出磁盘布局信息 (for debug) */
static void sb_print()
{
	printf("\ndisk layout information:\n");
	printf("1. super block:  block[0]\n");
	printf("2. inode bitmap: block[%d - %d]\n", sb.inode_bitmap_firstblock,
		sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1);
	printf("3. inode region: block[%d - %d]\n", sb.inode_firstblock,
		sb.inode_firstblock + sb.inode_blocks - 1);
	printf("4. data bitmap:  block[%d - %d]\n", sb.data_bitmap_firstblock,
		sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1);
	printf("5. data region:  block[%d - %d]\n", sb.data_firstblock,
		sb.data_firstblock + sb.data_blocks - 1);
	printf("block size = %d Byte, total size = %d MB, total inode = %d\n\n", sb.block_size,
		(int)((unsigned long long)(sb.total_blocks) * sb.block_size / 1024 / 1024), sb.total_inodes);
}

/* 文件系统初始化 */
void fs_init()
{
    if (fs_initialized)
        return;

	// 初始化缓冲系统
	buffer_init();
	// 初始化inode缓存与锁 【新增】
	inode_init();

	// 读取超级块
	buffer_t *b = buffer_get(FS_SB_BLOCK);
	memmove(&sb, b->data, sizeof(super_block_t));
	buffer_put(b);

	// 验证魔数
    if (sb.magic_num != FS_MAGIC)
    {
        panic("fs_init: invalid filesystem magic number");
    }

	// 打印布局信息
	sb_print();

    fs_initialized = true;
    printf("fs.c: File system initialized\n");

    /* ================= 以下为测试代码 ================= */

	// 测试1: inode的访问 + 创建 + 删除
	printf("============= test 1 begin =============\n\n");

	inode_t *rooti, *ip_1, *ip_2;
	
	rooti = inode_get(ROOT_INODE);
	inode_lock(rooti);
	inode_print(rooti, "root");
	inode_unlock(rooti);

	bitmap_print(false);

	ip_1 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	ip_2 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_lock(ip_1);
	inode_lock(ip_2);
	inode_dup(ip_2);

	inode_print(ip_1, "dir");
	inode_print(ip_2, "data");
	
	bitmap_print(false);

	ip_1->disk_info.nlink = 0;
	ip_2->disk_info.nlink = 0;
	inode_unlock(ip_1);
	inode_unlock(ip_2);
	inode_put(ip_1);
	inode_put(ip_2);

	bitmap_print(false);

	inode_put(ip_2);
	bitmap_print(false);

	printf("============= test 1 end =============\n\n");


	// 测试2: 写入和读取inode管理的数据
	printf("============= test 2 begin =============\n\n");

	// 提前申请大块内存，防止 buffer cache 打乱物理内存布局
    char *big_src; 
    char big_dst[9];
    big_dst[8] = 0;

    char *pages[5];
    for (int i = 0; i < 5; i++) pages[i] = pmem_alloc(true);
    // 简单判断分配方向，确定 buffer 起始地址
    if (pages[1] > pages[0]) big_src = pages[0];
    else big_src = pages[4];

    // 填充数据
    for (uint32 i = 0; i < 5 * (PGSIZE / 8); i++)
        for (uint32 j = 0; j < 8; j++)
            big_src[i * 8 + j] = 'A' + j;

	inode_t *ip_test2;
	uint32 len, cut_len;

	int small_src[10], small_dst[10];
	for (int i = 0; i < 10; i++) small_src[i] = i;
	
	ip_test2 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_lock(ip_test2);

	printf("writing small data...\n");
	cut_len = 10 * sizeof(int);
	for (uint32 offset = 0; offset < 400 * cut_len; offset += cut_len) {
		len = inode_write_data(ip_test2, offset, cut_len, small_src, false);
		assert(len == cut_len, "write fail 1!");
	}

	len = inode_read_data(ip_test2, 120 * cut_len + 4, cut_len, small_dst, false);
	assert(len == cut_len, "read fail 1!");
	printf("read small data:");
	for (int i = 0; i < 10; i++) printf(" %d", small_dst[i]);
	printf("\n");

	ip_test2->disk_info.nlink = 0;
	inode_unlock(ip_test2);
	inode_put(ip_test2);

	/* 大批量读写测试 */
	ip_test2 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_lock(ip_test2);
	inode_print(ip_test2, "big_data");

	printf("writing big data...\n");
	cut_len = PGSIZE * 4 + 1110;
	// 减小循环次数以适应虚拟机内存限制，但足以触发多级索引
	for (uint32 offset = 0; offset < cut_len * 100; offset += cut_len)
	{
		len = inode_write_data(ip_test2, offset, cut_len, big_src, false);
		assert(len == cut_len, "write fail 2!");
	}
	inode_print(ip_test2, "big_data");

	len = inode_read_data(ip_test2, cut_len * 100 - 8, 8, big_dst, false);
	assert(len == 8, "read fail 2");
	printf("read big data tail: %s\n", big_dst);

	ip_test2->disk_info.nlink = 0;
	inode_unlock(ip_test2);
	inode_put(ip_test2);

    // 释放测试内存
    for (int i = 0; i < 5; i++) pmem_free((uint64)pages[i], true);

	printf("============= test 2 end =============\n\n");


	// 测试3: 目录项操作
	printf("============= test 3 begin =============\n\n");

	inode_t *ip_3_1, *ip_3_2, *ip_3_3;
	uint32 inode_num_1, inode_num_2, inode_num_3;
	uint32 offset;
	char tmp[10];

	tmp[9] = 0;
	cut_len = 9;
	rooti = inode_get(ROOT_INODE);

	inode_lock(rooti);
	inode_num_1 = dentry_search(rooti, "ABCD.txt");
	inode_num_2 = dentry_search(rooti, "abcd.txt");
	inode_num_3 = dentry_search(rooti, ".");
	if (inode_num_1 == INVALID_INODE_NUM || inode_num_2 == INVALID_INODE_NUM) {
		panic("invalid inode num!");
	}
	dentry_print(rooti);
	inode_unlock(rooti);

	ip_3_1 = inode_get(inode_num_1); inode_lock(ip_3_1);
	ip_3_2 = inode_get(inode_num_2); inode_lock(ip_3_2);
	ip_3_3 = inode_get(inode_num_3); inode_lock(ip_3_3);

	inode_print(ip_3_1, "ABCD.txt");
	
	len = inode_read_data(ip_3_1, 0, cut_len, tmp, false);
	printf("\nread data ABCD: %s\n", tmp);

	len = inode_read_data(ip_3_2, 0, cut_len, tmp, false);
	printf("read data abcd: %s\n", tmp);

	inode_unlock(ip_3_1); inode_put(ip_3_1);
	inode_unlock(ip_3_2); inode_put(ip_3_2);
	inode_unlock(ip_3_3); inode_put(ip_3_3);

	/* 创建和删除dentry */
	inode_lock(rooti);

	ip_3_1 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);	
	offset = dentry_create(rooti, ip_3_1->inode_num, "new_dir");
	inode_num_1 = dentry_search(rooti, "new_dir");
	printf("new dentry offset = %d, inode_num = %d\n", offset, inode_num_1);
	
	dentry_print(rooti);

	inode_num_2 = dentry_delete(rooti, "new_dir");
	assert(inode_num_1 == inode_num_2, "inode num is not equal!");

	dentry_print(rooti);

	inode_unlock(rooti);
	inode_put(rooti);
	printf("============= test 3 end =============\n\n");


	// 测试4: 路径解析
	printf("============= test 4 begin =============\n\n");

	inode_t *ip_4, *ip_5;
	
	rooti = inode_get(ROOT_INODE);
	ip_1 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	ip_2 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	inode_t *ip_3 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
	
	inode_lock(rooti);
	inode_lock(ip_1);
	inode_lock(ip_2);
	inode_lock(ip_3);

	if (dentry_create(rooti, ip_1->inode_num, "AABBC") == -1) panic("dentry_create fail 1!");
	if (dentry_create(ip_1, ip_2->inode_num, "aaabb") == -1) panic("dentry_create fail 2!");
	if (dentry_create(ip_2, ip_3->inode_num, "file.txt") == -1) panic("dentry_create fail 3!");

	char tmp1[] = "This is file context!";
	char tmp2[32];
	inode_write_data(ip_3, 0, sizeof(tmp1), tmp1, false);

	inode_rw(rooti, true);
	inode_rw(ip_1, true);
	inode_rw(ip_2, true);
    inode_rw(ip_3, true); // 确保写入回磁盘

	inode_unlock(rooti); inode_put(rooti);
	inode_unlock(ip_1); inode_put(ip_1);
	inode_unlock(ip_2); inode_put(ip_2);
	inode_unlock(ip_3); inode_put(ip_3);

	char *path = "///AABBC///aaabb/file.txt";
	char name[MAXLEN_FILENAME];

	ip_4 = path_to_inode(path);
	if (ip_4 == NULL) panic("invalid ip_4");

	ip_5 = path_to_parent_inode(path, name);
	if (ip_5 == NULL) panic("invalid ip_5");
	
	printf("get a name = %s\n", name);

	inode_lock(ip_4);
	inode_lock(ip_5);

	inode_print(ip_4, "file.txt");
	inode_print(ip_5, "aaabb");

	inode_read_data(ip_4, 0, 32, tmp2, false);
	printf("read path data: %s\n", tmp2);

	inode_unlock(ip_4); inode_put(ip_4);
	inode_unlock(ip_5); inode_put(ip_5);

	printf("============= test 4 end =============\n");
    
    while(1);
}