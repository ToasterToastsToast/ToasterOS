#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len
// initcode entry 偏移（相对于页面基址 PGSIZE）
#define INITCODE_ENTRY_OFFSET 0x2c

// in trampoline.S
extern char trampoline[];

extern pgtbl_t kernel_pgtbl; // 需要内核页表

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();

// 第一个用户进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成trapframe和trampoline的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    // 1. 为顶级页表分配一个物理页
    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(true);
    if(pgtbl == NULL)
        panic("proc_pgtbl_init: out of memory");
    
    memset(pgtbl, 0, PGSIZE);
    extern char trampoline[];
    // 2. 映射 trampoline
    //    内核和用户空间使用相同的虚拟地址 TRAMPOLINE
    //    权限：R-X (执行)
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // 3. 映射 trapframe
    //    TRAPFRAME 是一个虚拟地址 (在TRAMPOLINE下面)
    //    trapframe 是传入的物理地址
    //    权限：R-W (读写)，因为内核需要读写它
    vm_mappages(pgtbl, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W);

    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问

    注意: 用用户空间的地址映射需要标记 PTE_U
*/
void proc_make_first()
{
// 用户栈地址定义
#define USTACK (TRAPFRAME - PGSIZE)

    proc_t *p = &proczero;
    p->pid = 0;

    // 分配 trapframe
    p->tf = (trapframe_t *)pmem_alloc(true);
    if (p->tf == NULL)
        panic("proc_make_first: alloc tf failed");
    memset(p->tf, 0, PGSIZE);

    // 创建用户页表
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);

    // 用户栈
    void *ustack_pa = pmem_alloc(false);
    if (ustack_pa == NULL)
        panic("proc_make_first: alloc ustack failed");
    vm_mappages(p->pgtbl, USTACK, (uint64)ustack_pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    p->ustack_npage = 1;

    // 用户代码页放在 USER_BASE
    void *ucode_pa = pmem_alloc(false);
    if (ucode_pa == NULL)
        panic("proc_make_first: alloc ucode failed");
    memset(ucode_pa, 0, PGSIZE);
    memmove(ucode_pa, initcode, MIN((uint32)initcode_len, (uint32)PGSIZE));
    vm_mappages(p->pgtbl, USER_BASE, (uint64)ucode_pa, PGSIZE, PTE_R | PTE_X | PTE_U);

    p->heap_top = USER_BASE + PGSIZE;

    // 填写 trapframe 关键字段
    p->tf->user_to_kern_satp = r_satp();
    p->tf->user_to_kern_sp = KSTACK(0) + PGSIZE; // 内核栈顶
    extern void trap_user_handler();
    p->tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    p->tf->user_to_kern_epc = USER_BASE + INITCODE_ENTRY_OFFSET; // 用户程序入口点 (USER_BASE + ELF entry point)
    p->tf->user_to_kern_hartid = r_tp();
    p->tf->sp = USTACK + PGSIZE;

    // 设置进程的内核栈和上下文
    p->kstack = KSTACK(0);
    p->ctx.sp = KSTACK(0) + PGSIZE;       // 内核栈顶
    p->ctx.ra = (uint64)trap_user_return; // 返回地址

    // 当前CPU绑定该进程并切回用户
    cpu_t *c = mycpu();
    c->proc = p;

    trap_user_return();
}