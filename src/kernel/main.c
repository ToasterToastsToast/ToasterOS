#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
static volatile int started = 0;

#define BUFFER_SIZE 128

void echo_test(void)
{

    const char *prompt = "--- Echo Test: Enter text  ---\n";
    int i = 0;

    while (prompt[i] != '\0')
    {
        uart_putc_sync_ext(prompt[i]);
        i++;
    }

    // 主循环：读取字符并回显
    while (1)
    {
        int c = uart_getc_sync(); // 非阻塞式读取

        if (c != -1)
        {
            uart_putc_sync_ext(c);
        }
    }
}
int main()
{
    int cpuid = r_tp();

    if (cpuid == 0)
    {
        // --- Lab-1 & 2 的初始化 ---
        print_init();
        pmem_init();
        kvm_init();

        // --- 【【Lab-3 新增】】 ---
        // 调用 trap 模块的共享初始化
        // 这会调用 timer_create()
        trap_kernel_init();
        plic_init(); // set up interrupt controller
        // --- Lab-2 的初始化 ---
        kvm_inithart(); // 启用内核页表

        // --- 【【Lab-3 新增】】 ---
        // 调用 trap 模块的核心独有初始化
        // 这会设置 stvec 指向 kernel_vector 并打开中断
        trap_kernel_inithart();
        plic_inithart(); // ask PLIC for device interrupts
        printf("cpu %d is booting!\n", cpuid);

        // 唤醒其他核心
        __sync_synchronize();
        started = 1;
    }
    else
    {
        // --- 其他核心 (APs) ---
        while (started == 0)
            ;
        __sync_synchronize();

        kvm_inithart(); // 启用内核页表

        // --- 【【Lab-3 新增】】 ---
        // 每个核心都需要初始化自己的中断
        trap_kernel_inithart();
        plic_inithart(); // ask PLIC for device interrupts
        printf("cpu %d is booting!\n", cpuid);
    }

    // main 函数不再执行任何测试，
    // 只是无限循环。
    // 中断处理程序会在后台自动打印 "tick"。
    if(started && cpuid==1){
        echo_test();
    }
    while (1)
        ;
}