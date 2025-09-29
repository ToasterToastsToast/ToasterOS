#pragma once

/* print.c: 标准输出与错误处理函数 */

void print_init(void);
void printf(const char *fmt, ...);
void panic(const char *s);
void assert(bool condition, const char *warning);
int putchar(int c);
void puts(char* ptr);
/* uart.c: UART驱动函数 */

void uart_init(void);
void uart_putc_sync(int c);
int uart_getc_sync(void);
void uart_intr(void);

/*cpu.c: 获得CPU信息 */

int mycpuid(void);
cpu_t *mycpu(void);

/* utils.c: 一些常用的工具函数 */

void memset(void *begin, uint8 data, uint32 n);
void memmove(void *dst, const void *src, uint32 n);
int strncmp(const char *p, const char *q, uint32 n);
