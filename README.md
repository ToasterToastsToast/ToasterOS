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

**Contributors**: 
- [ToasterToasterToast](https://github.com/ToasterToastsToast) - 前三个任务
- [syqwq](https://github.com/syqwq-OMG) - 后两个任务

--- 
## 任务1：用户态和内核态的数据迁移

在lab-4中，用户程序输出的hello world硬编码在陷阱处理函数里。在这个task中，我们要实现用户与内核的数据迁移，让用户既可以从内核读取内容，也可以把数据交给内核输出。`uvm_copyin()`从用户态读数据到内核，`uvm_copyout()`从内核态写数据给用户，`uvm_copyin_str()`从用户态读取字符串到内核。相应地，我们有三个系统调用函数用于测试此功能。`sys_copyout()`将硬编码整数数组传输给`uvm_copyout()`，测试内核能否写入用户内存。`sys_copyin()`获取用户程序提供的源地址,调用`uvm_copyin()`读取整数数组并打印。`sys_copyinstr()`验证 `uvm_copyin_str` 函数能否安全地从用户空间拷贝一个字符串("hello, world")到内核。

成功截图：
![success1](lab-manual/image.png)

可以通过寄存器完成内核和用户的值传递，但是内核不能直接读取用户的指针，因为用户传入的地址空间是基于用户页表的, 但是进入内核后使用的是内核页表。用户看到的连续虚拟内存，可能分散在不同的物理页上。解决这个问题需要手动查询用户页表, 找到虚拟地址对应的物理地址, 之后再做数据迁移。

`uvm_copyout`接收页表，目标地址，原地址和数组长度。使用了一个while循环，每次处理一页内容，防止物理地址页面分散。程序根据虚拟地址和页表，在PTE查找物理页并合成物理地址。程序体现了虚拟地址和物理地址有不同的页号和相同的页内偏移。这一过程还需检查PTE有效且可写。最后，在这个操作系统实现中，物理内存被线性映射到内核地址空间的一段区域。因此，内核可以直接使用物理地址（或者 PA + 某个固定偏移）来访问数据。因此我们用memmove来拷贝。

`uvm_copyin`是该过程的反向。`uvm_copyin_str`是在遇到`\0`时return，整体上类似。

## 任务2：堆的手动管理与栈的自动管理

实现动态增长的栈和堆。

### 堆

实现处理堆的系统调用和用户函数。

其实花了很久的是搞明白输出调试信息，`sys_brk` 作为系统调用入口，首先通过读取用户传入的地址参数，结合当前进程的 `heap_top` 来区分查询、增长、收缩或保持不变四类事件。正是依赖这一参数对比机制，`sys_brk` 能够自动判断事件类型，并据此输出对应的调试信息，包括请求地址、旧堆顶、变化的字节数及对齐后的页区间。

在实际的内存操作中，`sys_brk` 调用 `uvm_heap_grow` 或 `uvm_heap_ungrow` 执行页级的分配与回收。流程是页对齐 → 逐页建立/删除 PTE → 更新堆顶页对齐保证了堆区变化不会破坏页表结构，而逐页操作确保系统能够严格控制映射边界。实际上这些是策略层，确定需要新/解除映射的页对齐虚拟地址范围，vm_mappages/vm_unmappages 才是机制层负责操作页表。完成内存操作后，`sys_brk` 再通过 `vm_print` 输出完整页表，使堆区的变化在系统层面可视化，从而构成一个功能完整且可调试的堆管理体系。



![Alt text](lab-manual/image-2.png)
![Alt text](lab-manual/image-1.png)
### 栈
`uvm_ustack_grow`采用按需分配策略。由于栈是从高地址向低地址增长，当程序访问到尚未映射的栈地址，触发缺页异常时，内核介入。uvm_ustack_grow 会确定导致异常的地址所在页，计算出需要映射的新页范围，然后分配物理页并建立映射。这种机制确保了栈空间能够高效、自动地向下扩展. 调试内容写的稍微详细了一些。
![Alt text](lab-manual/image-3.png)

### 边界
在堆和栈的生长中，分别检查new_heap_top > MMAP_BEGIN和new_ustack_top_va < MMAP_END保障堆栈的隔离。对于越界时的处理有不同的选择。
堆越界被视为系统完整性威胁，需触发`panic`。因其向高地址生长，若突破`MMAP_BEGIN`意味着用户进程可能正在入侵内核空间，将破坏整个系统的安全隔离，必须立即中止内核运行。
栈越界则作为进程级错误处理，应避免`panic`。栈向低地址生长时，若扩展失败（如触及`MMAP_END`或物理内存不足），这通常仅影响当前进程。内核应通过返回错误或发送`SIGSEGV`信号终止该进程，从而保持系统整体稳定，使其他进程不受影响。

## 任务3
在`mmap.c`实现的离散内存资源管理器中，核心流程围绕一个静态预分配的节点池展开。初始化时，所有节点被串联成一个空闲链表。当调用`mmap_region_alloc()`时，系统会在自旋锁保护下从链表头部取出节点，若资源耗尽则触发`panic`；反之，`mmap_region_free()`则通过地址计算验证节点合法性后，将其安全插回链表头部。整个流程通过头部操作保证O(1)效率，并依靠自旋锁确保多核并发下的数据一致性。其设计体现了内核资源管理的典型模式：以固定资源池避免动态分配开销，通过锁机制保障线程安全。
一开始列表用的头插，结果不是很有序，改尾插似乎好了。
![Alt text](lab-manual/image.png)
![Alt text](lab-manual/image-4.png)
## 0xff. references
- [labs assignments](https://gitee.com/xu-ke-123/ecnu-oslab-2025-task)
- [riscv简单常用汇编指令xv6](https://blog.csdn.net/surfaceyan/article/details/135030477)
