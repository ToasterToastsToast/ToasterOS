#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
static volatile int started = 0;


// The actual entry point for the kernel
int main() {
    int cpuid = r_tp();
    if (cpuid == 0) {
        // --- ADD INITIALIZATION HERE ---
        print_init(); // For printf
        pmem_init();  // For pmem_alloc/free
        kvm_init();   // For kernel page table
        kvm_inithart(); // Enable virtual memory for this core
        trap_kernel_init();
        trap_kernel_initart();
        printf("Initialization complete. Starting test.\n");

        // --- RUN YOUR TEST ---
        test_mapping_and_unmapping();

        printf("All tests passed!\n");
    } else {
        // Other cores should wait until MMU is enabled
        kvm_inithart();
        trapinithart();
    }

    while(1);
}