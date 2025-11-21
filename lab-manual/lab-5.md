# LAB-5: 系统调用流程建立 + 用户态虚拟内存管理

**前言**

在lab-4中, 我们初步实现了第一个用户进程`proczero`

它通过`sys_helloworld`系统调用, 利用内核的系统服务发出了"第一声啼哭"

本次实验的核心目的是完善和发展`proczero`, 具体包括两个方面:

- 赋予`proczero`更强的内存掌控能力, 包括堆、栈、离散映射三个部分

- 赋予`proczero`完善的请求服务能力, 建立真正的系统调用流程

## 代码组织结构

```
ECNU-OSLAB-2025-TASK
├── LICENSE        开源协议
├── .vscode        配置了可视化调试环境
├── registers.xml  配置了可视化调试环境
├── .gdbinit.tmp-riscv xv6自带的调试配置
├── common.mk      Makefile中一些工具链的定义
├── Makefile       编译运行整个项目 (CHANGE, 新增目录syscall)
├── kernel.ld      定义了内核程序在链接时的布局
├── pictures       README使用的图片目录 (CHANGE, 日常更新)
├── README.md      实验指导书 (CHANGE, 日常更新)
└── src            源码
    ├── kernel     内核源码
    │   ├── arch   RISC-V相关
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── boot   机器启动
    │   │   ├── entry.S
    │   │   └── start.c
    │   ├── lock   锁机制
    │   │   ├── spinlock.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── lib    常用库
    │   │   ├── cpu.c
    │   │   ├── print.c
    │   │   ├── uart.c
    │   │   ├── utils.c
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h
    │   ├── mem    内存模块
    │   │   ├── pmem.c
    │   │   ├── kvm.c
    │   │   ├── uvm.c (TODO, 用户态虚拟内存管理主体)
    │   │   ├── mmap.c (TODO, mmap节点资源仓库)
    │   │   ├── method.h (CHANGE, 日常更新)
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE, 日常更新)
    │   ├── trap   陷阱模块
    │   │   ├── plic.c
    │   │   ├── timer.c
    │   │   ├── trap_kernel.c
    │   │   ├── trap_user.c (TODO, 系统调用处理 + pagefault处理)
    │   │   ├── trap.S
    │   │   ├── trampoline.S
    │   │   ├── method.h
    │   │   ├── mod.h (CHANGE, 日常更新)
    │   │   └── type.h
    │   ├── proc   进程模块
    │   │   ├── proc.c (TODO, proczero->mmap初始化)
    │   │   ├── swtch.S
    │   │   ├── method.h
    │   │   ├── mod.h
    │   │   └── type.h (CHANGE, 进程结构体里新增mmap字段)
    │   ├── syscall 系统调用模块
    │   │   ├── syscall.c (NEW, 系统调用通用逻辑)
    │   │   ├── sysfunc.c (TODO, 各个系统调用的处理逻辑) 
    │   │   ├── method.h (NEW)
    │   │   ├── mod.h (NEW)
    │   │   └── type.h (NEW)
    │   └── main.c
    └── user       用户程序
        ├── initcode.c (CHANGE, 按照测试需求来设置)
        ├── sys.h
        ├── syscall_arch.h
        └── syscall_num.h (CHANGE, 日常更新)
```

**标记说明**

**NEW**: 新增源文件, 直接拷贝即可, 无需修改

**CHANGE**: 旧的源文件发生了更新, 直接拷贝即可, 无需修改

**TODO**: 你需要实现新功能 / 你需要完善旧功能

## 任务1：用户态和内核态的数据迁移

回忆一下上个实验的`sys_helloworld`系统调用, 它的作用是让内核输出`"hello world"`

一个明显的问题: 用户态程序无法向内核程序传递参数, 导致系统服务非常僵硬和受限

我们可以从普通函数的参数传递获得启示, 传参方法无非两种:

- 直接传递值: `add(int a, int b)`, 本质是将参数值放到寄存器里

- 基于地址做间接传递: `strcmp(char *s1, char *s2, int len)`, 本质是将地址放到寄存器里

通过阅读`user/syscall_arch.h`, 可以发现系统调用编号默认放在a7寄存器, a0到a5寄存器则是存放参数

```c
static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a5 __asm__("a5") = f;
    __asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5))
}
```

内核可以通过访问`proc->tf->ax`直接拿到这些参数 (trapframe实在太好用了~)

- 对于值传递, `arg_uint32`和`arg_uint64`可以很好地完成任务

- 对于地址传递, 必须考虑用户地址空间和内核地址空间不匹配的问题:

**用户传入的地址空间是基于用户页表的, 但是进入内核后使用的是内核页表**

解决这个问题需要手动查询用户页表, 找到虚拟地址对应的物理地址, 之后再做数据迁移

请你完成`kernel/mem/uvm.c`的第一部分, 包括`uvm_copyin`、`uvm_copyout`、`uvm_copyin_str`三个部分

随后, 你需要补全`trap_user_handler`中的系统调用的处理逻辑:

- 调用`syscall`进行分类跳转

- 补全三个具体的处理逻辑 `sys_copyin`、`sys_copyout`、`sys_copyinstr`

- 注意: 这三个系统调用只服务于本次测试, 不是长期保存的系统调用

## 测试1：用户态和内核态的数据迁移

测试逻辑: 

- 用户读取内核中的数组 (1 2 3 4 5)

- 用户将读到的数组传递给内核, 内核收到后打印出来

- 用户将自己的字符串传递给内核, 内核收到后打印出来

```c
// in initcode.c
#include "sys.h"

int main()
{
    int L[5];
    char* s = "hello, world"; 
    syscall(SYS_copyout, L);
    syscall(SYS_copyin, L, 5);
    syscall(SYS_copyinstr, s);
    while(1);
    return 0;
}
```

测试结果见`picture/test-1.png`

## 任务2：堆的手动管理与栈的自动管理

上次实验中, 栈空间被设置为4KB, 堆空间被设置为0KB, 对于非常简单的`initcode.c`是足够的

然而, 现实世界的应用程序需要可以动态增长的栈和堆, 本次实验我们做一个初步的实现

### 堆的管理是手动的

**堆-HEAP**为用户提供了一块连续的大范围内存空间, 它的生长方向的是低地址到高地址

内核给用户程序提供了一个`sys_brk`系统调用, 允许用户改变堆顶的位置

`sys_brk`的效果可以进一步分为:

- 空间增加: old_heap_top < new_heap_top 

- 空间减少: old_heap_top > new_heap_top

- 空间不变: old_heap_top == new_heap_top

- 查询当前栈顶: new_heap_top == 0

涉及内存页面的申请释放、用户页表的修改、`proc->heap_top`的更新

请你完成`sys_brk`、`uvm_heap_grow`、`uvm_heap_ungrow`几个函数

### 栈的管理是自动的

**栈-STACK**为用户的临时变量和函数执行提供了一块连续的内存空间, 它的生长方向是高地址到低地址

用户程序无需显式地管理栈空间, 由内核根据用户需要进行自动管理 (自动的内存申请和映射)

内核不会在进程初始化时直接分配一个很大的栈空间 (默认分配4KB), 而是根据程序运行的需要逐步分配足够大的空间

当用户读或写一块未分配的地址空间时, 会触发**13号异常(Load Page Fault)** / **15号异常(Store/AMO Page Fault)**

我们在`trap_user_handler`里识别这两种异常, 然后调用`uvm_ustack_grow`来处理缺页异常

`uvm_ustack_grow`首先判断发生page fault的地址 (放在stval寄存器) 是否是合理的栈扩展地址

确认合法性后: 申请物理页面、修改用户页表、更新`proc->ustack_npage`

需要提醒的是: 一次可以扩展多个页面, 扩展后不会发生收缩 (和堆的管理不同)

### 边界检查

需要提醒的是: 我们在栈和堆的中间区域里, 划分了一段地址空间作为离散内存空间的区域 (mmap_region)

这块区域的起点地址被定义为`MMAP_BEGIN`, 终点被定义为`MMAP_END` (in `kernl/mem/type.h`)

因此, 栈的生长不应该越过`MMAP_END`, 堆的生长不应该越过`MMAP_BEGIN`

mmap_region的详细介绍放在任务3和和任务4, 这里只需要注意边界检查即可

## 测试2：堆的手动管理与栈的自动管理

**堆的管理**

```c
// in initcode.c
#include "sys.h"

#define PGSIZE 4096

int main()
{
    long long heap_top = 0;
    
    heap_top = syscall(SYS_brk, 0);
    heap_top = syscall(SYS_brk, heap_top + PGSIZE * 9);
    heap_top = syscall(SYS_brk, heap_top);
    heap_top = syscall(SYS_brk, heap_top - PGSIZE * 5);

    while(1);
    return 0;
}
```

你需要在`sys_brk`中增加一些调试性输出

测试结果见`picture/test-2(1)(2).png`

**栈的管理**

函数内定义非static的长数组就能让栈的大小超过4KB

你也可以通过深度函数递归来实现类似的效果 (比如汉诺塔问题)

```c
// in initcode.c
#include "sys.h"

#define PGSIZE 4096

int main()
{
    char tmp[PGSIZE * 4];

    tmp[PGSIZE * 3] = 'h';
    tmp[PGSIZE * 3 + 1] = 'e';
    tmp[PGSIZE * 3 + 2] = 'l';
    tmp[PGSIZE * 3 + 3] = 'l';
    tmp[PGSIZE * 3 + 4] = 'o';
    tmp[PGSIZE * 3 + 5] = '\0';

    syscall(SYS_copyinstr, tmp + PGSIZE * 3);

    tmp[0] = 'w';
    tmp[1] = 'o';
    tmp[2] = 'r';
    tmp[3] = 'l';
    tmp[4] = 'd';
    tmp[5] = '\0';

    syscall(SYS_copyinstr, tmp);

    while (1);
    return 0;
}
```

你需要在`trap_user_handler`中增加一些调试性输出

测试结果见`picture/test-3.png`

## 任务3: mmap_region_node 仓库管理

应用程序有了堆和栈就足够了吗? 应用程序有时需要临时申请一块内存空间, 过一会就释放掉

- 用栈来申请的话无法手动释放 (释放函数里数组占用的空间?)

- 用堆来申请的话可能面临碎片化风险 (堆更适合管理大片逻辑连续的内存空间)

因此, 我们需要设计一种可以动态申请释放的离散内存资源管理方法

直观的想法就是链表结构: 将多个内存资源节点通过链表链接在一起, 在进程结构体里存储表头!

说明: 在真实的操作系统里, 堆、栈、内存映射区的细节和定位与我们这里说的有所区别

结构体 `mmap_region_t` 用于描述一块连续地址空间, 它起始于`begin`, 包括`npages`个页面

进程会记录地址空间中的第一个`mmap_region_t`, 各个资源节点通过`next`指针串联 (构成单链表)

**特别提醒: mma_region_t 描述的是已分配出去的空间, 和2024版本是反过来的!**

```c
/* mmap_region 描述了一个 mmap区域 */
typedef struct mmap_region
{
    uint64 begin;             // 起始地址
    uint32 npages;            // 管理的页面数量
    struct mmap_region *next; // 链表指针
} mmap_region_t;
```

理解这部分后我们继续考虑另一个问题: `mmap_region_t`结构体本身也是一种资源

我们规定OS内核可以提供`N_MMAP`个这样的结构体, 各个进程需要有序获取该资源

为了保证各个进程可以高效和有序地共享这种资源, 我们在`kernel/mem/mmap.c`里维护了一个资源仓库

```c
/* mmap_region_node 是 mmap_region 在仓库里的包装 */
typedef struct mmap_region_node
{
    mmap_region_t mmap;
    struct mmap_region_node *next;
} mmap_region_node_t;


// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;
```

具体来说:

- 首先将 `mmap_region_t` 包装为 `mmap_region_node_t`, 以维护资源仓库的单链表结构

- 然后通过全局的自旋锁 `list_lk` 确保任何时候只有一个进程在获取资源或释放资源

- 提供`mmap_init`、`mmap_region_alloc`、`mmap_region_free`作为资源仓库的对外接口

## 测试3: mmap_region_node 仓库管理

我们先来测试一下, 作为资源仓库, 它能不能在多核竞争的条件下保证资源申请和释放的有序性

```c
// in main.c
volatile static int started = 0;
volatile static bool over_1 = false, over_2 = false;
volatile static bool over_3 = false, over_4 = false;

void* mmap_list[N_MMAP];

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        
        print_init();
        printf("cpu %d is booting!\n", cpuid);
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();
        
        // 初始化 + 初始状态显示
        mmap_init();
        mmap_show_nodelist();
        printf("\n");

        __sync_synchronize();
        started = 1;

        // 申请
        for(int i = 0; i < N_MMAP / 2; i++)
            mmap_list[i] = mmap_region_alloc();
        over_1 = true;

        // 屏障
        while(over_1 == false ||  over_2 == false);

        // 释放
        for(int i = 0; i < N_MMAP / 2; i++)
            mmap_region_free(mmap_list[i]);
        over_3 = true;

        // 屏障
        while (over_3 == false || over_4 == false);

        // 查看结束时的状态
        mmap_show_nodelist();        

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        kvm_inithart();
        trap_kernel_inithart();

        // 申请
        for(int i = N_MMAP / 2; i < N_MMAP; i++)
            mmap_list[i] = mmap_region_alloc();
        over_2 = true;

        // 屏障
        while(over_1 == false || over_2 == false);

        // 释放
        for(int i = N_MMAP / 2; i < N_MMAP; i++)
            mmap_region_free(mmap_list[i]);
        over_4 = true;
    }

    while (1);
}
```

测试结果见`picture/test-4(1)(2).png`

- 第一部分的输出应该是 `node X index = X` (X从0增加到255)

- 第二部分输出应该是两股输出交替 (node从0增加到255, 一股index从255减到128, 另一股index从127减到0)

## 任务4: mmap 与 munmap

资源仓库的建立使得 `mmap_region_t` 结构体的申请和释放更加方便和安全, 服务于mmap和munamp操作

我们以mmap为例, 从系统调用出发, 梳理它的逻辑过程:

- 用户程序发出 `sys_mmap(uint64 begin, uint32 len)` 申请一块内存空间

- 调用`uvm_mmap(begin, len / PGSIZE, PTE_R | PTE_W)`进行具体处理

- `uvm_mmap()`首先创建一个新的 mmap_region_t 用于描述这块新的地址空间

- 随后将将这块 new_mmap_region 插入进程 mmap 链表的合适位置 (保持整体有序)

- 新插入的节点可能和前面的节点相邻, 可能和后面的节点相邻, 也可能同时相邻

- 考虑到仓库里资源受限的问题, 我们应该将相邻的节点进行尽可能的合并 (逻辑较为复杂, 建议你画图分析)

- 我们提供了辅助函数`mmap_merge()`用于帮助你完成这些合并, 你可以研究一下怎么用

- 合并完成后, 进行物理页申请和页表修改的步骤 (这里比较简单)

**注意: 当用户传入的begin=0时, 从头到尾扫描, 找到第一个足够大的空间即可**

**另外: 记得为proc结构体增加mmap字段, 并在proc_make_first函数中增加对应的初始化逻辑**

munmap的整体流程于mmap相近, 你应该具备举一反三的能力, 这里不做详细介绍

## 测试4: mmap 与 munmap

我们给出了测试用例用于检测uvm_mmap()和uvm_munmap()中可能的遗漏和错误

请你理解它在测试哪些情况, 以及预期的输出是什么样的

当然, 你应该补充更多测试用例, 以确保实现的完备性

```c
// in initcode.c
#include "sys.h"

// 与内核保持一致
#define VA_MAX       (1ul << 38)
#define PGSIZE       4096
#define MMAP_END     (VA_MAX - (16 * 256 + 2) * PGSIZE)
#define MMAP_BEGIN   (MMAP_END - 64 * 256 * PGSIZE)

int main()
{
    // 建议画图理解这些地址和长度的含义

    // sys_mmap 测试 
    syscall(SYS_mmap, MMAP_BEGIN + 4 * PGSIZE, 3 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 10 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 2 * PGSIZE,  2 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 12 * PGSIZE, 1 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN + 7 * PGSIZE, 3 * PGSIZE);
    syscall(SYS_mmap, MMAP_BEGIN, 2 * PGSIZE);
    syscall(SYS_mmap, 0, 10 * PGSIZE);

    // sys_munmap 测试
    syscall(SYS_munmap, MMAP_BEGIN + 10 * PGSIZE, 5 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN, 10 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 17 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 15 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 19 * PGSIZE, 2 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 22 * PGSIZE, 1 * PGSIZE);
    syscall(SYS_munmap, MMAP_BEGIN + 21 * PGSIZE, 1 * PGSIZE);

    while(1);
    return 0;
}
```

请你在`sys_mmap()`和`sys_munmap()`中增加提示性输出

```c
    proc_t *p = myproc();
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");
```

测试结果见`picture/test-5(1)(2)(3)(4)(5)(6)(7)(8).png`

## 任务5: 页表的复制与销毁

虽然目前我们只有一个进程且永不退出，但是需要为下一个实验做一些准备

你需要完成页表复制和销毁的函数 uvm_destroy_pgtbl() 和 uvm_copy_pgtbl()

需要提醒的是:

- 第一个函数考虑如何使用递归完成

- 第二个函数深入理解用户地址空间各个区域的特点

## 测试5: 页表的复制与销毁

请你参考前4个测试点的设计, 自行决定如何测试页表的复制和销毁

**尾声**

本次实验大概分成以下三个逻辑阶段:

- 首先关注如何实现用户态和内核态的数据传递 (以trapframe为媒介), 并建立规范的系统调用流程

- 随后讨论了用户态内存空间的管理: 堆、栈、mmap_region

- 最后讨论了用户页表整体的复制和销毁, 为下一个实验做准备

经过两次实验的打磨, proczero现在已经比较强大和完善了, 但是似乎有些孤单?

**我们将在下一个实验引入它的子子孙孙, 从单进程走向多进程！**