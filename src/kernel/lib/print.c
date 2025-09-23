/* 标准输出和报错机制 */
#include <stdarg.h>
#include "mod.h"

static char digits[] = "0123456789abcdef";

/* printf的自旋锁 */
static spinlock_t print_lk;
static int print_locking = 1;


/* 初始化uart + 初始化printf锁 */
void print_init(void)
{
    uart_init();
    spinlock_init(&print_lk, "printf");
    print_locking = 1;
}

/* %d %p */
static void printint(int xx, int base, int sign)
{
    char buf[16];
    int i;
    uint32 x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    i = 0;
    do
    {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        uart_putc_sync(buf[i]);
}

/* %x */
static void printptr(uint64 x)
{
    uart_putc_sync('0');
    uart_putc_sync('x');
    for (int i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
        uart_putc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

static void printchar(int c){
    uart_putc_sync(c);
}

static void printstring(char* ptr){
    while (*ptr!='\0'){
        uart_putc_sync(*ptr);
        ptr++;
    }
}
/*
    标准化输出, 需要支持:
    1. %d (32位有符号数,以10进制输出)
    2. %p (32位无符号数,以16进制输出)
    3. %x (64位无符号数,以0x开头的16进制输出)
    4. %c (单个字符)
    5. %s (字符串)
    提示: stdarg.h中的va_list中包括你需要的参数地址
*/
void printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int i, c;

    if (print_locking) {
        spinlock_acquire(&print_lk);
    }
    assert(fmt != 0, "null fmt");
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            uart_putc_sync(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0) {
            break;
        }
        switch (c) {
            case 'd':
                printint(va_arg(ap, int), 10, 1);
                break;
            case 'x':
                printptr(va_arg(ap, uint64));
                break;
            case 'p':
                printint(va_arg(ap, int), 16, 0);
                break;
            case 'c':
                printchar(va_arg(ap, int));
                break;
            case '%':
                printchar('%');
                break;
            case 's':
                printstring(va_arg(ap, char *));
                break;
            default:
                printchar('%');
                printchar(c);
                break;
        }
    }
    if (print_locking) {
        spinlock_release(&print_lk);
    }
}



/* 如果发生panic, UART的停止标志 */
volatile int panicked = 0;

/* 报错并终止输出 */
void panic(const char *s)
{
    printf("panic! %s\n", s);
    panicked = 1;
    while (1)
        ;
}

/* 如果不满足条件, 则调用panic */
void assert(bool condition, const char *warning)
{
    if (!condition){
        panic(warning);
    }
}
