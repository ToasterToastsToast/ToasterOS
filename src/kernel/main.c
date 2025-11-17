#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
#include "proc/mod.h"

// volatile static int started = 0;

// int main()
// {
//     int cpuid = r_tp();

//     if (cpuid == 0)
//     {

//         print_init();
//         printf("cpu %d is booting!\n", cpuid);

//         pmem_init();
//         kvm_init();
//         kvm_inithart();
//         trap_kernel_init();
//         trap_kernel_inithart();

//         __sync_synchronize();
//         started = 1;
//         proc_make_first();
//     }
//     else
//     {

//         while (started == 0)
//             ;
//         __sync_synchronize();
//         printf("cpu %d is booting!\n", cpuid);
//         kvm_inithart();
//         trap_kernel_inithart();
//     }
//     while (1)
//         ;
// }
// in main.c
volatile static int started = 0;
volatile static bool over_1 = false, over_2 = false;
volatile static bool over_3 = false, over_4 = false;

void *mmap_list[N_MMAP];

int main()
{
    int cpuid = r_tp();

    if (cpuid == 0)
    {

        print_init();
        printf("cpu %d is booting!\n", cpuid);
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();

        // 初始化 + 初始状态显示
        mmap_init();
        mmap_show_nodelist();
        printf("\n");

        __sync_synchronize();
        started = 1;

        // 申请
        for (int i = 0; i < N_MMAP / 2; i++)
            mmap_list[i] = mmap_region_alloc();
        over_1 = true;

        // 屏障
        while (over_1 == false || over_2 == false)
            ;

        // 释放
        for (int i = 0; i < N_MMAP / 2; i++)
            mmap_region_free(mmap_list[i]);
        over_3 = true;

        // 屏障
        while (over_3 == false || over_4 == false)
            ;

        // 查看结束时的状态
        mmap_show_nodelist();
    }
    else
    {

        while (started == 0)
            ;
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        kvm_inithart();
        trap_kernel_inithart();

        // 申请
        for (int i = N_MMAP / 2; i < N_MMAP; i++)
            mmap_list[i] = mmap_region_alloc();
        over_2 = true;

        // 屏障
        while (over_1 == false || over_2 == false)
            ;

        // 释放
        for (int i = N_MMAP / 2; i < N_MMAP; i++)
            mmap_region_free(mmap_list[i]);
        over_4 = true;
    }

    while (1)
        ;
}