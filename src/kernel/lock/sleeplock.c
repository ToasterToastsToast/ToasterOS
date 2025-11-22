#include "mod.h"
#include "../proc/mod.h" // 需要 proc_sleep, proc_wakeup

// 睡眠锁初始化
void sleeplock_init(sleeplock_t *lk, char *name) {
    spinlock_init(&lk->lock, "sleeplock");
    lk->locked = 0;
    lk->pid = 0;
}

// 检查当前进程是否持有睡眠锁
bool sleeplock_holding(sleeplock_t *lk) {
    int r;
    spinlock_acquire(&lk->lock);
    r = lk->locked && (lk->pid == myproc()->pid);
    spinlock_release(&lk->lock);
    return r;
}

// 当前进程尝试获取睡眠锁, 失败进入睡眠状态
void sleeplock_acquire(sleeplock_t *lk) {
    spinlock_acquire(&lk->lock);
    while (lk->locked) {
        proc_sleep(lk, &lk->lock); // 睡眠在 lk 上
    }
    lk->locked = 1;
    lk->pid = myproc()->pid;
    spinlock_release(&lk->lock);
}

// 释放睡眠锁, 唤醒其他等待睡眠锁的进程
void sleeplock_release(sleeplock_t *lk) {
    spinlock_acquire(&lk->lock);
    lk->locked = 0;
    lk->pid = 0;
    proc_wakeup(lk); // 唤醒等待者
    spinlock_release(&lk->lock);
}