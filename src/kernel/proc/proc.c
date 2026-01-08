#include "mod.h"
#include "../arch/mod.h"
#include "../mem/mod.h"
#include "../lib/mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len
// initcode entry 偏移（相对于页面基址 PGSIZE）
#define INITCODE_ENTRY_OFFSET 0x2c
#define USTACK (TRAPFRAME - PGSIZE)
// in trampoline.S
extern char trampoline[];

extern pgtbl_t kernel_pgtbl; // 需要内核页表

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();
extern void kernel_vector();
/* ------------本地变量----------- */

// 进程结构体数组 + 第一个用户进程的指针
static proc_t proc_list[N_PROC];
static proc_t *proczero;

// 全局pid + 保护它的锁
static int global_pid;
static spinlock_t pid_lk;
static spinlock_t wait_lk; // OKOS 经验：专门用于 wait/exit 同步的锁，防止死锁

/* 获取一个pid */
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&pid_lk);
    assert(global_pid > 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&pid_lk);
    return tmp;
}

/* 释放进程锁 + trap_user_return */
static void proc_return()
{
    // w_stvec((uint64)kernel_vector);

    spinlock_release(&myproc()->lk); // 先释放调度器转交过来的锁
    fs_init();                       // 初始化文件系统（会触发磁盘I/O）

    // 为 proczero 初始化文件系统资源
    proc_t *p = myproc();
    if (p == proczero && p->cwd == NULL)
    {
        p->cwd = inode_get(ROOT_INODE);
        for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++)
            p->open_file[i] = NULL;
        p->open_file[0] = file_open("/dev/stdin", FILE_OPEN_READ);
        p->open_file[1] = file_open("/dev/stdout", FILE_OPEN_WRITE);
        p->open_file[2] = file_open("/dev/stderr", FILE_OPEN_WRITE);
    }

    trap_user_return();
}

/* 进程模块初始化 */
void proc_init()
{
    spinlock_init(&pid_lk, "pid");
    spinlock_init(&wait_lk, "wait");
    global_pid = 1;

    for (int i = 0; i < N_PROC; i++)
    {
        spinlock_init(&proc_list[i].lk, "proc");
        proc_list[i].state = UNUSED;
        proc_list[i].kstack = KSTACK_VA(i); // 预先计算好内核栈地址
        proc_list[i].cwd = NULL;
        for (int j = 0; j < N_OPEN_FILE_PER_PROC; j++)
            proc_list[i].open_file[j] = NULL;
    }
}

/*
    申请一个UNUSED进程结构体(返回时带锁)
    并执行通用的初始化逻辑
*/
proc_t *proc_alloc()
{
    proc_t *p = NULL;

    // 1. 寻找空闲槽位
    for (int i = 0; i < N_PROC; i++)
    {
        spinlock_acquire(&proc_list[i].lk);
        if (proc_list[i].state == UNUSED)
        {
            p = &proc_list[i];
            break;
        }
        spinlock_release(&proc_list[i].lk);
    }
    if (p == NULL)
        return NULL;

    // 2. 初始化基础信息
    p->pid = alloc_pid();
    p->state = RUNNABLE; // 分配完成前保持 UNUSED
    p->parent = NULL;
    p->exit_code = 0;
    p->sleep_space = NULL;
    p->mmap = NULL;
    p->heap_top = 0;
    p->ustack_npage = 0;
    p->cwd = NULL;
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++)
        p->open_file[i] = NULL;
    memset(p->name, 0, sizeof(p->name));

    // 3. 分配 Trapframe
    if ((p->tf = (trapframe_t *)pmem_alloc(true)) == NULL)
    {
        p->state = UNUSED;
        spinlock_release(&p->lk);
        return NULL;
    }
    memset(p->tf, 0, PGSIZE);

    // 4. 初始化页表 (映射 trampoline 和 trapframe)
    if ((p->pgtbl = proc_pgtbl_init((uint64)p->tf)) == NULL)
    {
        pmem_free((uint64)p->tf, true);
        p->tf = NULL;
        spinlock_release(&p->lk);
        return NULL;
    }

    // 5. 初始化内核上下文 (Context)
    // 关键点：设置 ra 为 proc_return，这样第一次调度该进程时，
    // swtch 返回后会跳转到 proc_return，进而进入用户态
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->ctx.ra = (uint64)proc_return;
    p->ctx.sp = p->kstack + PGSIZE; // 内核栈顶

    return p; // 返回持有锁的进程指针
}

/*
    回收一个进程结构体并释放它包含的资源
    tips: 调用者需要持有进程锁
*/
void proc_free(proc_t *p)
{
    if (p->tf)
        pmem_free((uint64)p->tf, true);
    p->tf = NULL;

    if (p->pgtbl)
        uvm_destroy_pgtbl(p->pgtbl);
    p->pgtbl = NULL;

    // 释放文件资源
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++)
    {
        if (p->open_file[i] != NULL)
        {
            file_close(p->open_file[i]);
            p->open_file[i] = NULL;
        }
    }
    if (p->cwd != NULL)
    {
        inode_put(p->cwd);
        p->cwd = NULL;
    }

    p->pid = 0;
    p->parent = NULL;
    p->name[0] = 0;
    p->state = UNUSED;
    spinlock_release(&p->lk);
}

// 获得一个初始化过的用户页表
// 完成trapframe和trampoline的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t pgtbl;

    // 1. 分配一个物理页作为根页表 (用户资源)
    pgtbl = (pgtbl_t)pmem_alloc(false);
    if (pgtbl == NULL)
    {
        panic("proc_pgtbl_init: out of memory for pgtbl");
    }
    memset(pgtbl, 0, PGSIZE);

    // 2. 映射 trampoline
    // VA: TRAMPOLINE, PA: (uint64)trampoline
    // 权限: 读, 执行, 用户态 (R, X, U)
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline,
                PGSIZE, PTE_R | PTE_X);

    // 3. 映射 trapframe
    // VA: TRAPFRAME, PA: trapframe (参数)
    // 权限: 读, 写, 用户态 (R, W, U)
    vm_mappages(pgtbl, TRAPFRAME, trapframe,
                PGSIZE, PTE_R | PTE_W);

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
    proc_t *p = proc_alloc();
    if (!p)
        panic("proc_make_first failed");
    proczero = p;

    // 映射用户栈
    extern void trap_user_handler();
    void *ustack_pa = pmem_alloc(false);
    if (!ustack_pa)
        panic("proc_make_first ustack");
    vm_mappages(p->pgtbl, USTACK, (uint64)ustack_pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    p->ustack_npage = 1;

    // 映射代码段 (user base)
    void *code_pa = pmem_alloc(false);
    if (!code_pa)
        panic("proc_make_first code");
    memmove(code_pa, initcode, initcode_len);
    // 注意：initcode 从 0x0 (USER_BASE) 开始执行
    vm_mappages(p->pgtbl, USER_BASE, (uint64)code_pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    p->heap_top = USER_BASE + PGSIZE;

    // 设置 Trapframe
    p->tf->user_to_kern_satp = r_satp();
    p->tf->user_to_kern_sp = p->kstack + PGSIZE;
    p->tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    p->tf->user_to_kern_epc = USER_BASE; // PC
    p->tf->sp = USTACK + PGSIZE;         // User SP

    // 命名并启动
    char *name = "proczero";
    for (int i = 0; name[i]; i++)
        p->name[i] = name[i];

    p->state = RUNNABLE;
    spinlock_release(&p->lk);
}

/*
    父进程产生子进程
    UNUSED -> RUNNABLE
*/
int proc_fork()
{

    proc_t *p = myproc();
    proc_t *np = proc_alloc(); // 注意：proc_alloc 返回时已经持有了 np->lk
    if (np == NULL)
        return -1;

    // 1. 复制内存映射 (页表, 堆, 栈, mmap)
    uvm_copy_pgtbl(p->pgtbl, np->pgtbl, p->heap_top, p->ustack_npage, p->mmap);
    np->heap_top = p->heap_top;
    np->ustack_npage = p->ustack_npage;

    // 复制 mmap 链表 (深拷贝)
    mmap_region_t *node = p->mmap;
    mmap_region_t **ptr = &np->mmap;
    while (node)
    {
        mmap_region_t *new_node = mmap_region_alloc();
        *new_node = *node; // 复制内容
        new_node->next = NULL;
        *ptr = new_node;
        ptr = &new_node->next;
        node = node->next;
    }

    // 2. 复制 Trapframe
    *(np->tf) = *(p->tf);
    // 关键修正：子进程必须使用自己的内核栈指针
    np->tf->user_to_kern_sp = np->kstack + PGSIZE;

    // 3. 子进程返回 0
    np->tf->a0 = 0;

    // 4. 设置父子关系
    np->parent = p;
    for (int i = 0; i < 16; i++)
        np->name[i] = p->name[i];

    // 5. 复制文件描述符和工作目录
    np->cwd = (p->cwd != NULL) ? inode_dup(p->cwd) : NULL;
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++)
    {
        if (p->open_file[i] != NULL)
            np->open_file[i] = file_dup(p->open_file[i]);
        else
            np->open_file[i] = NULL;
    }

    // 6. 唤醒子进程
    // 【删除】 spinlock_acquire(&np->lk);  <-- 删掉这一行，因为锁已经在 proc_alloc 中拿到了
    np->state = RUNNABLE;
    spinlock_release(&np->lk); // 释放锁，允许调度器调度子进程

    // printf("called fork\n");
    return np->pid;
}

/*
    进程主动放弃CPU控制权
    RUNNING->RUNNABLE
*/
// 让出 CPU (RUNNING -> RUNNABLE)
void proc_yield()
{
    proc_t *p = myproc();
    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;
    proc_sched();
    spinlock_release(&p->lk);
}

/*
    当父进程退出时, 让它的所有子进程认proczero为父
    因为proczero永不退出, 可以回收子进程的资源
*/
static void proc_reparent(proc_t *p)
{
    for (int i = 0; i < N_PROC; i++)
    {
        proc_t *child = &proc_list[i];
        if (child->parent == p)
        {
            spinlock_acquire(&child->lk);
            child->parent = proczero;
            // 如果子进程已经是僵尸，唤醒 proczero 来回收它
            if (child->state == ZOMBIE)
            {
                proc_try_wakeup(child);
            }
            spinlock_release(&child->lk);
        }
    }
}

/*
    唤醒等待呼叫的进程
    由proc_exit调用
    tips: 调用者需要持有p的进程锁
*/
// 逻辑：检查父进程是否在休眠，且等待的资源(sleep_space)是不是父进程自己
void proc_try_wakeup(proc_t *p)
{
    proc_t *parent = p->parent;
    if (!parent)
        return; // 理论上不应发生，除非是 proczero

    spinlock_acquire(&parent->lk);
    // 父进程在 proc_wait 中是调用 proc_sleep(parent, &wait_lk)
    // 所以它等待的 sleep_space 是 parent 本身
    if (parent->state == SLEEPING && parent->sleep_space == parent)
    {
        parent->state = RUNNABLE;
    }
    spinlock_release(&parent->lk);
}

/*
    进程退出
    RUNNING -> ZOMBIE
*/
void proc_exit(int exit_code)
{
    proc_t *p = myproc();
    if (p == proczero)
        panic("proczero exiting");

    // 1. 必须先持有 wait_lk，以保证父子进程状态变更的原子性
    spinlock_acquire(&wait_lk);

    // 2. 将子进程过继给 proczero
    proc_reparent(p);

    // 3. 唤醒父进程 (如果父进程正在 wait)
    proc_try_wakeup(p);

    // 4. 标记为 ZOMBIE
    spinlock_acquire(&p->lk);
    p->exit_code = exit_code;
    p->state = ZOMBIE;

    // 5. 释放 wait_lk
    spinlock_release(&wait_lk);

    // 6. 调度 (注意: 此时还持有 p->lk，调度器会释放它)
    proc_sched();
    panic("zombie exit");
}

/*
    父进程等待一个子进程进入ZOMBIE状态
    1. 如果等到: 释放子进程, 返回子进程的pid, 将子进程的退出状态传出到user_addr
    2. 如果发现没孩子: 返回-1
    3. 如果没等到: 父进程进入睡眠状态
*/
int proc_wait(uint64 addr)
{
    proc_t *p = myproc();
    spinlock_acquire(&wait_lk);

    while (1)
    {
        int have_kids = 0;
        for (int i = 0; i < N_PROC; i++)
        {
            proc_t *child = &proc_list[i];
            if (child->parent != p)
                continue;

            have_kids = 1;
            spinlock_acquire(&child->lk);
            if (child->state == ZOMBIE)
            {
                int pid = child->pid;
                if (addr != 0 && child->pgtbl)
                {
                    int code = child->exit_code;
                    // 简单拷贝，假设 addr 合法
                    uvm_copyout(p->pgtbl, addr, (uint64)&code, sizeof(int));
                }
                proc_free(child);
                // spinlock_release(&child->lk); // 先释放子进程锁
                spinlock_release(&wait_lk); // 再释放 wait 锁
                return pid;
            }
            spinlock_release(&child->lk);
        }

        if (!have_kids)
        {
            spinlock_release(&wait_lk);
            return -1;
        }

        // 睡眠等待
        proc_sleep(p, &wait_lk);
    }
}

/*
    进程等待sleep_space对应的资源, 进入睡眠状态
    RUNNING -> SLEEPING
*/
void proc_sleep(void *chan, spinlock_t *lk)
{
    proc_t *p = myproc();

    // 逻辑修正：
    // 必须持有 p->lk 才能修改 p->state 并调用 sched。
    // 如果传入的锁 lk 不是 p->lk，说明我们还没持有 p->lk，需要获取。
    // 如果传入的锁就是 p->lk，说明我们已经持有了，不需要再次获取。
    if (lk != &p->lk)
    {
        spinlock_acquire(&p->lk);
        spinlock_release(lk);
    }

    // 修改状态
    p->sleep_space = chan;
    p->state = SLEEPING;

    // 调度 (proc_sched 要求调用者持有 p->lk)
    proc_sched();

    // 醒来后清理
    p->sleep_space = NULL;

    // 恢复锁的状态
    // 如果之前释放了 lk 并获取了 p->lk，现在要反过来
    if (lk != &p->lk)
    {
        spinlock_release(&p->lk);
        spinlock_acquire(lk);
    }
}

/*
    唤醒所有等待sleep_space的进程
    SLEEPING -> RUNNABLE
*/
void proc_wakeup(void *chan)
{
    for (int i = 0; i < N_PROC; i++)
    {
        proc_t *p = &proc_list[i];
        if (p != myproc())
        {
            spinlock_acquire(&p->lk);
            if (p->state == SLEEPING && p->sleep_space == chan)
            {
                p->state = RUNNABLE;
            }
            spinlock_release(&p->lk);
        }
    }
}

/*
    用户进程切换到调度器
    tips: 调用者保证持有当前进程的锁
*/
// 进程主动放弃 CPU (调度器 -> 进程 -> 调度器)
void proc_sched()
{
    int intena;
    proc_t *p = myproc();

    if (!spinlock_holding(&p->lk))
        panic("proc_sched p->lk");
    if (mycpu()->noff != 1)
        panic("proc_sched locks");
    if (p->state == RUNNING)
        panic("proc_sched running");
    if (intr_get())
        panic("proc_sched interruptible");

    intena = mycpu()->origin;
    swtch(&p->ctx, &mycpu()->ctx); // 切回 scheduler
    mycpu()->origin = intena;
}

/*
    调度器
    RUNNABLE->RUNNING
*/
void proc_scheduler()
{
    cpu_t *c = mycpu();
    c->proc = NULL;

    while (1)
    {
        intr_on(); // 调度器空闲时打开中断，响应时钟

        for (int i = 0; i < N_PROC; i++)
        {
            proc_t *p = &proc_list[i];
            spinlock_acquire(&p->lk);

            if (p->state == RUNNABLE)
            {
                p->state = RUNNING;
                c->proc = p;

                // 切换到进程的内核上下文 (会跳转到 proc_return -> trap_user_return -> 用户态)
                swtch(&c->ctx, &p->ctx);

                // 进程被切换回来 (比如调用了 yield 或 sleep)
                c->proc = NULL;
            }
            spinlock_release(&p->lk);
        }
    }
}