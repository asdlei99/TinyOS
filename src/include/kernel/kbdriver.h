#ifndef TINY_OS_KB_DRIVER_H
#define TINY_OS_KB_DRIVER_H

#include <kernel/sysmsg/sysmsg.h>

/*
    键盘驱动
*/

struct PCB;

/*
    msg参数含义:
    struct
    {
        uint32_t key
        uint32_t flags
        uint32_t reserved
    }

    flags0 down/up
*/
struct kbmsg_struct
{
    sysmsg_type type; // 为SYSMSG_TYPE_KEYBOARD
    uint32_t key;
    uint32_t flags;
    uint32_t reserved;
};

/*
    char消息
    struct
    {
        uint32_t char
    }
*/
struct kbchar_msg_struct
{
    sysmsg_type type; // 为SYSMSG_TYPE_CHAR
    uint32_t ch;
    uint32_t reserved1;
    uint32_t reserved2;
};

#define KBMSG_FLAG_UP 0x1

/* 键盘驱动初始化 */
void init_kb_driver(void);

/*
    订阅键盘消息
    通常由进程自己调用
*/
void subscribe_kb(struct PCB *pcb);

/*
    订阅字符输入消息
*/
void subscribe_char(struct PCB *pcb);

/* 查询某个按键的状态 */
bool is_key_pressed(uint8_t keycode);

/*
    键盘状态查询相关的系统调用
    参数是功能号
*/
uint32_t syscall_keyboard_query_impl(uint32_t func, uint32_t arg);

/*
    功能号：某个按键是否按下
    第一个参数为虚拟键值
*/
#define KEYBOARD_SYSCALL_FUNCTION_IS_KEY_PRESSED 0

#endif /* TINY_OS_KB_DRIVER_H */
