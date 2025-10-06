#include "mod.h"
// 内核空间和用户空间的可分配物理页分开描述
alloc_region_t kern_region, user_region; //按理说要static但是要测试case2

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
 
    char *kern_alloc_start = (char *)ALLOC_BEGIN;//ALLOCBEGIn是可分配空间，先给内核
    char *user_alloc_start = kern_alloc_start + KERN_PAGES * PGSIZE; // 内核先拿了KERN_PAGES这么多

    // 初始化内核区域
    kern_region.begin = (uint64)kern_alloc_start;
    kern_region.end = (uint64)user_alloc_start;
    spinlock_init(&kern_region.lk, "kern_lock");
    kern_region.allocable = KERN_PAGES;
    kern_region.list_head.next = NULL;

    // 链表 尾插
    page_node_t *kern_prev = &kern_region.list_head; //指针指尾巴
    for (char *p = kern_alloc_start; p < user_alloc_start; p += PGSIZE)
    {
        page_node_t *page = (page_node_t *)p;
        page->next = NULL; //创建新页
        kern_prev->next = page;
        kern_prev = page; //连起来，更新尾巴
    }

    // 初始化用户区域
    user_region.begin = (uint64)user_alloc_start;
    user_region.end = (uint64)ALLOC_END;
    spinlock_init(&user_region.lk, "user_lock");
    user_region.allocable = ((uint64)ALLOC_END - (uint64)user_alloc_start) / PGSIZE;
    user_region.list_head.next = NULL;

    // 链表
    page_node_t *user_prev = &user_region.list_head;
    for (char *p = user_alloc_start; p + PGSIZE <= (char *)ALLOC_END; p += PGSIZE)
    {
        page_node_t *page = (page_node_t *)p;
        page->next = NULL;
        user_prev->next = page;
        user_prev = page;
    }
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void *pmem_alloc(bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;//inkernel参数用来确定位置
    spinlock_acquire(&region->lk);

    
    if(region->list_head.next == NULL)//无处分配
        panic("no free page");

    page_node_t * page = region->list_head.next; //拿出头部来分配
    region->list_head.next = page->next; // 将链表头指向下一个节点
    memset(page, 0, PGSIZE);
    region->allocable -= 1;
    spinlock_release(&region->lk);
    
    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    spinlock_acquire(&region->lk);
    page_node_t *node = (page_node_t *)page;
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable += 1;
    spinlock_release(&region->lk);
}
