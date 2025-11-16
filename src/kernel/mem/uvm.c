#include "mod.h"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
/*--------------------part-1: 关于内核空间<->用户空间的数据传递--------------------*/

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 失败（如地址无效）应返回 -1 或类似错误码，这里为简洁起见，使用 void 并在内部处理错误。
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint32 copied_len = 0;

    // 循环直到拷贝完所有数据
    while (copied_len < len)
    {
        // 1. 计算当前页的用户虚拟地址和页内偏移
        uint64 current_uva = src + copied_len;
        uint64 page_offset = current_uva % PGSIZE;

        // 2. 计算当前页最多还能拷贝多少字节
        // 限制：不能超过本页的边界，也不能超过总剩余长度
        uint32 remaining_len = len - copied_len;
        uint32 bytes_on_this_page = MIN(remaining_len, (uint32)(PGSIZE - page_offset));

        // 3. 查找用户 PTE (不允许分配新页表)
        pte_t *pte = vm_getpte(pgtbl, current_uva, false);

        // 4. 检查 PTE 是否有效且可读 (R)
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_R))
        {
            // 如果地址无效、未映射或不可读，则失败。
            // 实际系统中这里应该返回错误，或发送 SIGSEGV 信号。
            panic("uvm_copyin: Invalid user address or permission denied\n");
        }

        // 5. 转换地址：获取内核可访问的物理地址（PA）
        // 假设 PA 就是内核可以直接访问的地址 (Direct Mapping)
        uint64 pa = PTE_TO_PA(*pte);
        uint64 k_src_addr = pa + page_offset; // 内核源地址 = 物理页基址 + 页内偏移

        // 6. 数据迁移：从用户页 (k_src_addr) 拷贝到内核目标 (dst + copied_len)
        uint64 k_dst_addr = dst + copied_len;
        memcpy((void *)k_dst_addr, (void *)k_src_addr, bytes_on_this_page);

        // 7. 更新进度
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
        // 1. 计算当前页的用户虚拟地址和页内偏移
        uint64 current_uva = dst + copied_len;
        uint64 page_offset = current_uva % PGSIZE;

        // 2. 计算当前页最多还能拷贝多少字节
        uint32 remaining_len = len - copied_len;
        uint32 bytes_on_this_page = MIN(remaining_len, (uint32)(PGSIZE - page_offset));

        // 3. 查找用户 PTE (不允许分配新页表)
        pte_t *pte = vm_getpte(pgtbl, current_uva, false);

        // 4. 检查 PTE 是否有效且可写 (W)
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_W))
        {
            // 地址无效、未映射或不可写，则失败
            panic("uvm_copyout: Invalid user address or permission denied.\n");
            
        }

        // 5. 转换地址：获取内核可访问的物理地址（PA）
        uint64 pa = PTE_TO_PA(*pte);
        uint64 k_dst_addr = pa + page_offset; // 内核目标地址 = 物理页基址 + 页内偏移

        // 6. 数据迁移：从内核源 (src + copied_len) 拷贝到用户页 (k_dst_addr)
        uint64 k_src_addr = src + copied_len;
        memcpy((void *)k_dst_addr, (void *)k_src_addr, bytes_on_this_page);

        // 7. 更新进度
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
        // 1. 计算当前页的用户虚拟地址和页内偏移
        uint64 current_uva = src + copied_len;
        uint64 page_offset = current_uva % PGSIZE;

        // 2. 计算当前页最多还能拷贝多少字节
        // 限制：不能超过本页的边界，也不能超过总剩余长度
        uint32 remaining_len = maxlen - copied_len;
        uint32 bytes_to_check = MIN(remaining_len, (uint32)(PGSIZE - page_offset));

        // 3. 查找用户 PTE
        pte_t *pte = vm_getpte(pgtbl, current_uva, false);

        // 4. 检查 PTE 是否有效且可读
        if (pte == NULL || !(*pte & PTE_V) || !(*pte & PTE_R))
        {
            panic("uvm_copyin_str: Invalid user address or permission denied.\n");

        }

        // 5. 转换地址：获取内核可访问的物理地址（PA）
        uint64 pa = PTE_TO_PA(*pte);
        char *k_src_addr = (char *)(pa + page_offset); // 内核源地址
        char *k_dst_addr = (char *)(dst + copied_len); // 内核目标地址

        // 6. 逐字节检查和拷贝
        for (uint32 i = 0; i < bytes_to_check; i++)
        {
            char byte = k_src_addr[i];
            k_dst_addr[i] = byte;

            // 检查是否遇到终止符
            if (byte == '\0')
            {
                return; // 字符串拷贝完成
            }
            copied_len++;
        }
    }

    // 如果循环结束是因为达到了 maxlen，但最后一个拷贝的字节不是 '\0'，
    // 则说明字符串被截断了，但拷贝操作本身已完成。
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
}

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 失败则panic卡死
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{
}

/*------------------part-3: 用户空间heap和stack管理相关------------------*/

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
}

// 用户堆空间减少, 返回新的堆顶地址
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
}

// 处理函数栈增长导致的page fault事件
// 成功返回new_ustack_npage，失败返回-1
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr)
{
}

/*----------------------part-4: 用户页表管理相关----------------------*/

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);   // 可以释放，因为trapframe是每个进程独有的
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false); // 不能释放，因为所有进程共用区域
    destroy_pgtbl(pgtbl, 3);
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
}
