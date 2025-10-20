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



int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        // --- Lab-1 & 2 的初始化 ---
        print_init();
        pmem_init();
        kvm_init();

        // --- 【【Lab-3 新增】】 ---
        // 调用 trap 模块的共享初始化
        // 这会调用 timer_create()
        trap_kernel_init(); 
        
        // --- Lab-2 的初始化 ---
        kvm_inithart(); // 启用内核页表

        // --- 【【Lab-3 新增】】 ---
        // 调用 trap 模块的核心独有初始化
        // 这会设置 stvec 指向 kernel_vector 并打开中断
        trap_kernel_inithart(); 

        printf("cpu %d is booting!\n", cpuid);
        
        // 唤醒其他核心
        __sync_synchronize();
        started = 1;

    } else {
        // --- 其他核心 (APs) ---
        while(started == 0);
        __sync_synchronize();
        
        kvm_inithart(); // 启用内核页表
        
        // --- 【【Lab-3 新增】】 ---
        // 每个核心都需要初始化自己的中断
        trap_kernel_inithart(); 

        printf("cpu %d is booting!\n", cpuid);
    }
    
    // main 函数不再执行任何测试，
    // 只是无限循环。
    // 中断处理程序会在后台自动打印 "tick"。
    while (1);    
}