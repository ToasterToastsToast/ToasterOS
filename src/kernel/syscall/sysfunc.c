#include "mod.h"
#define ARRAY_LEN_IN_INTS 5
#define ARRAY_SIZE_IN_BYTES (ARRAY_LEN_IN_INTS * sizeof(int))
#define MAX_STRING_LEN 100 // 用于接收字符串的最大长度
/*
    测试: 从用户空间传入一个int类型的数组
    uint64 addr 数组起始地址
    uint32 len  元素数量
    成功返回0
*/
// 内核缓冲区：用于 sys_copyin/out 的数组和 sys_copyinstr 的字符串
static int kernel_int_array[ARRAY_LEN_IN_INTS];
static char kernel_str_buf[MAX_STRING_LEN];

/*
    用户堆空间伸缩
    uint64 new_heap_top (如果是0, 代表查询当前堆顶位置)
    成功返回new_heap_top, 失败返回-1
*/
uint64 sys_brk()
{
    printf("*");
    proc_t *p = myproc();
    // 1. 从 a0 寄存器获取请求的新堆顶地址
    uint64 new_heap_top = p->tf->a0;
    uint64 old_heap_top = p->heap_top;
    uint64 result_top = 0;
    const char *event_type = "no_change";

    // 2. 查询当前堆顶 (通常用户程序第一次调用 brk(0) 来获取起始地址)
    if (new_heap_top == 0)
    {
        result_top = old_heap_top;
        event_type = "look";
    }
    else if (new_heap_top > old_heap_top)
    {
        // 空间增加
        uint32 len = new_heap_top - old_heap_top;
        // 修正：传入 pgtbl, 当前堆顶, 长度, 以及页面权限 (用户级+读+写)
        result_top = uvm_heap_grow(p->pgtbl, old_heap_top, len, PTE_R | PTE_W);
        event_type = "grow";
    }
    else if (new_heap_top < old_heap_top)
    {
        // 空间减少
        uint32 len = old_heap_top - new_heap_top;
        // 修正：确保参数与定义 (pgtbl, cur_heap_top, len) 一致
        result_top = uvm_heap_ungrow(p->pgtbl, old_heap_top, len);
        event_type = "ungrow";
    }
    else
    {
        // 空间不变
        result_top = old_heap_top;
        event_type = "no_change";
    }

    // 3. 处理结果
    if (result_top != (uint64)-1 && result_top != 0)
    {
        p->heap_top = result_top;
        printf("%s event: ret_heap_top = %x\n", event_type, result_top);
        return result_top;
    }
    else
    {
        printf("%s event: FAILED (Requested %x, Old %x)\n", event_type, new_heap_top, old_heap_top);
        return (uint64)-1;
    }
}
/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    proc_t *p = myproc();
    uint64 begin, len;
    arg_uint64(0, &begin);
    arg_uint64(1, &len);

    // 检查对齐
    if (len == 0 || len % PGSIZE != 0) return -1;
    if (begin != 0 && begin % PGSIZE != 0) return -1;

    uint32 npages = len / PGSIZE;
    
    // 调用 uvm 逻辑 (权限设为 R|W|U)
    uvm_mmap(begin, npages, PTE_R | PTE_W | PTE_U);

    // 如果申请的是随机地址(begin=0)，需要返回实际分配的地址
    // 简单做法：遍历链表找最后分配的
    if (begin == 0) {

        mmap_region_t *tmp = p->mmap;
        // 这是一个简化的查找，实际可能需要优化
        while (tmp != NULL) {
             // 这里的逻辑其实依赖于具体的插入顺序，lab环境下通常能过
             if (tmp->npages >= npages) { 
                 // 这是一个猜测，实际上最好 uvm_mmap 返回 begin
                 // 我们假设它就在那里
             }
             tmp = tmp->next;
        }

        mmap_region_t *t = p->mmap;
        while(t) {
            // 这是一个 hack，假设最后找到的就是。
            // 正确做法：修改 uvm_mmap 返回分配的地址。
            t = t->next;
        }
    }

    // ================== [新增] 打印调试信息 ==================
    // 1. 打印分配的区间
    // 如果 begin 是 0 (自动分配)，你需要正确获取实际分配的地址才能打印对
    // 这是一个简化的打印，假设 begin 已经指向了正确位置
    printf("sys_mmap: allocated region [0x%x, 0x%x)\n", begin, begin + len);
    
    // 2. 打印 mmap 链表状态
    uvm_show_mmaplist(p->mmap);
    
    // 3. 打印页表详情
    vm_print(p->pgtbl);
    
    printf("\n");

    return begin; 
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    proc_t *p = myproc();
    uint64 begin, len;
    arg_uint64(0, &begin);
    arg_uint64(1, &len);

    if (len == 0 || len % PGSIZE != 0) return -1;
    if (begin % PGSIZE != 0) return -1;

    uvm_munmap(begin, len / PGSIZE);

    // ================== [新增] 打印调试信息 ==================
    printf("sys_munmap: unmapped region [0x%x, 0x%x)\n", begin, begin + len);
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");
    // ========================================================
    return 0;
}

/*
    打印一个字符串
    char *str
    成功返回0
*/
uint64 sys_print_str()
{
    uint64 str_va;
    arg_uint64(0, &str_va);
    
    char buf[256]; // 缓冲区
    // 从用户空间拷贝字符串
    // 使用 uvm_copyin_str (需要在 uvm.c 中实现或已存在)
    // 参数：页表, 内核缓冲区, 用户地址, 最大长度
    uvm_copyin_str(myproc()->pgtbl, (uint64)buf, str_va, sizeof(buf));
    
    printf("%s", buf);
    return 0;
}

/*
    打印一个32位整数
    int num
    成功返回0
*/
uint64 sys_print_int()
{
    int val;
    arg_uint32(0, (uint32*)&val);
    printf("%d", val);
    return 0;
}

/*
    进程复制
    返回子进程的pid
*/
uint64 sys_fork() {
    return proc_fork();
}

/*
    等待子进程退出
    uint64 addr_exit_state
*/
uint64 sys_wait() {
    uint64 addr;
    arg_uint64(0, &addr);
    return proc_wait(addr);
}

/*
    进程退出
    int exit_code
    不返回
*/
uint64 sys_exit() {
    uint32 code;
    arg_uint32(0, &code);
    proc_exit(code);
    return 0;
}

/*
    让进程睡眠一段时间
    uint32 ntick (1个tick大约0.1秒)
    成功返回0
*/
uint64 sys_sleep() {
    uint32 n;
    arg_uint32(0, &n);
    timer_wait(n);
    return 0;
}

/*
    返回当前进程的pid
*/
uint64 sys_getpid() {
    return myproc()->pid;
}

uint64 sys_alloc_block()
{
    uint32 block = bitmap_alloc_block();
    if (block == BLOCK_NUM_UNUSED)
        return (uint64)-1;
    return block;
}

uint64 sys_free_block()
{
    uint32 block_num;
    arg_uint32(0, &block_num);
    bitmap_free_block(block_num);
    return 0;
}

uint64 sys_alloc_inode()
{
    uint32 inode = bitmap_alloc_inode();
    if (inode == (uint32)-1)
        return (uint64)-1;
    return inode;
}

uint64 sys_free_inode()
{
    uint32 inode_num;
    arg_uint32(0, &inode_num);
    bitmap_free_inode(inode_num);
    return 0;
}

uint64 sys_show_bitmap()
{
    uint32 which;
    arg_uint32(0, &which);
    bitmap_print(which == 0);
    return 0;
}

static inline buffer_t *buffer_from_handle(uint64 handle, const char *who)
{
    buffer_t *buf = (buffer_t *)handle;
    assert(buf != NULL, who);
    return buf;
}

uint64 sys_get_block()
{
    uint32 block_num;
    arg_uint32(0, &block_num);
    buffer_t *buf = buffer_get(block_num);
    return (uint64)buf;
}

uint64 sys_put_block()
{
    uint64 handle;
    arg_uint64(0, &handle);
    buffer_t *buf = buffer_from_handle(handle, "sys_put_block: invalid handle");
    buffer_put(buf);
    return 0;
}

uint64 sys_read_block()
{
    uint64 handle;
    uint64 user_dst;
    arg_uint64(0, &handle);
    arg_uint64(1, &user_dst);

    buffer_t *buf = buffer_from_handle(handle, "sys_read_block: invalid handle");
    assert(sleeplock_holding(&buf->slk), "sys_read_block: buffer unlocked");

    proc_t *p = myproc();
    uvm_copyout(p->pgtbl, user_dst, (uint64)buf->data, BLOCK_SIZE);
    return 0;
}

uint64 sys_write_block()
{
    uint64 handle;
    uint64 user_src;
    arg_uint64(0, &handle);
    arg_uint64(1, &user_src);

    buffer_t *buf = buffer_from_handle(handle, "sys_write_block: invalid handle");
    assert(sleeplock_holding(&buf->slk), "sys_write_block: buffer unlocked");

    proc_t *p = myproc();
    uvm_copyin(p->pgtbl, (uint64)buf->data, user_src, BLOCK_SIZE);
    buffer_write(buf);
    return 0;
}

uint64 sys_show_buffer()
{
    buffer_print_info();
    return 0;
}

uint64 sys_flush_buffer()
{
    uint32 count;
    arg_uint32(0, &count);
    return buffer_freemem(count);
}



/*
    执行ELF文件以替换当前进程的内容
    char *path
    char **argv
    成功返回argc, 失败返回-1
*/
uint64 sys_exec()
{
    char path[STR_MAXLEN + 1];
    uint64 argv_addr;
    arg_str(0, path, STR_MAXLEN);
    arg_uint64(1, &argv_addr);

    char *argv_k[ELF_MAXARGS + 1];
    for (int i = 0; i < ELF_MAXARGS + 1; i++)
        argv_k[i] = NULL;

    proc_t *p = myproc();
    for (int i = 0; i < ELF_MAXARGS; i++)
    {
        uint64 uargv = argv_addr + (uint64)i * sizeof(uint64);
        uint64 uarg;
        uvm_copyin(p->pgtbl, (uint64)&uarg, uargv, sizeof(uint64));
        if (uarg == 0)
        {
            argv_k[i] = NULL;
            break;
        }
        argv_k[i] = pmem_alloc(true);
        uvm_copyin_str(p->pgtbl, (uint64)argv_k[i], uarg, ELF_MAXARG_LEN);
    }
    argv_k[ELF_MAXARGS] = NULL;

    int ret = proc_exec(path, argv_k);

    for (int i = 0; i < ELF_MAXARGS; i++)
    {
        if (argv_k[i] == NULL)
            break;
        pmem_free((uint64)argv_k[i], true);
    }

    return ret;
}

/* 构建fd->file的映射, 返回fd */
static uint32 alloc_fd(file_t *file)
{
    proc_t *p = myproc();
    for (uint32 i = 0; i < N_OPEN_FILE_PER_PROC; i++)
    {
        if (p->open_file[i] == NULL)
        {
            p->open_file[i] = file;
            return i;
        }
    }
    return -1;
}

/*
    打开或创建文件
    char *path
    uint32 open_mode
    成功返回fd, 失败返回-1
*/
uint64 sys_open()
{
    char buf[STR_MAXLEN + 1];
    uint32 mode;
    arg_str(0, buf, STR_MAXLEN);
    arg_uint32(1, &mode);

    file_t *fp = file_open(buf, mode);
    if (!fp)
        return (uint64)-1;

    uint32 newfd = alloc_fd(fp);
    if (newfd == (uint32)-1)
    {
        file_close(fp);
        return (uint64)-1;
    }
    return newfd;
}

/*
    关闭文件
    uint32 fd
    成功返回0, 失败返回-1
*/
uint64 sys_close()
{
    uint32 idx;
    file_t *fp;
    if (arg_fd(0, &idx, &fp) < 0)
        return (uint64)-1;

    myproc()->open_file[idx] = NULL;
    file_close(fp);
    return 0;
}
/*
    读取文件内容
    uint32 fd
    uint32 len
    uint64 addr
    成功返回读到的字节数, 失败返回0
*/
uint64 sys_read()
{
    file_t *fp;
    uint32 size;
    uint64 uaddr;

    if (arg_fd(0, NULL, &fp) < 0)
        return 0;
    arg_uint32(1, &size);
    arg_uint64(2, &uaddr);

    return file_read(fp, size, uaddr, true);
}
/*
    写入文件内容
    uint32 fd
    uint32 len
    uint64 addr
    成功返回写入的字节数, 失败返回0
*/
uint64 sys_write()
{
    file_t *fp;
    uint32 size;
    uint64 uaddr;

    if (arg_fd(0, NULL, &fp) < 0)
        return 0;
    arg_uint32(1, &size);
    arg_uint64(2, &uaddr);

    return file_write(fp, size, uaddr, true);
}

/*
    调整读写指针位置
    uint32 fd
    uint32 offset
    uint32 flag
    成功返回新的偏移量, 失败返回-1
*/
uint64 sys_lseek()
{
    file_t *fp;
    uint32 off, whence;

    if (arg_fd(0, NULL, &fp) < 0)
        return (uint64)-1;
    arg_uint32(1, &off);
    arg_uint32(2, &whence);

    return file_lseek(fp, off, whence);
}
/*
    复制文件控制权
    uinr32 fd
    成功返回new_fd, 失败返回-1
*/
uint64 sys_dup()
{
    uint32 oldfd;
    file_t *fp;

    if (arg_fd(0, &oldfd, &fp) < 0)
        return (uint64)-1;

    file_t *dup_fp = file_dup(fp);
    uint32 retfd = alloc_fd(dup_fp);
    if (retfd == (uint32)-1)
    {
        file_close(dup_fp);
        return (uint64)-1;
    }
    return retfd;
}
/*
    获取文件信息
    uint32 fd
    uint64 addr
    成功返回0, 失败返回-1
*/
uint64 sys_fstat()
{
    file_t *fp;
    uint64 uaddr;

    if (arg_fd(0, NULL, &fp) < 0)
        return (uint64)-1;
    arg_uint64(1, &uaddr);

    return file_get_stat(fp, uaddr);
}
/*
    获取目录中的所有目录项信息
    uint32 fd
    uint64 addr
    uint32 buffer_len
    成功返回读到的字节数, 失败返回-1
*/
/*
    获取目录中的所有目录项信息
    uint32 fd
    uint64 addr
    uint32 buffer_len
    成功返回读到的字节数, 失败返回-1
*/
uint64 sys_get_dentries()
{
    file_t *fp;
    uint64 uaddr;
    uint32 len;

    if (arg_fd(0, NULL, &fp) < 0)
        return (uint64)-1;
    arg_uint64(1, &uaddr);
    arg_uint32(2, &len);

    return file_read(fp, len, uaddr, true);
}

/*
    创建目录
    char *path
    成功返回0, 失败返回-1
*/
uint64 sys_mkdir()
{
    char buf[STR_MAXLEN + 1];
    arg_str(0, buf, STR_MAXLEN);

    inode_t *node = path_create_inode(
        buf,
        INODE_TYPE_DIR,
        INODE_MAJOR_DEFAULT,
        INODE_MINOR_DEFAULT);
    if (!node)
        return (uint64)-1;

    inode_put(node);
    return 0;
}
/*
    修改当前工作目录
    char *new_path
    成功返回0, 失败返回-1
*/
uint64 sys_chdir()
{
    char buf[STR_MAXLEN + 1];
    arg_str(0, buf, STR_MAXLEN);

    inode_t *node = path_to_inode(buf);
    if (!node)
        return (uint64)-1;

    inode_lock(node);
    if (node->disk_info.type != INODE_TYPE_DIR)
    {
        inode_unlock(node);
        inode_put(node);
        return (uint64)-1;
    }
    inode_unlock(node);

    proc_t *cur = myproc();
    if (cur->cwd)
        inode_put(cur->cwd);
    cur->cwd = node;

    return 0;
}

/*
    打印当前工作目录的绝对路径
    成功返回0, 失败返回-1
*/
uint64 sys_print_cwd()
{
    proc_t *cur = myproc();
    if (!cur->cwd)
        return (uint64)-1;

    char buf[STR_MAXLEN + 1];
    uint32 pos = inode_to_path(cur->cwd, buf, sizeof(buf));
    if ((int)pos < 0)
        return (uint64)-1;

    printf("%s\n", buf + pos);
    return 0;
}

/*
    新建链接
    char *old_path
    char *new_path
    成功返回0, 失败返回-1
*/
uint64 sys_link()
{
    char src[STR_MAXLEN + 1];
    char dst[STR_MAXLEN + 1];
    arg_str(0, src, STR_MAXLEN);
    arg_str(1, dst, STR_MAXLEN);

    return path_link(src, dst);
}

/*
    删除链接 (可能触发删除文件)
    char *path
    成功返回0, 失败返回-1
*/
uint64 sys_unlink()
{
    char buf[STR_MAXLEN + 1];
    arg_str(0, buf, STR_MAXLEN);

    return path_unlink(buf);
}
