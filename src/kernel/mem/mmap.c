#include "mod.h"

// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
void mmap_init()
{
    spinlock_init(&list_lk, "mmap_node_list");
    list_head.next = 0;                    // 初始时链表为空
    mmap_region_node_t *tail = &list_head; // 尾指针，初始指向头节点

    for (int i = 0; i < N_MMAP; i++)
    {
        mmap_region_node_t *node = &node_list[i];
        node->next = 0; // 新节点总是链表的最后一个，所以 next 为 0

        // 将当前节点插入到当前链表的尾部
        tail->next = node;
        // 更新尾指针指向新的尾部节点
        tail = node;
    }

    printf("mmap_init: %d mmap_region_node_t initialized.\n", N_MMAP);
}

// 从仓库申请一个 mmap_region_t
// 若仓库空了则 panic
mmap_region_t *mmap_region_alloc()
{
    mmap_region_node_t *node;

    spinlock_acquire(&list_lk);

    // 检查链表是否为空
    node = list_head.next;
    if (node == 0)
    {
        // 仓库空了，释放锁并 panic
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: mmap node warehouse is empty!");
    }
    list_head.next = node->next;
    spinlock_release(&list_lk);

    memset(&(node->mmap), 0, sizeof(mmap_region_t));

    printf("mmap_region_alloc: Node %d allocated.\n", (int)(node - &node_list[0]));

    return &(node->mmap);
}

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t *mmap)
{
    //  通过 mmap_region_t 地址计算出 mmap_region_node_t 的地址
    // 这是一个常见的 C 语言技巧，用于在结构体中获取其父结构体的指针。
    // 由于 mmap 字段是 mmap_region_node_t 的第一个成员，
    // 所以 (char *)mmap 等价于 (char *)node。
    mmap_region_node_t *node = (mmap_region_node_t *)mmap;

    if (node < &node_list[0] || node >= &node_list[N_MMAP])
    {
        printf("mmap_region_free: WARNING! Freeing invalid mmap node address %x\n", node);
        return; // 或者 panic
    }

    spinlock_acquire(&list_lk);

    node->next = list_head.next;
    list_head.next = node;

    spinlock_release(&list_lk);

    printf("mmap_region_free: Node %d freed.\n", (int)(node - &node_list[0]));
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