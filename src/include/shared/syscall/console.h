#ifndef TINY_OS_SHARED_SYSCALL_CONSOLE_H
#define TINY_OS_SHARED_SYSCALL_CONSOLE_H

#include <shared/syscall/common.h>

/* console相关的系统调用第一个参数为操作类型，后面的才是真正的调用参数 */

/* high16：pos，low8：char */
#define CONSOLE_SYSCALL_FUNCTION_SET_CHAR        0
/* high16：pos，low8：attrib */
#define CONSOLE_SYSCALL_FUNCTION_SET_ATTRIB      1
/* high16：pos，high8(low16)：char，low8：attrib */
#define CONSOLE_SYSCALL_FUNCTION_SET_CHAR_ATTRIB 2
/* void */
#define CONSOLE_SYSCALL_FUNCTION_CLEAR_SCREEN    3

/* 80 * row + col */
#define CONSOLE_SYSCALL_FUNCTION_SET_CURSOR      4
/*
    void
    return：80 * row + col
*/
#define CONSOLE_SYSCALL_FUNCTION_GET_CURSOR      5

/* low8：char */
#define CONSOLE_SYSCALL_FUNCTION_PUT_CHAR        6
/* pointer to str */
#define CONSOLE_SYSCALL_FUNCTION_PUT_STR         7
/* void */
#define CONSOLE_SYSCALL_FUNCTION_ROLL_SCREEN     8

/* 控制台系统系统调用功能号数量 */
#define CONSOLE_SYSCALL_FUNCTION_COUNT 9

#endif /* TINY_OS_SHARED_SYSCALL_CONSOLE_H */
