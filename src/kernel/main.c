#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
static volatile int started = 0;
volatile static int over_1 = 0, over_2 = 0;
static int* mem[1024];
extern alloc_region_t user_region;
extern alloc_region_t kern_region;
#define TEST_CNT 10

// Your test function, renamed to avoid conflict with the real main
void test_mapping_and_unmapping()
{
    // 1. 初始化测试页表
    pte_t *pte;
    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(true);
    memset(pgtbl, 0, PGSIZE);

    // 2. 准备测试条件
    uint64 va_1 = 0x100000;
    uint64 va_2 = 0x8000;
    uint64 pa_1 = (uint64)pmem_alloc(false);
    uint64 pa_2 = (uint64)pmem_alloc(false);

    // 3. 建立映射
    vm_mappages(pgtbl, va_1, pa_1, PGSIZE, PTE_R | PTE_W);
    vm_mappages(pgtbl, va_2, pa_2, PGSIZE, PTE_R);

    // 4. 验证映射结果
    pte = vm_getpte(pgtbl, va_1, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_1 not found");
    assert((*pte & PTE_V) != 0, "test_mapping_and_unmapping: pte_1 not valid");
    assert(PTE_TO_PA(*pte) == pa_1, "test_mapping_and_unmapping: pa_1 mismatch");
    assert((*pte & (PTE_R | PTE_W)) == (PTE_R | PTE_W), "test_mapping_and_unmapping: flag_1 mismatch");

    pte = vm_getpte(pgtbl, va_2, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_2 not found");
    assert((*pte & PTE_V) != 0, "test_mapping_and_unmapping: pte_2 not valid");
    assert(PTE_TO_PA(*pte) == pa_2, "test_mapping_and_unmapping: pa_2 mismatch");
    // POTENTIAL BUG: You mapped with PTE_R only, but check for PTE_R | PTE_W
    // This should be:
    assert((*pte & PTE_R) == PTE_R, "test_mapping_and_unmapping: flag_2 mismatch");


    // 5. 解除映射
    vm_unmappages(pgtbl, va_1, PGSIZE, true);
    vm_unmappages(pgtbl, va_2, PGSIZE, true);

    // 6. 验证解除映射结果
    pte = vm_getpte(pgtbl, va_1, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_1 not found after unmap");
    assert((*pte & PTE_V) == 0, "test_mapping_and_unmapping: pte_1 still valid");
    pte = vm_getpte(pgtbl, va_2, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_2 not found after unmap");
    assert((*pte & PTE_V) == 0, "test_mapping_and_unmapping: pte_2 still valid");
    
    printf("test_mapping_and_unmapping passed!\n");
}

// The actual entry point for the kernel
int main() {
    int cpuid = r_tp();
    if (cpuid == 0) {
        // --- ADD INITIALIZATION HERE ---
        print_init(); // For printf
        pmem_init();  // For pmem_alloc/free
        kvm_init();   // For kernel page table
        kvm_inithart(); // Enable virtual memory for this core
        printf("Initialization complete. Starting test.\n");
        
        // --- RUN YOUR TEST ---
        test_mapping_and_unmapping();

        printf("All tests passed!\n");
    } else {
        // Other cores should wait until MMU is enabled
        kvm_inithart();
    }

    while(1);
}