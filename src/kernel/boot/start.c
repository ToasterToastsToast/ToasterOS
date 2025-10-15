#include "../arch/mod.h"

// 每个CPU在运行操作系统时需要一个初始的函数栈
__attribute__((aligned(16))) uint8 CPU_stack[4096 * NCPU];

extern void main();

void start()
{
    // 暂时不开启分页，使用物理地址
    w_satp(0);

    // 切换到S-mode后无法访问M-mode的寄存器
    // 所以需要将hartid存到可访问的寄存器tp
    // 之后可以用mycpuid函数访问它
    int id = r_mhartid();
    w_tp(id);

    // 委托S-mode处理所有trap
    w_medeleg(0xffff); // 抄的，Machine Exception Delegation 寄存器
    w_mideleg(0xffff); // Machine Interrupt Delegation 寄存器
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE); // 启 S-mode 下的 外部/定时器/软件中断。
    
    // 时钟中断初始化 (唯一需要在M-mode处理的中断)
    timerinit();//抄的
    // 修改mstatus寄存器，假装上一个状态是S-mode
    uint64 status = r_mstatus();
    status &= ~MSTATUS_MPP_MASK;
    status |= MSTATUS_MPP_S;
    w_mstatus(status);

    // 设置M-mode的返回地址
    w_mepc((uint64)main);
    // 触发状态迁移，回到上一个状态（M-mode->S-mode）
    asm volatile("mret");
}

void timerinit()
{
    // each CPU has a separate source of timer interrupts.
    int id = r_mhartid();

    // ask the CLINT for a timer interrupt.
    int interval = 1000000; // cycles; about 1/10th second in qemu.
    *(uint64 *)CLINT_MTIMECMP(id) = *(uint64 *)CLINT_MTIME + interval;

    // prepare information in scratch[] for timervec.
    // scratch[0..3] : space for timervec to save registers.
    // scratch[4] : address of CLINT MTIMECMP register.
    // scratch[5] : desired interval (in cycles) between timer interrupts.
    uint64 *scratch = &mscratch0[32 * id];
    scratch[4] = CLINT_MTIMECMP(id);
    scratch[5] = interval;
    w_mscratch((uint64)scratch);

    // set the machine-mode trap handler.
    w_mtvec((uint64)timervec);

    // enable machine-mode interrupts.
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // enable machine-mode timer interrupts.
    w_mie(r_mie() | MIE_MTIE);
}
