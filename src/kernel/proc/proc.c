#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

// in trampoline.S
extern char trampoline[];

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
    pgtbl_t pgtbl;

    // 1. 分配一个物理页作为根页表 (用户资源)
    pgtbl = (pgtbl_t)pmem_alloc(false);
    if (pgtbl == NULL) {
        panic("proc_pgtbl_init: out of memory for pgtbl");
    }
    memset(pgtbl, 0, PGSIZE);

    // 2. 映射 trampoline
    // VA: TRAMPOLINE, PA: (uint64)trampoline
    // 权限: 读, 执行, 用户态 (R, X, U)
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline,
                PGSIZE, PTE_R | PTE_X | PTE_U);

    // 3. 映射 trapframe
    // VA: TRAPFRAME, PA: trapframe (参数)
    // 权限: 读, 写, 用户态 (R, W, U)
    vm_mappages(pgtbl, TRAPFRAME, trapframe,
                PGSIZE, PTE_R | PTE_W | PTE_U);

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
    // 1. 设置pid
    proczero.pid = 0;

    // 2. 申请 trapframe 物理页
    proczero.tf = (trapframe_t *)pmem_alloc(false); // 用户资源
    if (proczero.tf == NULL) {
        panic("proc_make_first: out of memory for trapframe");
    }
    memset(proczero.tf, 0, PGSIZE);

    // 3. 申请用户页表 (并映射 TRAMPOLINE 和 TRAPFRAME)
    proczero.pgtbl = proc_pgtbl_init((uint64)proczero.tf);

    // 4. 申请 ustack 物理页
    uint64 ustack_pa = (uint64)pmem_alloc(false);
    if (ustack_pa == NULL) {
        panic("proc_make_first: out of memory for ustack");
    }
    memset((void*)ustack_pa, 0, PGSIZE);

    // 5. 映射 ustack
    // VA: USTACK, PA: ustack_pa
    // 权限: 读, 写, 用户态 (R, W, U)
    vm_mappages(proczero.pgtbl, USTACK, ustack_pa,
                PGSIZE, PTE_R | PTE_W | PTE_U);

    // 6. 设置 ustack_npage
    proczero.ustack_npage = 1; // 我们分配了 1 页

    // 7. 申请 code+data 物理页
    uint64 initcode_pa = (uint64)pmem_alloc(false);
    if (initcode_pa == NULL) {
        panic("proc_make_first: out of memory for initcode");
    }

    // 8. 转移数据 (initcode -> 物理页)
    if (initcode_len > PGSIZE) {
        panic("proc_make_first: initcode larger than one page");
    }
    memmove((void*)initcode_pa, initcode, initcode_len);

    // 9. 映射 ELF (code+data)
    // VA: 0x0, PA: initcode_pa
    // 权限: 读, 写, 执行, 用户态 (R, W, X, U)
    // vm_mappages(proczero.pgtbl, 0, initcode_pa,
                // PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    vm_mappages(proczero.pgtbl, PGSIZE, initcode_pa,
            PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    // 设置 heap_top, 初始指向 code+data 之后
    proczero.heap_top = PGSIZE * 2; // 应该是 PGSIZE*2，因为代码现在在第2页

    // 10. 设置 trapframe 中的初始值
    // epc (即 user_to_kern_epc) 设置为 PGSIZE
    proczero.tf->user_to_kern_epc = PGSIZE;
    // sp 设置为用户栈顶 (USTACK + PGSIZE)
    proczero.tf->sp = USTACK + PGSIZE;

    // 11. 设置内核相关字段
    // kstack 虚拟地址
    proczero.kstack = KSTACK_VA(0); // 必须与 kvm_init 中一致
    // context.ra (swtch 返回后跳转到 trap_user_return)
    proczero.ctx.ra = (uint64)trap_user_return;
    // context.sp (内核栈顶)
    proczero.ctx.sp = proczero.kstack + PGSIZE;

    // 12. 将 proczero 绑定到当前 CPU
    mycpu()->proc = &proczero;

    printf("proc_make_first: switching to proczero...\n");

    // 13. 切换上下文
    // 从 main 的内核上下文 (mycpu()->ctx)
    // 切换到 proczero 的内核上下文 (proczero.ctx)
    swtch(&mycpu()->ctx, &proczero.ctx);

    // swtch 永远不会返回到这里
    panic("proc_make_first: swtch returned?!");
}