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

uint64 sys_copyout()
{
    proc_t *p = myproc();
    // 参数 1: a0 寄存器 (用户目标地址)
    uint64 user_dst_addr = p->tf->a0;

    for (int i = 0; i < ARRAY_LEN_IN_INTS; i++)
    {
        kernel_int_array[i] = i + 1;
    }

    uvm_copyout(p->pgtbl, user_dst_addr, (uint64)kernel_int_array, ARRAY_SIZE_IN_BYTES);

    printf("sys_copyout: Copied hardcoded array to user %x.\n", user_dst_addr);

    return 0; // 成功返回
}

uint64 sys_copyin()
{
    proc_t *p = myproc();
    // a0 寄存器 (用户源地址)
    uint64 user_src_addr = p->tf->a0;
    // a1 寄存器 (元素数量，我们假定它总是 5)
    uint32 num_elements = (uint32)p->tf->a1;

    if (num_elements != ARRAY_LEN_IN_INTS)
    {
        printf("sys_copyin: Expected 5 elements, received %d. Aborting.\n", num_elements);
        return (uint64)-1;
    }

    uvm_copyin(p->pgtbl, (uint64)kernel_int_array, user_src_addr, ARRAY_SIZE_IN_BYTES);

    printf("sys_copyin: Array received from user: [ ");
    for (int i = 0; i < ARRAY_LEN_IN_INTS; i++)
    {
        printf("%d ", kernel_int_array[i]);
    }
    printf("]\n");

    return 0; // 成功返回
}

uint64 sys_copyinstr()
{
    proc_t *p = myproc();
    // a0 寄存器 (用户字符串地址)
    uint64 user_src_addr = p->tf->a0;

    uvm_copyin_str(p->pgtbl, (uint64)kernel_str_buf, user_src_addr, MAX_STRING_LEN);

    printf("sys_copyinstr: String received from user: '%s'\n", kernel_str_buf);

    return 0; // 成功返回
}
/*
    用户堆空间伸缩
    uint64 new_heap_top (如果是0, 代表查询当前堆顶位置)
    成功返回new_heap_top, 失败返回-1
*/
uint64 sys_brk()
{
    proc_t *p = myproc();
    uint64 new_top;

    /* 读取参数：new_heap_top（0 表示查询） */
    arg_uint64(0, &new_top);

    uint64 old_top = p->heap_top;
    uint64 result_top = 0;
    const char *event_type = "unknown";

    /* 查询当前堆顶 */
    if (new_top == 0)
    {
        event_type = "look";
        result_top = old_top;
        printf("%s event: ret_heap_top = %p (query)\n", event_type, (void *)result_top);
        vm_print(p->pgtbl);
        return result_top;
    }

    /* 没变化 */
    if (new_top == old_top)
    {
        event_type = "equal";
        result_top = old_top;
        printf("%s event: requested = %p, old = %p, ret = %p (no-op)\n",
               event_type, (void *)new_top, (void *)old_top, (void *)result_top);
        vm_print(p->pgtbl);
        return result_top;
    }

    /* 增长 */
    if (new_top > old_top)
    {
        event_type = "grow";
        uint64 delta = new_top - old_top;

        /* 计算将要映射的页对齐范围，便于日志 */
        uint64 map_va_start = ALIGN_UP(old_top, PGSIZE);
        uint64 map_va_end = ALIGN_UP(new_top, PGSIZE);
        uint64 map_len = (map_va_end > map_va_start) ? (map_va_end - map_va_start) : 0;
        uint32 map_pages = (uint32)(map_len / PGSIZE);

        printf("%s event: requested = %p, old = %p, delta = %lu bytes\n",
               event_type, (void *)new_top, (void *)old_top, (unsigned long)delta);
        if (map_len == 0)
        {
            printf("  -> no new page mappings required (growth within existing page)\n");
        }
        else
        {
            printf("  -> mapping VA range [%p, %p) => %lu bytes, %u pages\n",
                   (void *)map_va_start, (void *)map_va_end, (unsigned long)map_len, map_pages);
        }

        result_top = uvm_heap_grow(p->pgtbl, old_top, (uint32)delta);

        /* uvm_heap_grow 在你的实现中失败可能返回 0；其他实现可能返回 (uint64)-1，兼容两种 */
        if (result_top == 0 || result_top == (uint64)-1)
        {
            printf("%s event: FAILED (requested=%p, old=%p) -> uvm returned %p\n",
                   event_type, (void *)new_top, (void *)old_top, (void *)result_top);
            vm_print(p->pgtbl);
            return (uint64)-1;
        }

        /* 成功 */
        p->heap_top = result_top;
        printf("%s event: success -> new heap_top = %p\n", event_type, (void *)result_top);
        vm_print(p->pgtbl);
        return result_top;
    }

    /* 收缩 */
    /* new_top < old_top */
    event_type = "ungrow";
    {
        uint64 delta = old_top - new_top;
        uint64 unmap_va_start = ALIGN_UP(new_top, PGSIZE);
        uint64 unmap_va_end = ALIGN_UP(old_top, PGSIZE);
        uint64 unmap_len = (unmap_va_end > unmap_va_start) ? (unmap_va_end - unmap_va_start) : 0;
        uint32 unmap_pages = (uint32)(unmap_len / PGSIZE);

        printf("%s event: requested = %p, old = %p, shrink = %lu bytes\n",
               event_type, (void *)new_top, (void *)old_top, (unsigned long)delta);
        if (unmap_len == 0)
        {
            printf("  -> no page unmap required (shrink within same page)\n");
        }
        else
        {
            printf("  -> unmapping VA range [%p, %p) => %lu bytes, %u pages\n",
                   (void *)unmap_va_start, (void *)unmap_va_end, (unsigned long)unmap_len, unmap_pages);
        }

        result_top = uvm_heap_ungrow(p->pgtbl, old_top, (uint32)delta);

        if (result_top == 0 || result_top == (uint64)-1)
        {
            printf("%s event: FAILED (requested=%p, old=%p) -> uvm returned %p\n",
                   event_type, (void *)new_top, (void *)old_top, (void *)result_top);
            vm_print(p->pgtbl);
            return (uint64)-1;
        }

        p->heap_top = result_top;
        printf("%s event: success -> new heap_top = %p\n", event_type, (void *)result_top);
        vm_print(p->pgtbl);
        return result_top;
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

    if (begin == 0) {

        mmap_region_t *tmp = p->mmap;

        while (tmp != NULL) {

             tmp = tmp->next;
        }

        mmap_region_t *t = p->mmap;
        while(t) {

            t = t->next;
        }
    }

    printf("sys_mmap: allocated region [0x%x, 0x%x)\n", begin, begin + len);
    
    
    uvm_show_mmaplist(p->mmap);
    

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

    printf("sys_munmap: unmapped region [0x%x, 0x%x)\n", begin, begin + len);
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

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