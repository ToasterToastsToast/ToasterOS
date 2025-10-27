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
- [ToasterToasterToast](https://github.com/ToasterToastsToast) 实现物理内存、审阅虚拟内存
- [syqwq](https://github.com/syqwq-OMG) 审阅物理内存、实现虚拟内存

--- 
## lab-2
### 物理内存

>    物理内存管理的核心是空闲页面组成的单链表:
>
    kern_reagion和user_region的list_head字段是链表的头节点

    物理页的分配就是将list_head->next移出链表

    物理页的回收就是将node插入list_head->next

#### pmem_init
实现不难，根据给定的结构体来填空。

`kernel.ld`是一个 RISC-V 内核链接脚本，它控制了内核各段在内存中的布局，并定义了一些符号供程序在运行时动态读取。具体而言，通过 `PROVIDE(symbol = .)`，C 代码在运行时可以用`extern`获取这些地址。这体现在type.h中获取了 `KERNEL_DATA`等。注意到使用了字符数组的形式引用，这是因为`char*`作为地址便于算术，因为是按照字节计算。

内存大概分为不可分配的、给内核的可分配物理页和给用户空间的可分配物理页三块区域。可分配空间从`ALLOC_BEGIN`开始，而剩下的地址划分都依靠加减法。

所有可分配的页面构成链表，`list_head`是头节点，初始化的时候要一并把这个链表构造好，代码使用了尾插法。

#### pmem_alloc
测试代码对这些函数的调用用一个bool参数确定是内核还是用户。

拿出链表头来分配，并且要把分配出的页面全部置0. 这些操作需要在锁内进行。

需要更新`allocable`。

#### pmem_free
类似的内容。使用头插把空闲页面放回列表。
理论上头插和尾插都无所谓。

### 虚拟内存
很大程度上，实现和物理内存类似。
依然是三级页表，页表本身存储在物理页中，需要实现页表的增加、删除条目以及walk。 在初始化的时候要构建对于一些底层硬件的的对等映射。

首先，对于获取pte条目，使用循环和二进制掩码来获取每一层的索引，直到最后一级页表返回pte，注意pte存储的是页码的映射，对于页内偏移不用考虑。注意途中要判断是否出现缺页异常。

然后，对于 `vm_mappages` 函数，循环遍历从va开始，大小为size的地址区间，每次步进 4KB。 在循环中，为每一个虚拟页调用上一个 `vm_getpte` 来找到或创建对应的PTE。最后，将PTE的内容设置为对应的物理页号（从pa计算得出）和权限位 `perm`。

`kvm_init` 函数中，先调用 `pmem_alloc()` 为内核创建一个顶级的页表 `kernel_pgtbl`。然后，调用 `vm_mappages`，为所有硬件寄存器区域和 0x80000000 到 0x80000000 + 128MB 的物理内存区域创建对等映射。
最后，调用 `kvm_inithart` (每个核心都要调用): 将 `kernel_pgtbl` 的地址写入 `satp` 寄存器，为当前核心开启MMU地址翻译。

## 0xff. references
- [labs assignments](https://gitee.com/xu-ke-123/ecnu-oslab-2025-task)
- [riscv简单常用汇编指令xv6](https://blog.csdn.net/surfaceyan/article/details/135030477)