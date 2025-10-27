#include "../arch/mod.h"

// 每个CPU在运行操作系统时需要一个初始的函数栈
__attribute__((aligned(16))) uint8 CPU_stack[4096 * NCPU];

extern void main();
extern void timer_init();
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
    w_medeleg(0xffff);

    // 时钟中断初始化 (唯一需要在M-mode处理的中断)
    // (SSIP, STIP, SEIP - S态软件/时钟/外设中断)
    w_mideleg((1 << 1) | (1 << 5) | (1 << 9));
    timer_init();

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
