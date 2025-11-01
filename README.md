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
- [ToasterToasterToast](https://github.com/ToasterToastsToast) - 主要完成串口中断的实现，以及一些串口中断和时钟中断的测试代码
- [syqwq](https://github.com/syqwq-OMG) - 主要完成时钟中断，以及内核态trap处理的核心逻辑

--- 
## 0x01 串口中断

#### 1
使用`medeleg = 0xffff;`设置`medeleg`的低16位全为1，表示将编号0~15的所有异常委托给S模式处理。

#### 2
我们修改了`uart.c`，添加了一个可以处理换行与退格的函数`uart_putc_sync_ext(int)`，逻辑是：
- 普通字符，直接调用uart_putc_sync()`发送。
- 如果发送的字符是换行符，则要输出`\r`和`\n`，换行回车。
- 如果字符是`backspace`或者`delete`，则光标回退，然后输出一个空格来覆盖字符，然后回退。
目前不支持跨行删除（即删除到一行开头后继续删除就回到上一行末尾），完整的跨行删除功能需要对终端状态有完整的掌握，包括光标位置、屏幕内容和滚动状态。这较为复杂且也不是这个lab的重点。

#### 3
检查uart引发的中断。由`trap_kernel_handler()`负责陷阱，检测到外设中断则安排给`external_interrupt_handler()`，后者检查如果是串口中断则调用对应的处理逻辑，`uart_intr()`。这个函数尝试读取字符并回显。

#### 4
完善`uart_intr`。这里主要指改调用更完善的`uart_putc_sync_ext(c);`。另一方面我们顺便模仿xv6准备了一些异步发送的代码，尽管在这个实验是**不必要**的，因为测试代码完全是同步发送。

#### 测试
测试函数`echo_test`实现了一个简单的输入回显功能。程序首先打印提示信息，然后进入无限循环不断检查UART输入。当检测到有字符输入时（`uart_getc_sync`返回非-1值），立即通过`uart_putc_sync_ext`将字符回显到输出设备。这是一个**同步阻塞式**的回显测试，字符的读取和输出都是直接操作硬件完成的，不依赖缓冲区或中断处理机制。

### 0x02 时钟中断

首先，在一切的开始，我们要让 cpu 可以处理时钟中断，因此需要修改 `start()` 函数，在进入 `main()` 之前，设置 `medeleg` 和 `mideleg`，将时钟中断委托给 S 模式处理，然后进行时钟的初始化，也就是 `timer_init()`。

接着，需要实现 S-mode 下的系统时钟，本质上是一个上锁的全局变量 `ticks`，每当时钟中断发生时，`ticks` 自增。

- 时钟的创建就是：1、创建一个守护时钟的锁 2、初始化 `ticks = 0`
- 更新时钟： 1、先获取锁，上锁 2、`ticks++` （为了测试，这里要加输出） 3、解锁 
- 获取时钟的值： 1、获取锁，上锁 2、读取 `ticks` 的值 3、解锁

最后，要处理 `trap_kernel_handler()` 中的时钟中断。检测到时钟中断后，调用 `timer_update()` 来更新时钟，处理完之后，恢复标志位，用于下一次可能的中断。

还有，`main()` 函数中要调用陷阱的初始化处理函数。

## 0xff. references
- [labs assignments](https://gitee.com/xu-ke-123/ecnu-oslab-2025-task)
- [riscv简单常用汇编指令xv6](https://blog.csdn.net/surfaceyan/article/details/135030477)
