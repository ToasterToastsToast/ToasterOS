#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{

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
