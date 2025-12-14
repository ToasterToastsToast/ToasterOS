#include "mod.h"

static buffer_node_t buf_cache[N_BUFFER];
static buffer_node_t buf_head_active, buf_head_inactive;
static spinlock_t lk_buf_cache;

/*
    将一个节点拿出来并插入
    1. 活跃链表的头部 buf_head_active->next
    2. 活跃链表的尾部 buf_head_active->prev
    3. 不活跃链表的头部 buf_head_inactive->next
    4. 不活跃链表的尾部 buf_head_inactive->prev
*/
static void insert_node(buffer_node_t *node, bool insert_active, bool insert_next)
{
    /* 如果有需要, 让node先离开当前位置 */
    if (node->next != NULL && node->prev != NULL)
    {
        node->next->prev = node->prev;
        node->prev->next = node->next;
    }

    /* 选择目标双向循环链表 */
    buffer_node_t *head = &buf_head_inactive;
    if (insert_active)
        head = &buf_head_active;

    /* 然后将node插入head->next or head->prev */
    if (insert_next)
    {
        node->next = head->next;
        node->next->prev = node;
        node->prev = head;
        head->next = node;
    }
    else
    {
        node->prev = head->prev;
        node->prev->next = node;
        node->next = head;
        head->prev = node;
    }
}

/*
    buffer系统初始化：
    1. 初始化全局的lk_buf_cache + buf_head_active + buf_head_inactive
    2. 初始化buf_cache中的所有node, 并将他们放在不活跃链表中
*/
void buffer_init()
{
    /* 初始化全局自旋锁 */
    spinlock_init(&lk_buf_cache, "buf_cache");

    /* 初始化活跃链表头节点 (双向循环链表) */
    buf_head_active.next = &buf_head_active;
    buf_head_active.prev = &buf_head_active;

    /* 初始化不活跃链表头节点 (双向循环链表) */
    buf_head_inactive.next = &buf_head_inactive;
    buf_head_inactive.prev = &buf_head_inactive;

    /* 初始化buf_cache中的所有buffer节点 */
    for (int i = 0; i < N_BUFFER; i++)
    {
        buf_cache[i].buf.block_num = BLOCK_NUM_UNUSED;
        buf_cache[i].buf.ref = 0;
        sleeplock_init(&buf_cache[i].buf.slk, "buffer");
        buf_cache[i].buf.data = NULL;
        buf_cache[i].buf.disk = false;
        buf_cache[i].next = NULL;
        buf_cache[i].prev = NULL;

        /* 将buffer插入不活跃链表 (希望第一个buffer最后位于head->next) */
        /* 所以从后往前插入到head->next位置 */
        insert_node(&buf_cache[i], false, true);
    }
}

/* 磁盘读取: block -> buf */
static void buffer_read(buffer_t *buf)
{
    /* 确保调用者持有睡眠锁 */
    assert(sleeplock_holding(&buf->slk), "buffer_read: not holding lock");

    /* 调用virtio层的读操作 */
    virtio_disk_rw(buf, false);
}

/* 磁盘写入: buf -> block */
void buffer_write(buffer_t *buf)
{
    /* 确保调用者持有睡眠锁 */
    assert(sleeplock_holding(&buf->slk), "buffer_write: not holding lock");

    /* 调用virtio层的写操作 */
    virtio_disk_rw(buf, true);
}

/* 从buf_cache中获取一个buf */
buffer_t *buffer_get(uint32 block_num)
{
    buffer_node_t *node;

    spinlock_acquire(&lk_buf_cache);

    /* 1. 首先在活跃链表中查找 */
    for (node = buf_head_active.next; node != &buf_head_active; node = node->next)
    {
        if (node->buf.block_num == block_num)
        {
            /* 找到了, ref+1, 移动到活跃链表头部 */
            node->buf.ref++;
            insert_node(node, true, true);
            spinlock_release(&lk_buf_cache);

            /* 获取睡眠锁 */
            sleeplock_acquire(&node->buf.slk);
            return &node->buf;
        }
    }

    /* 2. 在不活跃链表中查找 */
    for (node = buf_head_inactive.next; node != &buf_head_inactive; node = node->next)
    {
        if (node->buf.block_num == block_num)
        {
            /* 找到了, ref+1, 移动到活跃链表头部 */
            node->buf.ref++;
            insert_node(node, true, true);
            spinlock_release(&lk_buf_cache);

            /* 获取睡眠锁 */
            sleeplock_acquire(&node->buf.slk);
            return &node->buf;
        }
    }

    /* 3. 缓存未命中: 从不活跃链表尾部获取最不活跃的buffer */
    node = buf_head_inactive.prev;
    if (node == &buf_head_inactive)
    {
        /* 不活跃链表为空 */
        panic("buffer_get: no buffers available");
    }

    /* 如果data为NULL, 需要分配物理页 */
    if (node->buf.data == NULL)
    {
        node->buf.data = (uint8 *)pmem_alloc(true);
        if (node->buf.data == NULL)
        {
            panic("buffer_get: out of memory");
        }
    }

    /* 设置新的block_num, ref设为1, 移动到活跃链表尾部 */
    node->buf.block_num = block_num;
    node->buf.ref = 1;
    insert_node(node, true, false);

    spinlock_release(&lk_buf_cache);

    /* 获取睡眠锁 */
    sleeplock_acquire(&node->buf.slk);

    /* 从磁盘读取数据 */
    buffer_read(&node->buf);

    return &node->buf;
}

/* 向buf_cache归还一个buf */
void buffer_put(buffer_t *buf)
{
    /* 释放睡眠锁 */
    sleeplock_release(&buf->slk);

    spinlock_acquire(&lk_buf_cache);

    /* ref减1 */
    buf->ref--;

    /* 如果ref减到0, 移动到不活跃链表头部 */
    if (buf->ref == 0)
    {
        buffer_node_t *node = (buffer_node_t *)((char *)buf - ((char *)&((buffer_node_t *)0)->buf - (char *)0));
        insert_node(node, false, true);
    }

    spinlock_release(&lk_buf_cache);
}

/*
    从后向前遍历非活跃链表, 尝试释放buffer_count个buffer持有的物理内存(data)
    返回成功释放资源的buffer数量
*/
uint32 buffer_freemem(uint32 buffer_count)
{
    uint32 freed = 0;
    buffer_node_t *node;

    spinlock_acquire(&lk_buf_cache);

    /* 从不活跃链表尾部(最不活跃)开始遍历 */
    for (node = buf_head_inactive.prev;
         node != &buf_head_inactive && freed < buffer_count;
         node = node->prev)
    {
        /* 如果该buffer持有物理内存, 则释放 */
        if (node->buf.data != NULL)
        {
            pmem_free((void *)node->buf.data,true);
            node->buf.data = NULL;
            node->buf.block_num = BLOCK_NUM_UNUSED;
            freed++;
        }
    }

    spinlock_release(&lk_buf_cache);

    return freed;
}


/* 输出buffer_cache的信息 (for test) */
void buffer_print_info()
{
    buffer_node_t *node;

    assert(N_BUFFER == N_BUFFER_TEST, "buffer_print_info: invalid N_BUFFER");

    spinlock_acquire(&lk_buf_cache);

    printf("buffer_cache information:\n");

    printf("1.active list:\n");
    for (node = buf_head_active.next; node != &buf_head_active; node = node->next)
    {
        printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
               (int)(node - buf_cache), node->buf.ref, (uint64)node->buf.data, node->buf.block_num);
    }
    printf("over!\n");

    printf("2.inactive list:\n");
    for (node = buf_head_inactive.next; node != &buf_head_inactive; node = node->next)
    {
        printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
               (int)(node - buf_cache), node->buf.ref, (uint64)node->buf.data, node->buf.block_num);
    }
    printf("over!\n");

    spinlock_release(&lk_buf_cache);
}
