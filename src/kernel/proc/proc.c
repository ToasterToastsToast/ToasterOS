#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

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
    // 1. 设置PID
    proczero.pid = 0;

    // 2. 分配并映射内核栈 (KStack)
    // KSTACK(0) 是虚拟地址, kstack_pa 是物理地址
    uint64 kstack_pa = (uint64)pmem_alloc(true);
    if(kstack_pa == NULL)
        panic("proc_make_first: out of memory for kstack");
    memset((void*)kstack_pa, 0, PGSIZE);
    proczero.kstack = KSTACK(0);
    // 将这个 KStack 映射到 *内核* 页表中
    vm_mappages(kernel_pgtbl, proczero.kstack, kstack_pa, PGSIZE, PTE_R | PTE_W);

    // 3. 分配 Trapframe
    proczero.tf = (trapframe_t*)pmem_alloc(true);
    if(proczero.tf == NULL)
        panic("proc_make_first: out of memory for trapframe");
    memset(proczero.tf, 0, PGSIZE);

    // 4. 创建用户页表 (它会顺便映射 trampoline 和 trapframe)
    proczero.pgtbl = proc_pgtbl_init((uint64)proczero.tf);

    // 5. 分配并映射用户代码页 (initcode)
    //    映射到虚拟地址 0
    uint64 code_pa = (uint64)pmem_alloc(true);
    if(code_pa == NULL)
        panic("proc_make_first: out of memory for initcode");
    memmove((void*)code_pa, initcode, initcode_len);
    vm_mappages(proczero.pgtbl, 0, code_pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    proczero.heap_top = PGSIZE; // 堆顶在代码页之后

    // 6. 分配并映射用户栈 (USTACK)
    uint64 ustack_pa = (uint64)pmem_alloc(true);
    if(ustack_pa == NULL)
        panic("proc_make_first: out of memory for ustack");
    memset((void*)ustack_pa, 0, PGSIZE);
    // USTACK_VA 是在 type.h 中定义的用户栈虚拟地址
    vm_mappages(proczero.pgtbl, USTACK_VA, ustack_pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    proczero.ustack_npage = 1;

    // 7. 设置 Trapframe，为第一次进入用户态做准备
    proczero.tf->user_to_kern_epc = 0;                     // 用户态入口点 (VA 0)
    proczero.tf->sp = USTACK_VA + PGSIZE;     // 用户栈顶

    // 8. 设置内核上下文，为第一次 swtch 做准备
    proczero.ctx.ra = (uint64)trap_user_return; // swtch后执行的函数
    proczero.ctx.sp = kstack_pa + PGSIZE;       // 内核栈顶

    // 9. 切换上下文
    //    从当前的 main (内核启动) 上下文 切换到 proczero 的内核上下文
    cpu_t *cpu = mycpu();
    swtch(&cpu->ctx, &proczero.ctx);
}