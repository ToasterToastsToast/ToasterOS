# Lab-9: 文件系统之文件管理与全系统整合

**ToasterOS**

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

本实验的主要工作包含以下三个维度：

1. **文件系统完善**：实现硬链接、反向路径解析、文件抽象 (`file_t`) 及设备驱动框架。
2. **进程与FS交互**：为进程引入当前工作目录 (`cwd`) 和打开文件表，支持相对路径。
3. **全系统整合**：实现 `exec` 系统调用，解析 ELF Header，加载段，构建用户栈。

---


## 具体实现细节

### 1. 准备工作

为了支撑后续的高级功能，我们首先对基础模块进行了改造：

* **内存统计**：在 `pmem.c` 中实现了 `pmem_stat`，便于观察系统运行时的内存消耗。
* **权限精细化**：修改 `uvm_heap_grow`，支持传入 `flag` 参数。这是为了 `exec` 加载 ELF 时，能够区分代码段（R|X）和数据段（R|W）的权限，增强安全性。
* **行缓冲控制台**：在 `console.c` 中实现了行缓冲机制。用户输入的字符会先暂存在缓冲区，直到按下回车才被系统读取，支持了退格与基本的行编辑功能。

### 2. 文件系统进阶

我们在 Lab-8 的基础上，构建了更高层的文件抽象。

#### 2.1 目录项增强 (`dentry.c`)

* **逆向路径解析 (`inode_to_path`)**：实现了从 Inode 反查绝对路径的功能。核心逻辑是利用 `..` 目录项不断回溯父节点，直到根目录，主要用于 `getcwd`。
* **硬链接 (`path_link/unlink`)**：
* **Link**: 不复制文件内容，而是在新目录下创建一个指向已有 Inode 的 dentry，并增加 Inode 的 `nlink` 计数。
* **Unlink**: 删除 dentry 并减少 `nlink`。当 `nlink` 归零且无进程引用时，才真正释放磁盘块。



#### 2.2 文件的抽象 (`fs.c`)

引入 `file_t` 结构体，屏蔽了底层资源的差异。

* **结构定义**：包含 `type` (INODE/DEVICE), `ref` (引用计数), `readable/writable` (权限), `offset` (读写指针)。
* **统一接口**：`file_read` 和 `file_write` 根据文件类型进行分发：
* 若是普通文件/目录，调用底层 `inode_read/write`。
* 若是设备文件，调用 `device_read/write`。



#### 2.3 设备驱动框架 (`device.c`)

实现了“设备即文件”的映射。我们定义了主设备号 (Major Device Number) 来区分不同设备：

* **CONSOLE (1)**: 对应 `stdin` (读) 和 `stdout/stderr` (写)。
* **NULL (4)**: 黑洞设备，写被丢弃，读返回 0。
* **ZERO (5)**: 零设备，源源不断产生 `\0` 数据。
* **GPT0 (6)**: 一个简单的交互式测试设备。

### 3. 进程与文件系统的融合

进程 (`proc_t`) 不再是孤独的计算单元，它拥有了对文件系统的“感知”。

* **CWD (Current Working Directory)**：
* 在 `proc_t` 中增加 `inode_t *cwd`。
* 修改 `__path_to_inode`，支持相对路径解析：若路径不以 `/` 开头，则从 `cwd` 开始查找。
* 实现了 `sys_chdir` 切换工作目录。


* **打开文件表 (Open Files)**：
* 在 `proc_t` 中增加 `file_t *open_file[N_FILE]`。
* **Fork**: 子进程通过 `file_dup` 继承父进程的所有打开文件（引用计数+1），实现资源共享。
* **Exit**: 进程退出时，自动关闭所有打开文件并释放 `cwd`。


* **Initcode 改造**：PID 1 进程 (`initcode`) 在启动时，手动构造了 `stdin`, `stdout`, `stderr` 三个标准文件描述符，确保后续所有子进程天生具备 I/O 能力。

### 4. Exec 系统调用 (`exec.c`)

这是本实验最复杂的函数，它让静态的磁盘文件变成了动态的运行进程。`proc_exec` 的执行流如下：

1. **解析路径**：调用 `path_to_inode` 找到可执行文件。
2. **检查头信息**：读取 ELF Header，验证魔数 (`\x7fELF`)。
3. **构建新页表**：分配全新的用户页表，防止破坏旧地址空间。
4. **加载段 (Segments)**：
* 遍历 Program Headers。
* 对于 `LOAD` 类型的段，分配物理内存，将文件内容读取并映射到虚拟地址（利用 `uvm_heap_grow` 设置正确权限）。


5. **构建栈 (Stack)**：
* 分配用户栈页。
* **参数压栈**：将 `argv` 字符串数组拷贝到栈顶，并构造 `argv[]` 指针数组，确保 `main(argc, argv)` 能正确获取参数。


6. **替换上下文**：
* 提交新的页表。
* 修改 `trapframe`：`epc` 设为 ELF 入口地址，`sp` 设为新栈顶，`a0/a1` 设为参数。


7. **清理现场**：释放旧的页表和 Trapframe，释放 ELF 文件的 Inode。

