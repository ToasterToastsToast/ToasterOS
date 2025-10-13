#include "mod.h"

// 内核页表
static pgtbl_t kernel_pgtbl;
// 从链接脚本 kernel.ld 中导入的地址符号
// extern 告诉编译器这些变量在别处定义
// 声明这些符号的存在，它们的值由链接器在链接时从 kernel.ld 提供
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];
extern char ALLOC_END[];
#define PLIC_SIZE 0x400000 // QEMU virt machine standard PLIC size
#define CLINT_SIZE 0x10000

uint64 len_fit_pagesize(uint64 l) {
    uint64 mask = 0x00000fff;
    if (l & mask)
        return ((l >> 12) + 1) << 12;
    return l;
}

/*
// satp寄存器相关
#define SATP_SV39 (8L << 60)                                           // MODE =
SV39 #define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12)) //
设置MODE和PPN字段

// 获取虚拟地址中的虚拟页(VPN)信息 占9bit
#define VA_SHIFT(level) (12 + 9 * (level))
#define VA_TO_VPN(va, level) ((((uint64)(va)) >> VA_SHIFT(level)) & 0x1FF)

// PA和PTE之间的转换
#define PA_TO_PTE(pa)  ((((uint64)(pa)) >> 12) << 10)//10是符号位和RSW
#define PTE_TO_PA(pte) (((uint64)(pte) >> 10) << 12) // 每个物理页大小是
4KB，因此页起始地址一定是 4KB 对齐的；即低 12 位为 0

*/
// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN + PTE_TO_PA + PA_TO_PTE
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc) {
    for (int level = 2; level > 0; level--) {
        uint64 vpn = VA_TO_VPN(va, level);
        pte_t *pte = &pgtbl[vpn];
        if (*pte & PTE_V) {
            pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        } else {
            if (!alloc) {
                // 不允许分配，查找失败，返回NULL
                return NULL;
            }

            // 允许分配，则调用物理内存分配器申请一个新的、干净的4KB物理页
            pgtbl = (pgtbl_t)pmem_alloc(true); // 页表属于内核，所以用true
            if (pgtbl == NULL) {
                // 物理内存耗尽，分配失败
                return NULL;
            }

            // 将新分配的页清零，因为它将作为一张新的页表使用
            memset(pgtbl, 0, PGSIZE);

            *pte = PA_TO_PTE((uint64)pgtbl) | PTE_V;
        }
    }
    // 经过两次循环，现在的 `pgtbl` 已经是最低级（L0）页表的地址了。
    // 我们只需要用 VPN[0] 作为索引，就能找到最终的PTE。
    uint64 offset = VA_TO_VPN(va, 0);
    return &pgtbl[offset];
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm) {
    // 1. 防御性检查
    // 检查地址和长度是否页对齐。%是取模运算符。
    assert(va % PGSIZE == 0, "vm_mappages: va not page-aligned");
    assert(pa % PGSIZE == 0, "vm_mappages: pa not page-aligned");
    assert(len > 0, "vm_mappages: len must be greater than 0");
    // assert(len % PGSIZE == 0, "vm_mappages: len not page-aligned");
    len = len_fit_pagesize(len);

    uint64 current_va = va;
    uint64 current_pa = pa;
    uint64 va_end = va + len;

    // 2. 循环遍历所有需要映射的页
    for (; current_va < va_end; current_va += PGSIZE, current_pa += PGSIZE) {
        // 3. 为当前的虚拟地址获取其对应的最后一级PTE的地址,
        // 必须允许分配，因为中间页表可能不存在
        pte_t *pte = vm_getpte(pgtbl, current_va, true);

        // 3.1 检查 vm_getpte 是否成功
        if (pte == NULL) {
            panic("vm_mappages: out of memory (vm_getpte failed)");
        }

        // 3.2 检查这个虚拟地址是否已经被映射过了
        if (*pte & PTE_V) {
            // 如果已经被映射，这是一个内核设计错误，直接panic
            // panic("vm_mappages: remap");
        }

        // 4. 构建新的PTE并写入
        //    - PA_TO_PTE(current_pa): 将物理地址转换为PTE中的PPN字段
        //    - perm: 附加传入的权限位 (R, W, X, U)
        //    - PTE_V: 设置有效位，激活这个映射
        *pte = PA_TO_PTE(current_pa) | perm | PTE_V;
    }
}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit) {
    // 1. 防御性检查
    assert(va % PGSIZE == 0, "vm_unmappages: va not page-aligned");
    len = len_fit_pagesize(len);

    // assert(len % PGSIZE == 0, "vm_unmappages: len not page-aligned");

    uint64 current_va = va;
    uint64 va_end = va + len;

    // 2. 循环遍历所有需要解映射的页
    for (; current_va < va_end; current_va += PGSIZE) {

        // 3. 查找当前虚拟地址对应的PTE，不允许分配新页表
        pte_t *pte = vm_getpte(pgtbl, current_va, false);

        // 3.1 检查PTE是否存在且有效
        // 如果pte为NULL（中间页表不存在）或PTE本就无效，则无需操作，直接跳到下一个页
        if (pte == NULL || (*pte & PTE_V) == 0) {
            continue;
        }

        // 4. 如果指定了freeit，则释放该PTE指向的物理页
        if (freeit) {
            uint64 pa = PTE_TO_PA(*pte);
            pmem_free(pa, false); // 默认释放的是用户物理页
        }

        // 5. 将PTE清零，使其无效。这是“解映射”的核心操作。
        *pte = 0;
    }
}

// 完成UART、CLINT、PLIC、内核代码区、内核数据区、可分配区域的页表映射
// 相当于部分填充kernel_pgtbl
void kvm_init() {
    // 1. 为内核的顶级页表分配一个物理页
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true); // 页表属于内核资源
    if (kernel_pgtbl == NULL) {
        panic("kvm_init: out of memory for kernel page table");
    }
    memset(kernel_pgtbl, 0, PGSIZE);

    // 2. 映射硬件外设 (MMIO)
    // 权限: 读 + 写
    // UART 串口
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);
    // PLIC 平台级中断控制器
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, PLIC_SIZE, PTE_R | PTE_W);
    // CLINT 核心本地中断器
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE, CLINT_SIZE,
                PTE_R | PTE_W);

    // 3. 映射内核代码区 (.text)
    // 权限: 读 + 执行
    // 范围: 从 0x80000000 到 KERNEL_DATA 的地址
    vm_mappages(kernel_pgtbl, 0x80000000, 0x80000000,
                (uint64)&KERNEL_DATA - 0x80000000, PTE_R | PTE_X);

    // 4. 映射内核数据区 (.rodata, .data, .bss)
    // 权限: 读 + 写
    // 范围: 从 KERNEL_DATA 的地址到 ALLOC_BEGIN 的地址
    vm_mappages(kernel_pgtbl, (uint64)&KERNEL_DATA, (uint64)&KERNEL_DATA,
                (uint64)&ALLOC_BEGIN - (uint64)&KERNEL_DATA, PTE_R | PTE_W);

    // 5. 映射可分配的物理内存区域
    // 权限: 读 + 写
    // 范围: 从 ALLOC_BEGIN 的地址到 ALLOC_END 的地址
    vm_mappages(kernel_pgtbl, (uint64)&ALLOC_BEGIN, (uint64)&ALLOC_BEGIN,
                (uint64)&ALLOC_END - (uint64)&ALLOC_BEGIN, PTE_R | PTE_W);

    printf("kernel page table created successfully.\n");
}

// 每个CPU都需要调用, 从不使用页表切换到使用内核页表
// 切换后需要刷新TLB里面的缓存
void kvm_inithart() {
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 输出页表内容(for debug)
void vm_print(pgtbl_t pgtbl) {
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++) {
        pte = pgtbl_2[i];
        if (!((pte)&PTE_V))
            continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);

        for (int j = 0; j < PGSIZE / sizeof(pte_t); j++) {
            pte = pgtbl_1[j];
            if (!((pte)&PTE_V))
                continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_2);

            for (int k = 0; k < PGSIZE / sizeof(pte_t); k++) {
                pte = pgtbl_0[k];
                if (!((pte)&PTE_V))
                    continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k,
                       (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));
            }
        }
    }
}
