#include "mod.h"

// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
void mmap_init()
{
    // 1. 初始化自旋锁
    spinlock_init(&list_lk, "mmap_node_list");

    // 2. 初始化链表头节点
    list_head.next = 0; // 初始时链表为空

    // 3. 将所有 node_list 节点串联到 list_head 之后
    // 整个操作在初始化阶段完成，无需加锁，但习惯性地先初始化锁。
    for (int i = 0; i < N_MMAP; i++)
    {
        mmap_region_node_t *node = &node_list[i];

        // 将当前节点插入到 list_head 的头部
        node->next = list_head.next;
        list_head.next = node;
    }

    printf("mmap_init: %d mmap_region_node_t initialized.\n", N_MMAP);
}

// 从仓库申请一个 mmap_region_t
// 若仓库空了则 panic
mmap_region_t *mmap_region_alloc()
{
    mmap_region_node_t *node;

    spinlock_acquire(&list_lk);

    // 1. 检查链表是否为空
    node = list_head.next;
    if (node == 0)
    {
        // 仓库空了，释放锁并 panic
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: mmap node warehouse is empty!");
    }

    // 2. 将节点从链表头部移除
    list_head.next = node->next;

    // 3. 释放锁
    spinlock_release(&list_lk);

    // 4. 清零并返回 mmap_region_t 结构体
    // 清零是一个好习惯，防止残留数据干扰。
    memset(&(node->mmap), 0, sizeof(mmap_region_t));

    printf("mmap_region_alloc: Node %d allocated.\n", (int)(node - &node_list[0]));

    return &(node->mmap);
}

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t *mmap)
{
    // 1. 通过 mmap_region_t 地址计算出 mmap_region_node_t 的地址
    // 这是一个常见的 C 语言技巧，用于在结构体中获取其父结构体的指针。
    // 由于 mmap 字段是 mmap_region_node_t 的第一个成员，
    // 所以 (char *)mmap 等价于 (char *)node。
    mmap_region_node_t *node = (mmap_region_node_t *)mmap;

    // 2. 检查地址是否在合法范围内 (可选，但推荐)
    if (node < &node_list[0] || node >= &node_list[N_MMAP])
    {
        //printf("mmap_region_free: WARNING! Freeing invalid mmap node address %x\n", node);
        return; // 或者 panic
    }

    spinlock_acquire(&list_lk);

    // 3. 将节点插入到空闲链表头部
    node->next = list_head.next;
    list_head.next = node;

    spinlock_release(&list_lk);

    //printf("mmap_region_free: Node %d freed.\n", (int)(node - &node_list[0]));
}

// 输出可用的 mmap_region_node_t 链
// for debug
void mmap_show_nodelist()
{
    spinlock_acquire(&list_lk);

    mmap_region_node_t *tmp = list_head.next;
    int node = 0, index = 0;
    while (tmp)
    {
        index = tmp - &(node_list[0]);
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}