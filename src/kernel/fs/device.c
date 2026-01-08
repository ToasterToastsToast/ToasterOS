#include "mod.h"

device_t device_table[N_DEVICE];

/* 标准输入设备 */
static uint32 device_stdin_read(uint32 len, uint64 dst, bool is_user_dst)
{
    return cons_read(len, dst, is_user_dst);
}

/* 标准输出设备 */
static uint32 device_stdout_write(uint32 len, uint64 src, bool is_user_src)
{
    return cons_write(len, src, is_user_src);
}

/* 标准错误输出设备 */
static uint32 device_stderr_write(uint32 len, uint64 src, bool is_user_src)
{
    printf("ERROR: ");
    return cons_write(len, src, is_user_src);
}

/* 无限0流 */
static uint32 device_zero_read(uint32 len, uint64 dst, bool is_user_dst)
{
    uint32 write_len = 0, cut_len = 0;

    uint64 src = (uint64)pmem_alloc(true);
    proc_t *p = myproc();

    while (write_len < len)
    {
        cut_len = MIN(len - write_len, PGSIZE);

        if (is_user_dst)
            uvm_copyout(p->pgtbl, dst, src, cut_len);
        else
            memmove((void *)dst, (void *)src, cut_len);

        dst += cut_len;
        write_len += cut_len;
    }

    pmem_free(src, true);

    return write_len;
}

/* 空设备读取 */
static uint32 device_null_read(uint32 len, uint64 dst, bool is_user_dst)
{
    return 0;
}

/* 空设备写入 */
static uint32 device_null_write(uint32 len, uint64 src, bool is_user_src)
{
    return len;
}

/* 彩蛋: 笨蛋GPT */
static uint32 device_gpt0_write(uint32 len, uint64 src, bool is_user_src)
{
    char tmp[STR_MAXLEN + 1];
    proc_t *p = myproc();

    tmp[len] = '\0';

    if (is_user_src)
        uvm_copyin(p->pgtbl, (uint64)tmp, src, len);
    else
        memmove(tmp, (void *)src, len);

    if (strncmp(tmp, "Hello", len) == 0)
    {
        printf("Hi, I am gpt0!\n");
    }
    else if (strncmp(tmp, "Guess who I am", len) == 0)
    {
        printf("Your procid is %d and name is %s.\n", p->pid, p->name);
    }
    else if (strncmp(tmp, "How many free memory left", len) == 0)
    {
        uint32 kernel_free_pages, user_free_pages;
        pmem_stat(&kernel_free_pages, &user_free_pages);
        printf("We have %d free pages in kernel space, %d free pages in user space!\n",
               kernel_free_pages, user_free_pages);
    }
    else if (strncmp(tmp, "Good job", len) == 0)
    {
        printf("Thanks for your kind words!\n");
    }
    else
    {
        printf("Sorry, I can not understand it.\n");
    }

    return len;
}

/* 注册设备 */
static void device_register(uint32 index, char *name,
                            uint32 (*read)(uint32, uint64, bool),
                            uint32 (*write)(uint32, uint64, bool))
{
    memmove(device_table[index].name, name, MAXLEN_FILENAME);
    device_table[index].read = read;
    device_table[index].write = write;
}

/* 初始化device_table与挂载设备文件 */
void device_init()
{
    int idx = 0;
    // 预先清理设备表
    for (; idx < N_DEVICE; ++idx)
    {
        device_table[idx].read = NULL;
        device_table[idx].write = NULL;
        memset(device_table[idx].name, 0, MAXLEN_FILENAME);
    }

    // 静态注册核心设备
    device_register(INODE_MAJOR_STDIN, "stdin", device_stdin_read, NULL);
    device_register(INODE_MAJOR_STDOUT, "stdout", NULL, device_stdout_write);
    device_register(INODE_MAJOR_STDERR, "stderr", NULL, device_stderr_write);
    device_register(INODE_MAJOR_ZERO, "zero", device_zero_read, NULL);
    device_register(INODE_MAJOR_NULL, "null", device_null_read, device_null_write);
    device_register(INODE_MAJOR_GPT0, "gpt0", NULL, device_gpt0_write);

    // 确保 /dev 目录存在
    inode_t *dev_root = path_to_inode("/dev");
    if (!dev_root)
    {
        dev_root = path_create_inode("/dev", INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
    }
    if (dev_root)
        inode_put(dev_root);

    // 遍历设备表，在文件系统中创建对应的设备节点
    for (idx = 0; idx < N_DEVICE; idx++)
    {
        char *dev_name = device_table[idx].name;
        if (dev_name[0] == '\0')
            continue;

        char full_path[MAXLEN_FILENAME + 8];
        int name_len = strlen(dev_name);

        // 构造路径字符串: /dev/name
        memmove(full_path, "/dev/", 5);
        if (name_len >= MAXLEN_FILENAME)
            name_len = MAXLEN_FILENAME - 1;
        memmove(full_path + 5, dev_name, name_len);
        full_path[5 + name_len] = '\0';

        inode_t *node = path_to_inode(full_path);
        if (node == NULL)
        {
            // 若节点不存在则创建，major号对应数组索引
            node = path_create_inode(full_path, INODE_TYPE_DIVICE, (uint16)idx, INODE_MINOR_DEFAULT);
        }
        if (node)
            inode_put(node);
    }
}

/* 验证设备访问权限及主设备号合法性 */
bool device_open_check(uint16 major_id, uint32 mode)
{
    // 基础范围与存在性检查
    if (major_id >= N_DEVICE || device_table[major_id].name[0] == '\0')
    {
        return false;
    }

    device_t *target_dev = &device_table[major_id];

    // 检查读权限要求
    if ((mode & FILE_OPEN_READ) && !target_dev->read)
    {
        return false;
    }
    // 检查写权限要求
    if ((mode & FILE_OPEN_WRITE) && !target_dev->write)
    {
        return false;
    }

    return true;
}

/* 调用底层设备驱动读取数据 */
uint32 device_read_data(uint16 major_id, uint32 size, uint64 buffer, bool from_user)
{
    if (major_id < N_DEVICE)
    {
        uint32 (*read_fn)(uint32, uint64, bool) = device_table[major_id].read;
        if (read_fn != NULL)
        {
            return read_fn(size, buffer, from_user);
        }
    }
    return 0;
}

/* 调用底层设备驱动写入数据 */
uint32 device_write_data(uint16 major_id, uint32 size, uint64 buffer, bool from_user)
{
    if (major_id < N_DEVICE)
    {
        uint32 (*write_fn)(uint32, uint64, bool) = device_table[major_id].write;
        if (write_fn != NULL)
        {
            return write_fn(size, buffer, from_user);
        }
    }
    return 0;
}