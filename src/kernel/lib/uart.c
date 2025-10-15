/* low-level driver routines for 16550a UART. */

#include "mod.h"

#define BACKSPACE 0x100
#define C(x) ((x) - '@') // Control-x

// from printf.c 终止输出的标志
extern volatile int panicked;

// uart 初始化
void uart_init(void)
{
  	// 关闭中断
	WriteReg(IER, 0x00);

	// 进入设置比特率的模式
	WriteReg(LCR, LCR_BAUD_LATCH);

	// 设置比特率的低位和高位，最终设置为38.4K
	WriteReg(0, 0x03);
  	WriteReg(1, 0x00);

	// 设置传输字节长度为8bit,不校验
	WriteReg(LCR, LCR_EIGHT_BITS);

	// 清零和使能FIFO模式
	WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

	// 使能输出队列和接收队列的中断
	WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);
}

// 单个字符输出
void uart_putc_sync(int c)
{
	// 关闭中断
	push_off();

	// 如果错误发生则卡住
	while (panicked)
		;

	// 等待TX队列进入idle状态
	while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
		;

	// 输出
	WriteReg(THR, c);

	// 开启中断
	pop_off();
}

// 单个字符输入--->不支持换行（因为enter是\r\n)，不支持删除（因为只是输出0x08没有回退和移动光标）
// 失败返回-1
int uart_getc_sync(void)//非阻塞式
{
	if (ReadReg(LSR) & 0x01) // Line Status Register（串口状态寄存器）收到字符
		return ReadReg(RHR); // Receiver Holding Register，UART 接收寄存器
	else
		return -1;
}

// 中断处理(键盘输入->屏幕输出)
void uart_intr(void)
{
	while (1) // 不停尝试读取 UART
	{
		int c = uart_getc_sync(); // 尝试读一个字符
		if (c == -1)			  // 有字符可读，退出循环
			break;
		uart_putc_sync(c); // 把读取的字符同步输出到屏幕或串口（回显）
	}
}

// 发送一个字符并处理回显/换行/退格
void uart_putc_sync_ext(int c)
{
	if (c == '\n')
	{
		// 终端通常需要回车+换行
		uart_putc_sync('\r');
		uart_putc_sync('\n');
	}
	else if (c == 0x08 || c == 0x7f)
	{ // Backspace/Delete
		// 光标回退、用空格覆盖再回退
		uart_putc_sync(0x08);
		uart_putc_sync(' ');
		uart_putc_sync(0x08);
	}
	else
	{
		// 普通字符直接发送
		uart_putc_sync(c);
	}
}
