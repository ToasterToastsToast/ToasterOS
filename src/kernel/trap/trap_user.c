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

    // 1. 设置S-mode的陷阱入口为 user_vector
    //    当在用户态发生trap时，会进入 user_vector
    w_stvec((uint64)user_vector);

    // 2. 准备 trapframe，供 trampoline.S 中的 user_vector 使用
    // 1. 设置内核页表的SATP值
    p->tf->user_to_kern_satp = MAKE_SATP(kernel_pgtbl);
    // 2. 设置内核栈顶
    p->tf->user_to_kern_sp = p->kstack + PGSIZE;
    // 3. 设置内核陷阱处理函数
    p->tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    // 4. (可选但推荐) 设置内核的tp寄存器 (hartid)
    p->tf->user_to_kern_hartid = r_tp();

    // 3. 设置 SSTATUS 寄存器
    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP; // 清除SPP，表明返回到 U-mode
    sstatus |= SSTATUS_SPIE; // 允许在 U-mode 响应中断
    w_sstatus(sstatus);

    // 4. 设置返回到 U-mode 后的 PC (程序计数器)
    w_sepc(p->tf->user_to_kern_epc);

    // 5. 计算 user page table 的 satp 值
    uint64 satp = MAKE_SATP(p->pgtbl);

    // 6. 调用 trampoline.S 中的 user_return 汇编函数
    //    它会负责切换页表、恢复GPRs、并执行 sret
    user_return((uint64)p->tf, satp);
}