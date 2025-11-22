#include "mem/mod.h"
#include "proc/mod.h"
#include "trap/mod.h"
#include "syscall/mod.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        
        print_init();
        printf("cpu %d is booting!\n", cpuid);
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();
        
        // 初始化 mmap 资源仓库
        mmap_init();
        
        // 创建第一个用户进程并直接启动
        proc_make_first();
        
        // proc_make_first() 内部会调用 trap_user_return()，不会返回

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        kvm_inithart();
        trap_kernel_inithart();
    }

    while (1);
}