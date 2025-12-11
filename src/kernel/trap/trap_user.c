#include "../../user/syscall_num.h" // 需要 SYS_helloworld
#include "../proc/mod.h"            // 需要 myproc()
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
extern char user_return[]; // 内核处理完毕返回用户


// in trap.S
extern char
    kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口

// in trap_kernel.c
extern char *interrupt_info[16]; // 中断错误信息
extern char *exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler() {
    proc_t *p = myproc();       // 获取当前进程
    uint64 scause = r_scause(); // 获取 trap 原因
    uint64 stval = r_stval();   // 获取 trap 附加信息
    uint64 sepc = r_sepc();     // 获取 trap 时的 PC

    // 切换到内核陷阱向量
    //    防止在 S-mode 再次发生 trap 时进入 user_vector
    w_stvec((uint64)kernel_vector);

    // 保存用户态 PC 到 trapframe
    p->tf->user_to_kern_epc = sepc;

    int trap_id = scause & 0xf;

    // 判断 trap 类型 (中断 还是 异常)
    if (scause & 0x8000000000000000ul) {
        // --- 中断 ---
        // lab-4.md 要求测试中断是否正常工作
        switch (trap_id) {
        case 1: // S-mode software interrupt
        case 5: // S-mode timer interrupt (M-mode 委托)
            timer_interrupt_handler();
            break;
        case 9: // S-mode external interrupt (PLIC)
            external_interrupt_handler();
            break;
        default:
            printf("\nunexpected user interrupt: %s\n",
                   interrupt_info[trap_id]);
            printf("scause %p, sepc %p, stval %p\n", scause, sepc, stval);
            panic("trap_user_handler: interrupt");
        }
    } else {
        // --- 异常 ---
        switch (trap_id) {
        case 8: // Environment call from U-mode (系统调用)
            p->tf->user_to_kern_epc += 4;
            syscall();
            break;

        case 13: // Load Page Fault
        case 15: // Store/AMO Page Fault
            printf("trap_user_handler: User Page Fault! scause=%x, stval=%x\n",
                   scause, stval); // 添加调试输出
            uint64 new_npage =
                uvm_ustack_grow(p->pgtbl, p->ustack_npage, stval);
            // 尝试栈自动增长
            if (new_npage != (uint64)-1) {
                // 成功处理缺页，返回用户态继续执行
                printf(
                    "trap_user_handler: Stack successfully grown.\n"); // 添加调试输出
                break; // 跳出 switch，进入 trap_user_return
            }
            // 如果栈增长失败或 stval 是不合理的 Page Fault 地址，则 fall
            // through 到 default

        default:
            // 其他异常 (包括不合理的 Page Fault)
            printf("\nunexpected user exception: %s\n",
                   exception_info[trap_id]);
            printf("scause %x, sepc %x, stval %x\n", scause, sepc,
                   stval); // 使用 %x 打印 64 位地址
            panic("trap_user_handler: exception");
        }
    }

    if ((scause & 0x8000000000000000ul) && trap_id == 1) {
        proc_yield();
    }

    // 调用 "返回用户态" 流程
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    // 【关键修复】必须先关闭中断，防止在设置寄存器期间被时钟中断打断
    // 如果不关中断，在设置 stvec 为 user_vector 后但还没 sret 前发生时钟中断，
    // 内核会误以为是用户态陷阱，导致 panic 或状态错误。
    intr_off(); 

    proc_t *p = myproc();

    // 再次设置 S-mode 陷阱入口为 user_vector (Trampoline 中的位置)
    uint64 user_vector_addr = (uint64)TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline);
    w_stvec(user_vector_addr);

    //    填充 trapframe 中的 "内核信息"
    //    (trampoline.S 中的 user_vector 会用到它们来恢复内核环境)
    p->tf->user_to_kern_satp = r_satp();                        // 内核页表
    p->tf->user_to_kern_sp = p->kstack + PGSIZE;                // 内核栈顶
    p->tf->user_to_kern_trapvector = (uint64)trap_user_handler; // trap处理函数
    p->tf->user_to_kern_hartid = r_tp();                        // hartid

    // 将 sscratch 指向 trapframe，以便 user_vector 保存用户寄存器
    w_sscratch((uint64)TRAPFRAME);

    // 设置 sstatus 寄存器
    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP; // 清除 SPP: S-mode 的上一个状态是 U-mode
    sstatus |= SSTATUS_SPIE; // 使能 U-mode 的中断
    w_sstatus(sstatus);

    // 设置 sepc (设置返回用户态时的 PC)
    w_sepc(p->tf->user_to_kern_epc);

    //    准备调用 user_return (位于 trampoline.S)
    //    a0 = TRAPFRAME (用户虚拟地址)
    //    a1 = 用户页表 (SATP 格式)
    uint64 user_pgtbl_satp = MAKE_SATP(p->pgtbl);

    // 计算 user_return 在 trampoline 页中的虚拟地址
    uint64 user_return_addr = (uint64)TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    void (*user_return_func)(uint64, uint64) = (void (*)(uint64, uint64))user_return_addr;

    //    调用汇编函数, 进入用户态
    //    此函数会切换页表, 恢复所有寄存器, 并执行 sret (sret 会重新开启中断)
    user_return_func(TRAPFRAME, user_pgtbl_satp);

    // user_return 永远不会返回
    panic("trap_user_return: failed to return to user");
}