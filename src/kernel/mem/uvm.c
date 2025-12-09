#include "mod.h"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TRUNC_PAGE_DOWN(a) ((a) & ~(PGSIZE - 1))

/*--------------------part-1: 关于内核空间<->用户空间的数据传递--------------------*/

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 失败（如地址无效）应返回 -1 或类似错误码，这里为简洁起见，使用 void 并在内部处理错误。
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint32 copied_len = 0;

    while (copied_len < len)
    {
        // 计算当前页的用户虚拟地址和页内偏移
        uint64 current_uva = src + copied_len;
        uint64 page_offset = current_uva % PGSIZE;

        // 计算当前页最多还能拷贝多少字节
        uint32 remaining_len = len - copied_len;
        uint32 bytes_on_this_page = MIN(remaining_len, (uint32)(PGSIZE - page_offset));

        // 查找用户 PTE
        pte_t *pte = vm_getpte(pgtbl, current_uva, false);

        // 检查 PTE 是否有效且可读 (R)
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_R))
        {
            // 如果地址无效、未映射或不可读，则失败。
            // 实际系统中这里应该返回错误，或发送 SIGSEGV 信号。
            panic("uvm_copyin: Invalid user address or permission denied\n");
        }

        uint64 pa = PTE_TO_PA(*pte);
        uint64 k_src_addr = pa + page_offset; // 内核源地址 = 物理页基址 + 页内偏移

        // 从用户页 (k_src_addr) 拷贝到内核目标 (dst + copied_len)
        uint64 k_dst_addr = dst + copied_len;
        memmove((void *)k_dst_addr, (void *)k_src_addr, bytes_on_this_page);

        copied_len += bytes_on_this_page;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint32 copied_len = 0;

    // 循环直到拷贝完所有数据
    while (copied_len < len)
    {
        // 计算当前页的用户虚拟地址和页内偏移
        uint64 current_uva = dst + copied_len;
        uint64 page_offset = current_uva % PGSIZE;

        // 计算当前页最多还能拷贝多少字节
        uint32 remaining_len = len - copied_len;
        uint32 bytes_on_this_page = MIN(remaining_len, (uint32)(PGSIZE - page_offset));

        // 查找用户 PTE
        pte_t *pte = vm_getpte(pgtbl, current_uva, false);

        // 检查 PTE 是否有效且可写
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_W))
        {
            // 地址无效未映射或不可写则失败
            panic("uvm_copyout: Invalid user address or permission denied.\n");
        }

        // 转换地址：获取内核可访问的物理地址
        uint64 pa = PTE_TO_PA(*pte);
        uint64 k_dst_addr = pa + page_offset; // 内核目标地址 = 物理页基址 + 页内偏移

        // 数据迁移：从内核源 (src + copied_len) 拷贝到用户页 (k_dst_addr)
        uint64 k_src_addr = src + copied_len;
        memmove((void *)k_dst_addr, (void *)k_src_addr, bytes_on_this_page);

        // 更新进度
        copied_len += bytes_on_this_page;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint32 copied_len = 0;

    // 循环直到达到最大长度
    while (copied_len < maxlen)
    {

        uint64 current_uva = src + copied_len;
        uint64 page_offset = current_uva % PGSIZE;

        uint32 remaining_len = maxlen - copied_len;
        uint32 bytes_to_check = MIN(remaining_len, (uint32)(PGSIZE - page_offset));

        pte_t *pte = vm_getpte(pgtbl, current_uva, false);

        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_R))
        {
            panic("uvm_copyin_str: Invalid user address or permission denied.\n");
        }

        uint64 pa = PTE_TO_PA(*pte);
        char *k_src_addr = (char *)(pa + page_offset); // 内核源地址
        char *k_dst_addr = (char *)(dst + copied_len); // 内核目标地址

        for (uint32 i = 0; i < bytes_to_check; i++)
        {
            char byte = k_src_addr[i];
            k_dst_addr[i] = byte;
            if (byte == '\0')
            {
                return;
            }
            copied_len++;
        }
    }
}

/*--------------------part-2: mmap_region相关--------------------*/

// 打印以mmap为首的mmap链
// for debug
void uvm_show_mmaplist(mmap_region_t *mmap)
{
    mmap_region_t *tmp = mmap;
    printf("\nalloced mmap_space:\n");
    if (tmp == NULL)
        printf("empty\n");
    while (tmp != NULL)
    {
        printf("alloced mmap_region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 两个 mmap_region 区域合并
// 注意: 保留一个 释放一个 不操作 next 指针
// 由uvm_mmap调用
static void mmap_merge(mmap_region_t *mmap_1, mmap_region_t *mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");

    // merge
    if (keep_mmap_1)
    {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    }
    else
    {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 寻找一块足够大的区域(len), 作为 mmap_region
// 由uvm_mmap调用(处理begin==0的情况)
// 成功返回begin, 失败返回0
static uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{
    mmap_region_t *tmp = head_mmap;
    uint64 search_begin = MMAP_BEGIN;

    // 从 MMAP_BEGIN 开始扫描，寻找第一个足够大的空隙
    while (tmp != NULL)
    {
        // 检查 [search_begin, tmp->begin) 是否足够大
        if (tmp->begin >= search_begin + len)
        {
            *p_tmp_mmap = tmp;
            if (tmp == head_mmap)
            {
                *p_last_mmap = NULL;
            }
            else
            {
                // 找到 tmp 的前驱节点
                mmap_region_t *prev = head_mmap;
                while (prev->next != tmp)
                {
                    prev = prev->next;
                }
                *p_last_mmap = prev;
            }
            return search_begin;
        }
        // 更新搜索起始位置
        search_begin = tmp->begin + tmp->npages * PGSIZE;
        tmp = tmp->next;
    }

    // 检查最后一个区域之后是否有空间
    if (search_begin + len <= MMAP_END)
    {
        *p_last_mmap = NULL;
        if (head_mmap != NULL)
        {
            tmp = head_mmap;
            while (tmp->next != NULL)
                tmp = tmp->next;
            *p_last_mmap = tmp;
        }
        *p_tmp_mmap = NULL;
        return search_begin;
    }
    return 0; // 没找到
}

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 失败则panic卡死
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    proc_t *p = myproc();
    uint64 len = npages * PGSIZE;

    mmap_region_t *last_mmap = NULL;
    mmap_region_t *tmp_mmap = NULL;

    // 1. 确定地址
    if (begin == 0)
    {
        begin = uvm_mmap_find(p->mmap, len, &last_mmap, &tmp_mmap);
        if (begin == 0)
            panic("uvm_mmap: no space found");
    }
    else
    {
        // 检查边界
        if (begin < MMAP_BEGIN || begin + len > MMAP_END)
            panic("uvm_mmap: address out of range");

        // 寻找插入位置
        if (p->mmap == NULL || begin < p->mmap->begin)
        {
            last_mmap = NULL;
            tmp_mmap = p->mmap;
        }
        else
        {
            last_mmap = p->mmap;
            while (last_mmap->next != NULL && last_mmap->next->begin < begin)
            {
                last_mmap = last_mmap->next;
            }
            tmp_mmap = last_mmap->next;
        }
    }

    // 2. 申请节点并插入链表
    mmap_region_t *new_mmap = mmap_region_alloc();
    new_mmap->begin = begin;
    new_mmap->npages = npages;
    new_mmap->next = tmp_mmap;

    if (last_mmap == NULL)
    {
        p->mmap = new_mmap;
    }
    else
    {
        last_mmap->next = new_mmap;
    }

    // 3. 尝试合并 (前向合并)
    if (last_mmap != NULL && last_mmap->begin + last_mmap->npages * PGSIZE == new_mmap->begin)
    {
        last_mmap->npages += new_mmap->npages;
        last_mmap->next = new_mmap->next;
        mmap_region_free(new_mmap);
        new_mmap = last_mmap;
    }

    // 4. 尝试合并 (后向合并)
    if (new_mmap->next != NULL && new_mmap->begin + new_mmap->npages * PGSIZE == new_mmap->next->begin)
    {
        mmap_region_t *next_mmap = new_mmap->next;
        new_mmap->npages += next_mmap->npages;
        new_mmap->next = next_mmap->next;
        mmap_region_free(next_mmap);
    }

    // 5. 实际映射物理内存
    for (uint64 va = begin; va < begin + len; va += PGSIZE)
    {
        void *pa = pmem_alloc(false);
        if (pa == NULL)
            panic("uvm_mmap: pmem_alloc failed");
        vm_mappages(p->pgtbl, va, (uint64)pa, PGSIZE, perm);
    }
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{
    proc_t *p = myproc();
    uint64 end = begin + npages * PGSIZE;

    if (begin < MMAP_BEGIN || end > MMAP_END)
        panic("uvm_munmap: address out of range");

    mmap_region_t *prev = NULL;
    mmap_region_t *curr = p->mmap;

    while (curr != NULL && curr->begin < end)
    {
        uint64 curr_end = curr->begin + curr->npages * PGSIZE;

        if (curr_end > begin)
        { // 有交集
            uint64 unmap_begin = (curr->begin > begin) ? curr->begin : begin;
            uint64 unmap_end = (curr_end < end) ? curr_end : end;

            // 分情况讨论：完全包含、前半截、后半截、中间打洞
            if (begin <= curr->begin && end >= curr_end)
            { // 1. 完全包含 -> 删除节点
                vm_unmappages(p->pgtbl, curr->begin, curr->npages * PGSIZE, true);
                mmap_region_t *to_free = curr;
                if (prev == NULL)
                    p->mmap = curr->next;
                else
                    prev->next = curr->next;
                curr = curr->next; // 这里的curr已经是下一个了，prev不变
                mmap_region_free(to_free);
                continue;
            }
            else if (begin <= curr->begin && end < curr_end)
            { // 2. 覆盖前半 -> 修改begin
                uint32 unmap_npages = (unmap_end - unmap_begin) / PGSIZE;
                vm_unmappages(p->pgtbl, unmap_begin, unmap_npages * PGSIZE, true);
                curr->npages -= unmap_npages;
                curr->begin = unmap_end;
            }
            else if (begin > curr->begin && end >= curr_end)
            { // 3. 覆盖后半 -> 修改npages
                uint32 unmap_npages = (unmap_end - unmap_begin) / PGSIZE;
                vm_unmappages(p->pgtbl, unmap_begin, unmap_npages * PGSIZE, true);
                curr->npages -= unmap_npages;
            }
            else if (begin > curr->begin && end < curr_end)
            { // 4. 中间打洞 -> 分裂
                uint32 unmap_npages = (unmap_end - unmap_begin) / PGSIZE;
                vm_unmappages(p->pgtbl, unmap_begin, unmap_npages * PGSIZE, true);

                mmap_region_t *new_node = mmap_region_alloc();
                new_node->begin = end;
                new_node->npages = (curr_end - end) / PGSIZE;
                new_node->next = curr->next;

                curr->npages = (begin - curr->begin) / PGSIZE;
                curr->next = new_node;

                prev = new_node;
                curr = new_node->next;
                continue;
            }
        }
        prev = curr;
        curr = curr->next;
    }
}

/*------------------part-3: 用户空间heap和stack管理相关------------------*/

// 用户堆空间增加, 返回新的堆顶地址
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
    // 计算新的堆顶地址 (虚拟地址)
    uint64 new_heap_top = cur_heap_top + len;

    if (new_heap_top > MMAP_BEGIN)
    {
        panic("uvm_heap_grow: new heap top exceeds MMAP_BEGIN\n");
        return 0; // 失败返回 0
    }

    // 计算需要映射的虚拟地址范围
    // 堆总是从 cur_heap_top 开始向上增长。
    // 但是页映射总是从页的起始地址开始。
    // 起始虚拟地址：当前堆顶的页对齐地址
    uint64 map_va_start = ALIGN_UP(cur_heap_top, PGSIZE);
    // 终止虚拟地址：新堆顶的页对齐地址 (向上取整)
    uint64 map_va_end = ALIGN_UP(new_heap_top, PGSIZE);

    if (map_va_end <= map_va_start)
    {
        return new_heap_top; // 堆顶增长但没有跨越页边界，直接返回新的堆顶
    }

    uint64 map_len = map_va_end - map_va_start;
    uint32 num_pages = (uint32)(map_len / PGSIZE);

    // 循环分配物理页面并映射
    for (uint32 i = 0; i < num_pages; i++)
    {
        // 申请一页用户页
        uint64 pa = (uint64)pmem_alloc(false);

        if (pa == 0)
        {
            printf("uvm_heap_grow: pmem_alloc failed for heap\n");
            return 0;
        }
        uint64 current_va = map_va_start + i * PGSIZE;

        // 建立映射：用户可读写 (PTE_U | PTE_R | PTE_W)
        vm_mappages(pgtbl, current_va, pa, PGSIZE, PTE_U | PTE_R | PTE_W);
    }

    return new_heap_top;
}
// 用户堆空间减少, 返回新的堆顶地址
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{

    uint64 new_heap_top = cur_heap_top - len;

    // 边界检查
    if (new_heap_top == 0)
    {
        return 0; // 堆顶不能收缩到 0
    }

    uint64 unmap_va_end = ALIGN_UP(cur_heap_top, PGSIZE);

    uint64 unmap_va_start = ALIGN_UP(new_heap_top, PGSIZE);

    // 如果 unmap_va_end <= unmap_va_start，说明没有跨越页边界，无需操作
    if (unmap_va_end <= unmap_va_start)
    {
        return new_heap_top; // 堆顶收缩但没有跨越页边界，直接返回新的堆顶
    }

    // 解除映射
    uint64 unmap_len = unmap_va_end - unmap_va_start;

    vm_unmappages(pgtbl, unmap_va_start, unmap_len, true);

    return new_heap_top;
}
// 处理函数栈增长导致的page fault事件
// 成功返回new_ustack_npage，失败返回-1
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr)
{
    proc_t *p = myproc();

    // 1. 计算当前已分配栈区域的最低地址（栈顶）
    // 栈是从 USTACK_START (高地址) 向下生长的
    uint64 current_ustack_top_va = TRAPFRAME - old_ustack_npage * PGSIZE;

    // 2. 合法性检查 1：Page Fault 是否发生在栈边界之下？
    // 如果 fault_addr >= current_ustack_top_va，说明访问了栈内或栈上方的非法区域
    if (fault_addr >= current_ustack_top_va)
    {
        printf("uvm_ustack_grow: ERROR: fault_addr %x is above current stack top %x\n",
               fault_addr, current_ustack_top_va);
        return (uint64)-1;
    }

    // 3. 计算需要映射的新页的 VA 范围
    // 新的栈顶是 fault_addr 所在的页的起始地址
    uint64 new_ustack_top_va = TRUNC_PAGE_DOWN(fault_addr);

    // 4. 合法性检查 2：是否越过 MMAP_END (栈的下限)
    if (new_ustack_top_va < MMAP_END)
    {
        printf("uvm_ustack_grow: ERROR: New stack top %x exceeds MMAP_END %x\n",
               new_ustack_top_va, MMAP_END);
        return (uint64)-1;
    }

    // 5. 计算需要扩展的页面数量和映射范围
    // 映射范围是 [new_ustack_top_va, current_ustack_top_va)
    uint64 grow_len = current_ustack_top_va - new_ustack_top_va;
    uint32 num_pages_to_grow = (uint32)(grow_len / PGSIZE);

    printf("uvm_ustack_grow: Growing stack by %d pages (VA range [%x, %x))\n",
           num_pages_to_grow, new_ustack_top_va, current_ustack_top_va);

    // 6. 循环分配和映射
    // 从最低地址开始映射，以 new_ustack_top_va 为起点
    uint64 map_va = new_ustack_top_va;
    for (uint32 i = 0; i < num_pages_to_grow; i++)
    {
        // 6.1 申请一页物理内存 (用户页)
        uint64 pa = (uint64)pmem_alloc(false);

        if (pa == 0)
        {
            printf("uvm_ustack_grow: ERROR: pmem_alloc failed for stack, cannot grow!\n");
            // WARNING: 真实系统中，分配失败需要回滚之前已分配的页面！
            return (uint64)-1;
        }

        // 6.2 建立映射：用户可读写 (PTE_U | PTE_R | PTE_W)
        vm_mappages(pgtbl, map_va, pa, PGSIZE, PTE_U | PTE_R | PTE_W);

        printf("uvm_ustack_grow: Mapping VA %x to PA %x\n", map_va, pa);

        map_va += PGSIZE; // 映射下一页
    }

    // 7. 更新并返回新的栈页数
    uint64 new_ustack_npage = old_ustack_npage + num_pages_to_grow;
    p->ustack_npage = new_ustack_npage;

    printf("uvm_ustack_grow: Stack successfully expanded to %x pages.\n", new_ustack_npage);
    return new_ustack_npage;
}

/*----------------------part-4: 用户页表管理相关----------------------*/

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pgtbl[i];
        if (pte & PTE_V)
        {
            uint64 child_pa = PTE_TO_PA(pte);
            if (level > 0)
            {
                destroy_pgtbl((pgtbl_t)child_pa, level - 1);
            }
            else
            {
                // 叶子节点：如果是用户页(PTE_U)，则释放物理内存
                if (pte & PTE_U)
                {
                    pmem_free(child_pa, false);
                }
            }
        }
    }
    pmem_free((uint64)pgtbl, true); // 释放页表页本身
}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);   // 可以释放，因为trapframe是每个进程独有的
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false); // 不能释放，因为所有进程共用区域
    // destroy_pgtbl(pgtbl, 3);
    destroy_pgtbl(pgtbl, 2);
}

// 连续虚拟空间的复制
// 在uvm_copy_pgtbl中使用
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t *pte;

    for (va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");

        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char *)page, (const char *)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 拷贝页表 (拷贝并不包括 trapframe 和 trampoline)
// 拷贝的页表管理的物理页是原来页表的复制品
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap)
{
    // 1. 复制代码段
    copy_range(old, new, USER_BASE, USER_BASE + PGSIZE);

    // 2. 复制堆
    uint64 heap_end = (heap_top + PGSIZE - 1) & ~(PGSIZE - 1);
    if (heap_end > USER_BASE + PGSIZE)
        copy_range(old, new, USER_BASE + PGSIZE, heap_end);

    // 3. 复制栈
    uint64 stack_begin = TRAPFRAME - ustack_npage * PGSIZE;
    copy_range(old, new, stack_begin, TRAPFRAME);

    // 4. 复制 mmap 区域
    for (mmap_region_t *t = mmap; t != NULL; t = t->next)
    {
        copy_range(old, new, t->begin, t->begin + t->npages * PGSIZE);
    }
}
