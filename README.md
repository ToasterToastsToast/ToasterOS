# ToasterOS
```
            xxxxxxxxxxxxxxxxxxxxxx                                                                                             
       xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx                                                                                        
    xxxxxx x xx x xxx xx  xxxx xxxxxxxxx                                                                                       
 xxx                                xxxxx                                                                                      
 x                                    xxxxx                                    xx                      xxxx            xxxx    
x                                      xxxxx          xxxx xxxxxxx            xxxxxx      xxxx     xxxxxxx   xxxxxx  xxxxxxxx  
x           @@@@       @@@@            xxxx      xxxxxxxxxxx                 xx   xxx   xxxxxx  xxxxxxxx    xxxxx xx xxx  xxx  
x           @  @       @  @           xxxx  xxxxxxxxxx xx        xxxxxxx     xx    xxx  xx           xx     x        xx  xxxx  
x           @  @       @  @          xxx             xxxx   xxxxxxxxxxxxxxx  xxxxxxxxx  xxxxxx      xxx    xxxxxxxxx  xxxxxx   
xx          @@@@       @@@@        xxxx              xxx    x xx       xxxx  xxxxxxxxx  xxxxxxx     xxx    xxxxxxx    xxxx     
  xxx                           xxxxxx               x xx   xxxx          xx xx     xx      xxxxxx  xxx    xx         xx xxxx  
      xxx \ \             \ \   xxxxx                x xx   xxx          xxx xx     xx         xx   xxx    xxx        xx    xxx
        x                       xx xx                xxxx   xxxxxx     xxxx  xx     xx    xxxxxxx    xx     xxxxxxxx  xx      x
         x                      xxxxx                xxxx      xxxxxxxxxxx    x      x  xxxx                          xx       
         x                      xxxxx                xxxx        x xxxx                                                 x      
         x     xxx       xx     xxxxx                 xx              xxx                    xxxxxxxxxxx                       
         x   xxx          xxxx  xxxxx                  x            xxxxxxxxxxxxx          xxxxxx   xxxxx                      
         x  xx x         xx     xxxxx                             xx x  xxxx    xxxx       xxxx        xx                      
         x     xxx     xxx      xxxxx                             x xxxx          xxx       xx                                 
         x       xxxxxx         xxxxxx                            xxx              xxx       xxxxxxxxxx                        
         x                      xxxxxx                             xx               xx           xx xxxxxx                     
         x                      xxxxxx                             xxx             xxx               xxxxx                     
         x                      xxxxxx                             xxxxxx         xx x                 xxx                     
         x                      xxxx x                              xxxxxxxxxx  xx  x       xxx     xxxxx                      
         x                      xxxx x                                 x xxxxxxxxxxx         xxxxxxxxxxx                       
         x                     xxxxx x                                                                                         
         x                     xxxxx x                                                                                         
         xxxxxxxxxxxxxxxx xx x x  x                                                                                            
                                  x                                                                                            ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
```
ECNU Operating System 2025 Fall Final Project 

Contributors: 
- [ToasterToasterToast](https://github.com/ToasterToastsToast) - 主要完成串口中断的实现，以及一些串口中断和时钟中断的测试代码
- [syqwq](https://github.com/syqwq-OMG) - 主要完成时钟中断，以及内核态trap处理的核心逻辑

--- 

## 1. 实验概述

核心目标：
本实验标志着操作系统从“裸机控制”阶段进入“进程管理”阶段。我们需要手动构建第一个用户进程（`proczero`），使其在用户态运行，并通过系统调用向内核发送请求，内核接收后打印 `hello world`。

## 2. 核心任务流程

本实验需要修改三个核心文件：

1.  `src/kernel/mem/kvm.c`：配置内核页表，建立用户陷入内核的“桥梁”。
2.  `src/kernel/proc/proc.c`：手工制造第一个进程的“肉身”（代码、栈、页表、上下文）。
3.  `src/kernel/trap/trap_user.c`：处理用户态发出的系统调用。

-----

### 任务一：完善内核页表映射 (`src/kernel/mem/kvm.c`)

为了让用户进程在发生 Trap 时能顺利切换到内核代码执行，内核页表必须包含特定的映射。

实现原理：

  * Trampoline：一段特殊的汇编代码，用于在用户态和内核态之间切换页表和寄存器。它必须映射在用户页表和内核页表的相同虚拟地址处（即 `TRAMPOLINE` 宏定义的最高地址）。
  * 内核栈（Kstack）：每个进程在内核态执行时（例如处理系统调用时）都需要一个独立的栈。

具体实现 (`kvm_init` 函数)：
在 `kvm_init` 函数末尾（`printf` 之前），你需要添加以下映射：

1.  映射 Trampoline：

      * 虚拟地址：`TRAMPOLINE` (定义在 `proc/type.h`, 通常是虚拟地址空间的最高页)。
      * 物理地址：`(uint64)trampoline` (汇编符号的地址)。
      * 权限：`PTE_R | PTE_X` (读+执行)。
      * 目的：确保陷入内核时，即便还没切换页表，PC 指针也能访问到这段代码。

2.  映射进程 0 的内核栈：

      * 动作：分配一个物理页。
      * 虚拟地址：`KSTACK_VA(0)`。
      * 物理地址：分配到的物理页地址。
      * 权限：`PTE_R | PTE_W` (读+写)。
      * 目的：`proczero` 陷入内核后，需要用这个栈来保存上下文和运行 C 代码。
-----

### 任务二：构建第一个用户进程 (`src/kernel/proc/proc.c`)

接着，我们需要手动填充 `struct proc` 结构体，假装这个进程已经“运行过”并“暂停”了，以便内核调度器可以通过 `swtch` 将其“恢复”执行。

核心函数：`proc_make_first()`
主要的步骤是：

1.  初始化标识：设置 `pid = 0`。
2.  分配 Trapframe：
      * 调用 `pmem_alloc` 分配一个物理页作为 `tf`。
      * Trapframe 是用户态和内核态切换时保存通用寄存器的地方。
3.  初始化用户页表 (`proc_pgtbl_init`)：
      * 分配根页表。
      * 映射 Trampoline：`VA=TRAMPOLINE`, `PA=trampoline`, `Perm=R|X`。注意：此处不需要 PTE\_U，因为 trampoline 代码运行在 S-mode 下（但在切换页表前）。*(注：在 ToasterOS 的设计中，trampoline 是为了进入内核，通常不需要用户直接访问，但必须在用户页表中存在)*。
      * 映射 Trapframe：`VA=TRAPFRAME`, `PA=tf`的物理地址, `Perm=R|W`。这允许内核在刚陷入时保存用户寄存器。
4.  加载用户程序 (initcode)：
      * 分配物理页用于存放代码。
      * 将 `initcode` 数组（即编译好的用户程序二进制）通过 `memmove` 拷贝到该物理页。
      * 映射代码段：`VA=PGSIZE`, `Perm=R|W|X|U`。必须加 PTE\_U，否则用户态无法访问。
5.  分配并映射用户栈 (Ustack)：
      * 分配物理页。
      * 映射到 `VA=USTACK`，`Perm=R|W|U`。
6.  伪造上下文（关键步骤）：
      * Trapframe 设置（用于返回用户态）：
          * `epc=PGSIZE` (即用户程序入口)。
          * `sp= USTACK + PGSIZE` (用户栈顶)。
      * Context 设置（用于内核线程切换）：
          * `ra=(uint64)trap_user_return`。当 `swtch` 切换到这个进程时，它会“返回”到 `trap_user_return` 函数，从而启动进入用户态的流程。
          * `sp=kstack + PGSIZE` (内核栈顶)。
7.  执行切换：
      * 调用 `swtch(&mycpu()->ctx, &proczero.ctx)`。内核线程暂停，`proczero` 开始执行。

-----

### 任务三：处理用户态陷入 (`src/kernel/trap/trap_user.c`)

当用户程序执行 `syscall(SYS_helloworld)` 时，会触发 `ecall` 指令，CPU 跳转到 `stvec` 指向的地址（即 `user_vector`），最终调用 C 函数 `trap_user_handler`。

1. `trap_user_handler()` 实现逻辑：

  * 环境检查：
      * 修改 `stvec` 指向 `kernel_vector`（防止在内核态处理 Trap 时发生递归 Trap）。
      * 保存用户态的 `sepc` 到 `p->tf->user_to_kern_epc`。
  * 识别 Trap 原因：
      * 读取 `scause`。如果是异常且 ID 为 8 (`Environment call from U-mode`)，则为系统调用。
  * 处理系统调用：
      * 从 `p->tf->a7` 获取系统调用号。
      * 如果 `sys_num == SYS_helloworld`，则调用 `printf("proczero: hello world!\n")`。
      * 重要：PC + 4。系统调用是异常，处理完后必须手动将 `epc` 加 4，否则返回用户态后会无限执行那条 `ecall` 指令。
  * 处理中断：
      * 如果是时钟中断或外设中断，调用对应的 handler。
  * 返回：调用 `trap_user_return()`。

2. `trap_user_return()` 实现逻辑：

这是从内核态返回用户态的发射台。

  * 关中断：`intr_off()`。
  * 重设 Trap 入口：将 `stvec` 指向 `user_vector`（trampoline 中的汇编标签）。
  * 填充 Trapframe：
      * 填入内核的信息，以便下次从用户态陷入时使用：
      * `kernel_satp` (内核页表)
      * `kernel_sp` (进程的内核栈顶)
      * `trap_handler` (处理函数地址)
  * 设置 Sstatus：
      * 清除 `SPP` 位（表示下一个特权级是 User Mode）。
      * 设置 `SPIE` 位（开启用户态中断）。
  * 设置 Sepc：
      * 写入 `w_sepc(p->tf->user_to_kern_epc)`（即之前保存并可能+4过的 PC）。
  * 执行汇编切换：
      * 传递 `TRAPFRAME` 虚拟地址和用户页表 `satp` 给 `user_return` 函数（定义在 `trampoline.S`）。
      * 该汇编函数会恢复用户通用寄存器，切换页表，最后执行 `sret`。

-----

## 3. 完整的执行流（The Big Picture）
完整的执行流程如下：

1.  Boot: `entry.S` -\> `start.c` -\> `main.c`。
2.  Init: `kvm_init` (映射 trampoline/kstack) -\> `trap_kernel_init`。
3.  Process Creation: `main` 调用 `proc_make_first`。
      * 构建 `proczero` 的内存布局。
      * 伪造上下文，使 `ra` 指向 `trap_user_return`。
4.  Context Switch: `swtch` 发生。CPU 从 `main` 的栈切换到 `proczero` 的内核栈。
5.  Kernel Exit: 伪造的 `ra` 导致 CPU 跳转到 `trap_user_return`。
6.  To User Mode: `trap_user_return` -\> `user_return` (asm) -\> `sret` -\> 进入用户态。
7.  User Execution: 执行 `initcode.c` 编译出的二进制代码。
8.  Syscall: 用户程序执行 `syscall(SYS_helloworld)` -\> `ecall` 指令。
9.  Trap Entry: 硬件跳转到 `user_vector` (trampoline)。
      * 保存用户寄存器到 `trapframe`。
      * 切换到内核页表和内核栈。
      * 跳转到 `trap_user_handler`。
10. Handle: 内核识别出 `SYS_helloworld`，打印字符串。
11. Return: `epc += 4` -\> `trap_user_return` -\> ... -\> 回到用户代码下一行。

-----

## 4. 验证与测试
最后的输出是

```
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic
cpu 0 is booting!
pmem_init complete.
Kernel allocable pages: 1024
User allocable pages: 31737
kernel page table created successfully.
proc_make_first: switching to proczero...
proczero: hello world!
proczero: hello world!
[U] tick=20
[U] tick=40
[U] tick=60
[U] tick=80
[U] tick=100
```
