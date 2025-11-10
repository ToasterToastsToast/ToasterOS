#include "mod.h"

#ifndef PGSIZE
#define PGSIZE 4096
#endif

#ifndef SYS_helloworld
#define SYS_helloworld 0
#endif



// in trampoline.S
extern char trampoline[];  // 内核和用户切换的代码
extern char user_vector[]; // 用户触发陷阱进入内核
// extern char user_return[]; // 内核处理完毕返回用户
// extern void user_return(uint64 tf, uint64 satp);
extern void user_return(uint64, uint64); // <- 添加这行，声明为函数

// in trap.S
extern char kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口
extern pgtbl_t kernel_pgtbl; // 需要内核页表

// in trap_kernel.c
extern char *interrupt_info[16]; // 中断错误信息
extern char *exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    // 切换回内核的陷阱处理函数
    w_stvec((uint64)kernel_vector);

    proc_t *p = myproc();
    
    // 保存用户态的PC，以便返回
    p->tf->user_to_kern_epc = r_sepc();
    
    uint64 scause = r_scause();
    
    // 检查是否是 U-mode ecall (系统调用)
    if (scause == 8) { // Exception code 8 = Ecall from U-mode
        
        // 从 a7 寄存器获取系统调用号
        int sysnum = p->tf->a7;

        if (sysnum == SYS_helloworld) {
            // 目标达成：响应syscall
            printf("proczero: hello world!\n");
        } else {
            printf("unknown syscall %d\n", sysnum);
        }

        // 重要：系统调用返回时，PC应指向下一条指令
        // 中断/异常返回时是重新执行当前指令
        p->tf->user_to_kern_epc += 4;

    } else {
        // 其他中断或异常 (例如 lab-3 的时钟/串口中断)
        printf("user trap scause 0x%p, sepc 0x%p\n", scause, p->tf->user_to_kern_epc);
        panic("unhandled user trap");
    }

    // 处理完毕，准备返回用户态
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t *p = myproc();
    trapframe_t *tf = p->tf;

    // stvec -> 用户向量（高地址）
    uint64 uservec_va = TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline);
    w_stvec(uservec_va);

    // sscratch 写入 TRAPFRAME 虚拟地址
    w_sscratch((uint64)TRAPFRAME);

    // sret 到 U：清 SPP，置 SPIE
    uint64 s = r_sstatus();
    s &= ~SSTATUS_SPP;
    s |= SSTATUS_SPIE;
    w_sstatus(s);

    // 设置 sepc
    w_sepc(tf->user_to_kern_epc);

    // 跳转 trampoline 的 user_return(trapframe_va, user_satp)
    uint64 userret_va = TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    void (*ureturn)(trapframe_t *, uint64) = (void (*)(trapframe_t *, uint64))userret_va;
    ureturn((trapframe_t *)TRAPFRAME, MAKE_SATP(p->pgtbl));
}