#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

/*
typedef struct alloc_region
{
    uint64 begin;          // 起始物理地址
    uint64 end;            // 终止物理地址
    spinlock_t lk;         // 自旋锁(保护下面两个变量)
    uint32 allocable;      // 可分配页面数
    page_node_t list_head; // 可分配链的链头节点
} alloc_region_t;

*/
// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    kern_region.begin = KERNEL_BASE;
    kern_region.end = (uint64)ALLOC_BEGIN;
    spinlock_init(&kern_region.lk, "kern_lock");
    kern_region.allocable = ((uint64)ALLOC_BEGIN - KERNEL_BASE) / PGSIZE;
    kern_region.list_head.next = NULL;

    user_region.begin = (uint64)ALLOC_BEGIN;
    user_region.end = (uint64)ALLOC_END;
    spinlock_init(&user_region.lk, "user_lock");
    user_region.allocable = ((uint64)ALLOC_END - (uint64)ALLOC_BEGIN) / PGSIZE;

    //TODO: 循环处理链表
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{
    page_node_t *page;

    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{

}
