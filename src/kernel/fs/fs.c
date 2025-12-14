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

{ /* 确保只初始化一次 */
    if (fs_initialized)
        return;

    /* 1. 初始化buffer系统 */
    buffer_init();

    /* 2. 读入超级块 (位于block 0) */
    buffer_t *buf = buffer_get(FS_SB_BLOCK);

    /* 将超级块数据复制到全局变量sb中 */
    memmove(&sb, buf->data, sizeof(super_block_t));

    buffer_put(buf);

    /* 3. 验证魔数 */
    if (sb.magic_num != FS_MAGIC)
    {
        panic("fs_init: invalid filesystem magic number");
    }

    /* 4. 打印磁盘布局信息 */
    sb_print();

    fs_initialized = true;
    printf("fs.c: File system initialized\n");
}