#include "../proc/mod.h"         // 需要 myproc()
#include "../../user/syscall_num.h" // 需要 SYS_helloworld
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
<<<<<<< HEAD
extern char kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口
extern pgtbl_t kernel_pgtbl; // 需要内核页表
=======
extern char
    kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口
>>>>>>> origin/lab-4-2-attempt

// in trap_kernel.c
extern char *interrupt_info[16]; // 中断错误信息
extern char *exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
<<<<<<< HEAD
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
=======
void trap_user_handler() {
    proc_t *p = myproc();       // 获取当前进程
    uint64 scause = r_scause(); // 获取 trap 原因
    uint64 stval = r_stval();   // 获取 trap 附加信息
    uint64 sepc = r_sepc();     // 获取 trap 时的 PC

    // 1. 切换到内核陷阱向量
    //    防止在 S-mode 再次发生 trap 时进入 user_vector
    w_stvec((uint64)kernel_vector);

    // 2. 保存用户态 PC 到 trapframe
    p->tf->user_to_kern_epc = sepc;

    // 3. 判断 trap 类型 (中断 还是 异常)
    if (scause & 0x8000000000000000ul) {
        // --- 中断 ---
        // lab-4.md 要求测试中断是否正常工作
        int trap_id = scause & 0xf;
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
        int trap_id = scause & 0xf;
        switch (trap_id) {
        case 8: // Environment call from U-mode (系统调用)

            // 从 a7 寄存器获取系统调用号
            // (user_vector 已经将其保存在 trapframe 中)
            uint64 sys_num = p->tf->a7;

            if (sys_num == SYS_helloworld) {
                // 响应 lab-4 的核心目标
                printf("proczero: hello world!\n");
            } else {
                printf("trap_user_handler: unknown syscall num %d\n", sys_num);
            }

            // !!重要!!: ecall 指令是异常, 但返回时 PC 必须 +4,
            // 否则会无限循环执行 ecall
            p->tf->user_to_kern_epc += 4;
            break;

        default:
            // 其他异常 (如 Page Fault 等)
            printf("\nunexpected user exception: %s\n",
                   exception_info[trap_id]);
            printf("scause %p, sepc %p, stval %p\n", scause, sepc, stval);
            panic("trap_user_handler: exception");
        }
    }

    // 4. 调用 "返回用户态" 流程
>>>>>>> origin/lab-4-2-attempt
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
<<<<<<< HEAD
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
=======
void trap_user_return() {
    intr_off(); // 【【添加此行以关闭中断】】
    proc_t *p = myproc();

    // 1. 再次设置 S-mode 陷阱入口为 user_vector
    //    (下次从 U-mode 陷入时会进入 user_vector)
    // w_stvec((uint64)user_vector);
    uint64 user_vector_addr =
        (uint64)TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline);
    w_stvec(user_vector_addr);

    // 2. 填充 trapframe 中的 "内核信息"
    //    (trampoline.S 中的 user_vector 会用到它们)
    p->tf->user_to_kern_satp = r_satp();                        // 内核页表
    p->tf->user_to_kern_sp = p->kstack + PGSIZE;                // 内核栈顶
    p->tf->user_to_kern_trapvector = (uint64)trap_user_handler; // trap处理函数
    p->tf->user_to_kern_hartid = r_tp();                        // hartid

    w_sscratch((uint64)TRAPFRAME);

    // 3. 设置 sstatus 寄存器
    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP; // 清除 SPP: S-mode 的上一个状态是 U-mode
    sstatus |= SSTATUS_SPIE; // 使能 U-mode 的中断
    w_sstatus(sstatus);

    // 4. 设置 sepc (设置返回用户态时的 PC)
    //    (这个值在 handler 中已经被设为 0 或 0+4)
    w_sepc(p->tf->user_to_kern_epc);

    // 5. 准备调用 user_return (位于 trampoline.S)
    //    a0 = TRAPFRAME (用户虚拟地址)
    //    a1 = 用户页表 (SATP 格式)
    uint64 user_pgtbl_satp = MAKE_SATP(p->pgtbl);

    // 定义一个函数指针, 指向 user_return
    // void (*user_return_func)(uint64, uint64) =
    // (void (*)(uint64, uint64))user_return;
    uint64 user_return_addr =
        (uint64)TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    void (*user_return_func)(uint64, uint64) =
        (void (*)(uint64, uint64))user_return_addr;


    // 6. 调用汇编函数, 进入用户态
    //    此函数会切换页表, 恢复所有寄存器, 并执行 sret
    user_return_func(TRAPFRAME, user_pgtbl_satp);

    // user_return 永远不会返回
    panic("trap_user_return: failed to return to user");
>>>>>>> origin/lab-4-2-attempt
}