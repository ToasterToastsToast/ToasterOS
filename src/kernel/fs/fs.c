#include "mod.h"
#include "../mem/method.h"

super_block_t sb; /* 超级块 */

// 使用状态机进行单次初始化 (参考 fs(2).c)
static volatile int fs_state = 0; // 0=uninit, 1=initializing, 2=ready

file_t file_table[N_FILE]; // 文件资源池
spinlock_t lk_file_table;  // 保护它的锁
/* 初始化file_table */
void file_init()
{
       spinlock_init(&lk_file_table, "file_table");
       for (int i = 0; i < N_FILE; i++)
       {
              file_table[i].ip = NULL;
              file_table[i].readable = false;
              file_table[i].writbale = false;
              file_table[i].offset = 0;
              file_table[i].ref = 0;
       }
}

/* 从file_table中获取1个空闲file */
file_t *file_alloc()
{
       spinlock_acquire(&lk_file_table);
       for (int i = 0; i < N_FILE; i++)
       {
              if (file_table[i].ref == 0)
              {
                     file_table[i].ref = 1;
                     file_table[i].ip = NULL;
                     file_table[i].readable = false;
                     file_table[i].writbale = false;
                     file_table[i].offset = 0;
                     spinlock_release(&lk_file_table);
                     return &file_table[i];
              }
       }
       spinlock_release(&lk_file_table);
       return NULL;
}

/*
       根据路径打开文件 (指定打开模式)
       成功返回file, 失败返回NULL
*/
file_t *file_open(char *path, uint32 open_mode)
{
       inode_t *ip = path_to_inode(path);
       if (ip == NULL)
       {
              if (!(open_mode & FILE_OPEN_CREATE))
                     return NULL;
              ip = path_create_inode(path, INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
              if (ip == NULL)
                     return NULL;
       }

       inode_lock(ip);
       if (ip->disk_info.type == INODE_TYPE_DIVICE)
       {
              if (!device_open_check(ip->disk_info.major, open_mode))
              {
                     inode_unlock(ip);
                     inode_put(ip);
                     return NULL;
              }
       }
       inode_unlock(ip);

       file_t *f = file_alloc();
       if (f == NULL)
       {
              inode_put(ip);
              return NULL;
       }
       f->ip = ip;
       f->readable = (open_mode & FILE_OPEN_READ) != 0;
       f->writbale = (open_mode & FILE_OPEN_WRITE) != 0;
       f->offset = 0;
       return f;
}

/* 关闭文件 */
void file_close(file_t *file)
{
       spinlock_acquire(&lk_file_table);
       if (file->ref == 0)
              panic("file_close: ref underflow");
       file->ref--;
       if (file->ref > 0)
       {
              spinlock_release(&lk_file_table);
              return;
       }
       spinlock_release(&lk_file_table);

       if (file->ip)
              inode_put(file->ip);
       file->ip = NULL;
       file->readable = false;
       file->writbale = false;
       file->offset = 0;
}

/* 读取文件内容, 返回读到的字节数量 */
uint32 file_read(file_t *file, uint32 len, uint64 dst, bool is_user_dst)
{
       if (!file->readable)
              return 0;
       if (file->ip == NULL)
              return 0;

       inode_t *ip = file->ip;
       uint32 ret = 0;

       switch (ip->disk_info.type)
       {
       case INODE_TYPE_DATA:
              inode_lock(ip);
              ret = inode_read_data(ip, file->offset, len, (void *)dst, is_user_dst);
              file->offset += ret;
              inode_unlock(ip);
              break;
       case INODE_TYPE_DIR:
              inode_lock(ip);
              ret = dentry_transmit(ip, dst, len, is_user_dst);
              inode_unlock(ip);
              break;
       case INODE_TYPE_DIVICE:
              ret = device_read_data(ip->disk_info.major, len, dst, is_user_dst);
              break;
       default:
              ret = 0;
              break;
       }
       return ret;
}

/* 读取文件内容, 返回读到的字节数量 */
uint32 file_write(file_t *file, uint32 len, uint64 src, bool is_user_src)
{
       if (!file->writbale)
              return 0;
       if (file->ip == NULL)
              return 0;

       inode_t *ip = file->ip;
       uint32 ret = 0;

       switch (ip->disk_info.type)
       {
       case INODE_TYPE_DATA:
              inode_lock(ip);
              ret = inode_write_data(ip, file->offset, len, (void *)src, is_user_src);
              file->offset += ret;
              inode_unlock(ip);
              break;
       case INODE_TYPE_DIR:
              ret = 0;
              break;
       case INODE_TYPE_DIVICE:
              ret = device_write_data(ip->disk_info.major, len, src, is_user_src);
              break;
       default:
              ret = 0;
              break;
       }
       return ret;
}

/*
       读/写指针的移动
       对于不合理的lseek_offset, 只做尽力而为的移动
       返回新的file->offset
*/
uint32 file_lseek(file_t *file, uint32 lseek_offset, uint32 lseek_flag)
{
       if (file->ip == NULL)
              return (uint32)-1;

       inode_t *ip = file->ip;
       uint32 new_off = file->offset;

       switch (lseek_flag)
       {
       case FILE_LSEEK_SET:
              new_off = lseek_offset;
              break;
       case FILE_LSEEK_ADD:
              new_off = file->offset + lseek_offset;
              break;
       case FILE_LSEEK_SUB:
              new_off = (file->offset > lseek_offset) ? (file->offset - lseek_offset) : 0;
              break;
       default:
              break;
       }

       if (ip->disk_info.type != INODE_TYPE_DIVICE)
       {
              inode_lock(ip);
              if (new_off > ip->disk_info.size)
                     new_off = ip->disk_info.size;
              inode_unlock(ip);
       }

       file->offset = new_off;
       return new_off;
}

/* file->ref++ with lock protect */
file_t *file_dup(file_t *file)
{
       spinlock_acquire(&lk_file_table);
       if (file->ref == 0)
              panic("file_dup: invalid ref");
       file->ref++;
       spinlock_release(&lk_file_table);
       return file;
}

/* 获取文件参数, 成功返回0, 失败返回-1 */
uint32 file_get_stat(file_t *file, uint64 user_dst)
{
       if (file->ip == NULL)
              return (uint32)-1;

       file_stat_t stat;
       inode_t *ip = file->ip;

       inode_lock(ip);
       stat.type = ip->disk_info.type;
       stat.nlink = ip->disk_info.nlink;
       stat.size = ip->disk_info.size;
       stat.inode_num = ip->inode_num;
       inode_unlock(ip);
       stat.offset = file->offset;

       proc_t *p = myproc();
       uvm_copyout(p->pgtbl, user_dst, (uint64)&stat, sizeof(stat));
       return 0;
}


#define FS_TEST_ID 0
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

static void fs_read_superblock()
{
       buffer_t *b = buffer_get(FS_SB_BLOCK);
       memmove(&sb, b->data, sizeof(super_block_t));
       buffer_put(b);
       assert(sb.magic_num == FS_MAGIC, "fs_read_superblock: invalid magic");
}

/* 文件系统初始化 */
void fs_init()
{
       if (fs_state == 2)
              return;

       // Become the one-time initializer.
       if (!__sync_bool_compare_and_swap(&fs_state, 0, 1))
       {
              // Someone else is initializing; wait.
              while (fs_state != 2)
                     ;
              return;
       }

       buffer_init();
       fs_read_superblock();
       sb_print();
       inode_init();
       file_init();
       device_init();
       __sync_synchronize();
       fs_state = 2;
}