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

---

## 0x01 核心实现步骤

### 构建 Inode (`inode.c`)

Inode（索引节点）是文件系统的基石。我们的首要任务是打通从“逻辑文件偏移量”到“物理磁盘块号”的映射。

#### 1. 多级索引设计 (Data Organization)

为了支持从小文件到大文件的灵活存储，我们在 `locate_or_add_block` 函数中实现了**三级映射机制**：

* **直接映射 (Direct Mapping)**:
* `index[0] ~ index[9]`：直接存储物理块号。
* 适用：< 40KB 的小文件，访问速度最快。


* **一级间接映射 (Singly Indirect)**:
* `index[10] ~ index[11]`：指向一个索引块（Index Block），该块存储 1024 个物理块号。
* 适用：< 8MB 的中型文件。


* **二级间接映射 (Doubly Indirect)**:
* `index[12]`：指向一个二级索引块，该块存储 1024 个一级索引块的地址。
* 适用：< 4GB 的大型文件。



**关键逻辑**：
我们在实现 `locate_or_add_block` 时，采取了**按需分配**策略。如果目标逻辑块对应的物理块尚未分配（为 0），则调用 `bitmap_alloc` 分配新块，并将其记录在索引表中。对于间接索引，如果中间的索引块不存在，也会递归地进行分配。

#### 2. 数据读写接口

在打通地址映射后，我们封装了对外的读写接口：

* `inode_read_data`: 将文件逻辑偏移量转换为物理块号 -> 读取 Buffer -> 拷贝到用户 buffer。
* `inode_write_data`: 同样进行地址转换 -> 修改 Buffer 内容 -> 标记 Dirty -> 等待回写。

#### 3. 生命周期管理 (Lifecycle)

Inode 既存在于磁盘（持久化），也存在于内存（运行时缓存）。为了管理这种状态，我们维护了一个 **Inode Cache**：

* `inode_disk_t` (磁盘结构): 紧凑的 64 字节，存储 `type`, `size`, `nlink`, `index`。
* `inode_t` (内存结构): 包裹了磁盘结构，增加了 `ref` (引用计数), `slk` (睡眠锁), `valid_info` (有效位)。

**同步机制 (`inode_rw`)**:

* **Disk -> Memory**: 当 `inode_get` 命中失败时，从磁盘读取 inode block 到内存，并置 `valid_info = true`。
* **Memory -> Disk**: 当文件 size 或 link 发生变化时，将内存中的 `disk_info` 写回磁盘 buffer。

### 目录项与目录树 (`dentry.c`)

有了 Inode，我们只能通过数字（如 Inode 15）来访问文件。为了支持文件名（如 "hello.c"），我们需要实现 Dentry。

#### 1. 目录即文件

在 ToasterOS 中，**目录也是一种文件** (`INODE_TYPE_DIR`)。它的数据块中存储的不是普通文本，而是 `dentry_t` 结构体数组：

```c
typedef struct dentry {
    char name[MAXLEN_FILENAME]; // 文件名，如 "home"
    uint32 inode_num;           // 对应的 Inode 号
} dentry_t;

```

#### 2. 增删查改

* **`dentry_search`**: 遍历目录的数据块，逐个比较 `name`。
* **`dentry_create`**: 寻找空的 dentry 槽位（`name[0] == 0`），写入新文件名和 Inode 号。如果目录块已满，会自动扩大目录大小（利用了 Inode 的自动增长特性）。
* **`dentry_delete`**: 将对应槽位的 `name` 清零，断开链接。

### 路径解析系统 (`fs.c`)

为了支持绝对路径（如 `/home/user/file.txt`），我们需要将扁平的 Dentry 查找串联起来。

#### 1. 路径解析 (`path_to_inode`)

这是一个循环查找过程（Walking the path）：

1. 从 **Root Inode** 开始。
2. 使用 `get_element` 提取一级路径名（如 "home"）。
3. 在当前目录下调用 `dentry_search` 找到下一级 Inode。
4. **Hand-over-hand Locking (交替加锁)**：
* 解锁当前目录 Inode。
* 锁定下一级 Inode。
* 重复步骤 2，直到路径结束。



#### 2. 难点：父目录查找

`path_to_parent_inode` 用于创建新文件时（我们需要父目录的 Inode 来写入新 Dentry）。逻辑与上述类似，但在解析到最后一级目录名时提前停止并返回。

---

## 0x02 遇到的挑战与解决方案 

在开发过程中，我们参照 Lab 指导书和参考实现，克服了几个关键问题：

### 1. 物理内存分配的碎片问题

**问题现象**：在进行大文件写入测试（Test 2）时，需要申请连续的物理页面作为测试数据源。如果系统的物理内存分配器（`pmem_alloc`）在经过 Buffer 初始化后变得不连续，会导致测试数据校验失败。
**解决方案**：
我们在 `fs_init` 的最开始（Buffer 初始化之前）预先申请测试所需的物理页面，确保获得连续内存。同时增加了对内存地址增长方向的兼容性检查。

### 2. 路径解析中的死锁 (Deadlock)

**问题现象**：在实现 `path_to_parent_inode` 时，系统在解析路径时卡死。
**原因分析**：在返回找到的父节点 Inode 之前，**忘记释放该 Inode 的锁**。当调用者拿到返回的 Inode 指针并试图再次加锁（`inode_lock`）时，就会发生自我死锁。
**解决方案**：
严格遵守锁的规范：函数返回 Inode 指针时，如果该 Inode 处于上锁状态，必须在函数说明中明确，或者在返回前解锁（通常选择返回前解锁，由调用者决定何时加锁）。我们在返回前添加了 `inode_unlock(ip)`。

### 3. 数据持久化遗漏

**问题现象**：文件创建并写入后，重新读取时发现文件大小为 0。
**原因分析**：写入数据更新了 Inode 的 `size` 字段（在内存中），但没有显式调用 `inode_rw` 将这份元数据更新写回磁盘。
**解决方案**：在文件写入操作完成后，必须调用 `inode_rw(ip, true)` 强制同步元数据。

---

## 0x03 测试结果展示

我们将所有测试集成在 `src/kernel/fs/fs.c` 的 `fs_init` 函数中。

### Test 1: Inode 生命周期测试

测试了 Inode 的分配、引用计数 (`ref`) 变化、硬链接计数 (`nlink`) 变化以及最终的回收。

* **结果**：Bitmap 正确反映了 Inode 的占用与释放；引用计数增减符合预期。

### Test 2: 大文件读写测试

测试了跨越直接映射、一级间接、二级间接的大文件写入（约 100KB~级别，视虚拟机内存而定）。

* **结果**：写入的数据与回读的数据完全一致，证明多级索引逻辑正确。

### Test 3: 目录项操作

测试了在根目录下创建 `new_dir`，查找它，最后删除它。

* **结果**：`dentry_search` 能够正确返回 Inode 号，删除后无法再次搜到。

### Test 4: 路径解析

测试了复杂路径 `///AABBC///aaabb/file.txt` 的解析。

* **结果**：成功定位到深层文件并读取出内容 "This is file context!"。

---

## 0x04 总结

Lab-8 是一个承上启下的实验。我们从底层的块设备驱动出发，通过抽象出 **Inode** 和 **Dentry**，构建了一个功能完备的文件系统雏形。

* **Inode** 让磁盘块变成了“文件”。
* **Dentry** 让文件变成了“树”。
