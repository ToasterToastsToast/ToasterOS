#include "mod.h"
#include "../proc/type.h"

// 内核页表
pgtbl_t kernel_pgtbl;

// 从链接脚本导入的地址符号
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];
extern char ALLOC_END[];
extern char trampoline[];

#define PLIC_SIZE 0x400000
#define CLINT_SIZE 0x10000

// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if (pgtbl == NULL)
        pgtbl = kernel_pgtbl;

    if (va >= VA_MAX)
        return NULL;

    // 三级页表查找
    for (int level = 2; level >= 0; level--)
    {
        uint64 vpn = VA_TO_VPN(va, level);
        pte_t *pte = &pgtbl[vpn];

        if (level == 0)
        {
            return pte;
        }

        if (!(*pte & PTE_V))
        {
            if (!alloc)
            {
                return NULL;
            }
            pgtbl_t new_pgtbl = (pgtbl_t)pmem_alloc(true);
            if (new_pgtbl == NULL)
            {
                return NULL;
            }
            *pte = PA_TO_PTE((uint64)new_pgtbl) | PTE_V;
        }

        if (!PTE_CHECK(*pte))
        {
            return NULL;
        }

        pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
    }

    return NULL;
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    assert((va % PGSIZE) == 0, "vm_mappages: va not aligned");
    assert((pa % PGSIZE) == 0, "vm_mappages: pa not aligned");
    assert(len > 0, "vm_mappages: len must be positive");
    assert(va + len <= VA_MAX, "vm_mappages: va + len exceeds VA_MAX");

    uint64 end_va = va + ((len + PGSIZE - 1) / PGSIZE) * PGSIZE;

    for (uint64 current_va = va, current_pa = pa; current_va < end_va; current_va += PGSIZE, current_pa += PGSIZE)
    {
        pte_t *pte = vm_getpte(pgtbl, current_va, true);
        if (pte == NULL)
        {
            panic("vm_mappages: vm_getpte failed");
        }

        if (*pte & PTE_V)
        {
            *pte = PA_TO_PTE(current_pa) | perm | PTE_V;
        }
        else
        {
            *pte = PA_TO_PTE(current_pa) | perm | PTE_V;
        }
    }
}

// 解除pgtbl中[va, va+len)区域的映射
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    assert((va % PGSIZE) == 0, "vm_unmappages: va not aligned");
    assert(len > 0, "vm_unmappages: len must be positive");
    assert(va + len <= VA_MAX, "vm_unmappages: va + len exceeds VA_MAX");

    uint64 end_va = va + ((len + PGSIZE - 1) / PGSIZE) * PGSIZE;

    for (uint64 current_va = va; current_va < end_va; current_va += PGSIZE)
    {
        pte_t *pte = vm_getpte(pgtbl, current_va, false);
        if (pte == NULL || !(*pte & PTE_V))
        {
            continue;
        }

        if (freeit)
        {
            uint64 pa = PTE_TO_PA(*pte);
            pmem_free(pa, false);
        }

        *pte = 0;
    }
}

// 完成UART、CLINT、PLIC、内核代码区、内核数据区、可分配区域的页表映射
void kvm_init()
{
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);
    if (kernel_pgtbl == NULL)
    {
        panic("kvm_init: out of memory for kernel page table");
    }

    // UART映射
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);

    // CLINT映射
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE, CLINT_SIZE, PTE_R | PTE_W);

    // PLIC映射
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, PLIC_SIZE, PTE_R | PTE_W);

    // virtio MMIO映射
    vm_mappages(kernel_pgtbl, VIRTIO_BASE, VIRTIO_BASE, PGSIZE, PTE_R | PTE_W);

    // 内核代码区域映射
    uint64 kernel_code_size = (uint64)KERNEL_DATA - 0x80000000;
    vm_mappages(kernel_pgtbl, 0x80000000, 0x80000000, kernel_code_size, PTE_R | PTE_X);

    // 内核数据区域映射
    uint64 kernel_data_size = (uint64)ALLOC_BEGIN - (uint64)KERNEL_DATA;
    vm_mappages(kernel_pgtbl, (uint64)KERNEL_DATA, (uint64)KERNEL_DATA, kernel_data_size, PTE_R | PTE_W);

    // 可分配区域映射
    uint64 alloc_size = (uint64)ALLOC_END - (uint64)ALLOC_BEGIN;
    vm_mappages(kernel_pgtbl, (uint64)ALLOC_BEGIN, (uint64)ALLOC_BEGIN, alloc_size, PTE_R | PTE_W);

    // trampoline映射
    vm_mappages(kernel_pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // 为所有进程分配和映射内核栈
    for (int i = 0; i < N_PROC; i++)
    {
        void *kstack_pa = pmem_alloc(true);
        if (kstack_pa == NULL)
        {
            panic("kvm_init: alloc kstack failed");
        }
        vm_mappages(kernel_pgtbl, KSTACK_VA(i), (uint64)kstack_pa, PGSIZE, PTE_R | PTE_W);
    }

    printf("kernel page table created successfully.\n");
}

// 每个CPU都需要调用, 从不使用页表切换到使用内核页表
void kvm_inithart()
{
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 输出页表内容(for debug)
void vm_print(pgtbl_t pgtbl)
{
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++)
    {
        pte = pgtbl_2[i];
        if (!((pte)&PTE_V))
            continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);

        for (int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if (!((pte)&PTE_V))
                continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_0);

            for (int k = 0; k < PGSIZE / sizeof(pte_t); k++)
            {
                pte = pgtbl_0[k];
                if (!((pte)&PTE_V))
                    continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));
            }
        }
    }
}