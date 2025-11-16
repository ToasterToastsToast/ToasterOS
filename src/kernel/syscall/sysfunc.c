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

// --- 任务1: sys_copyout (内核 -> 用户) ---
/*
    测试: 向用户空间传出一个int类型的数组 (1 2 3 4 5)
    参数: a0 = addr (用户数组起始地址)
    成功返回0
*/
uint64 sys_copyout()
{
    proc_t *p = myproc();
    // 参数 1: a0 寄存器 (用户目标地址)
    uint64 user_dst_addr = p->tf->a0;

    // 1. 【准备内核源数据】
    // 硬编码内核数组内容 (1, 2, 3, 4, 5)
    for (int i = 0; i < ARRAY_LEN_IN_INTS; i++)
    {
        kernel_int_array[i] = i + 1;
    }

    // 2. 【调用 uvm_copyout】
    // 内核源地址: kernel_int_array
    // 拷贝长度: ARRAY_SIZE_IN_BYTES (20 字节)
    uvm_copyout(p->pgtbl, user_dst_addr, (uint64)kernel_int_array, ARRAY_SIZE_IN_BYTES);

    printf("sys_copyout: Copied hardcoded array to user 0x%lx.\n", user_dst_addr);

    return 0; // 成功返回
}

// --- 任务2: sys_copyin (用户 -> 内核) ---
/*
    测试: 从用户空间传入一个int类型的数组
    参数: a0 = addr (用户数组起始地址), a1 = 5 (元素数量)
    成功返回0
*/
uint64 sys_copyin()
{
    proc_t *p = myproc();
    // 参数 1: a0 寄存器 (用户源地址)
    uint64 user_src_addr = p->tf->a0;
    // 参数 2: a1 寄存器 (元素数量，我们假定它总是 5)
    uint32 num_elements = (uint32)p->tf->a1;

    if (num_elements != ARRAY_LEN_IN_INTS)
    {
        printf("sys_copyin: Expected 5 elements, received %d. Aborting.\n", num_elements);
        return (uint64)-1;
    }

    // 1. 【调用 uvm_copyin】
    // 内核目标地址: kernel_int_array
    // 拷贝长度: ARRAY_SIZE_IN_BYTES (20 字节)
    uvm_copyin(p->pgtbl, (uint64)kernel_int_array, user_src_addr, ARRAY_SIZE_IN_BYTES);

    // 2. 【打印验证】
    printf("sys_copyin: Array received from user: [ ");
    for (int i = 0; i < ARRAY_LEN_IN_INTS; i++)
    {
        printf("%d ", kernel_int_array[i]);
    }
    printf("]\n");

    return 0; // 成功返回
}

// --- 任务3: sys_copyinstr (用户 -> 内核字符串) ---
/*
    测试: 从用户空间传入一个字符串
    参数: a0 = addr (用户字符串起始地址)
    成功返回0
*/
uint64 sys_copyinstr()
{
    proc_t *p = myproc();
    // 参数 1: a0 寄存器 (用户字符串地址)
    uint64 user_src_addr = p->tf->a0;

    // 1. 【调用 uvm_copyin_str】
    // 内核目标地址: kernel_str_buf
    // 拷贝的最大长度: MAX_STRING_LEN
    uvm_copyin_str(p->pgtbl, (uint64)kernel_str_buf, user_src_addr, MAX_STRING_LEN);

    // 2. 【打印验证】
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
    printf("*");
    proc_t *p = myproc();
    // 1. 从 a0 寄存器获取请求的新堆顶地址
    uint64 new_heap_top = p->tf->a0;
    uint64 old_heap_top = p->heap_top;
    uint64 result_top = 0;
    const char *event_type = "no_change";

    // 2. 查询当前堆顶
    if (new_heap_top == 0)
    {
        // 用户请求查询当前堆顶位置
        result_top = old_heap_top;
        event_type = "look";
    }

    else if (new_heap_top > old_heap_top)
    {
        // 空间增加: old_heap_top < new_heap_top
        uint32 len = new_heap_top - old_heap_top;
        result_top = uvm_heap_grow(p->pgtbl, old_heap_top, len);
        event_type = "grow"; // 标记事件类型
    }
    else if (new_heap_top < old_heap_top)
    {
        // 空间减少: old_heap_top > new_heap_top
        uint32 len = old_heap_top - new_heap_top;
        result_top = uvm_heap_ungrow(p->pgtbl, old_heap_top, len);
        event_type = "ungrow"; // 标记事件类型
    }
    else
    {
        // 空间不变: old_heap_top == new_heap_top
        result_top = old_heap_top;
        event_type = "no_change"; // 标记事件类型
    }

    // 3. 处理结果和调试输出
    if (result_top > 0)
    {
        // 成功，更新进程堆顶
        p->heap_top = result_top;
        // 打印成功事件：ret_heap_top 是 64 位地址，使用 %x
        printf("%s event: ret_heap_top = %x\n", event_type, result_top);
        return result_top;
    }
    else
    {
        // 失败 (result_top == 0 是失败的标志)
        // 打印失败事件：Requested 和 Old 都是 64 位地址，使用 %x
        printf("%s event: FAILED (Requested %x, Old %x)\n", event_type, new_heap_top, old_heap_top);

        // 失败返回 -1 (约定)
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
    return 0;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    return 0;
}