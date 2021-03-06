#include <shared/syscall.h>
#include <shared/keyboard.h>
#include <shared/sysmsg.h>
#include <shared/sys.h>

bool is_key_pressed(uint8_t kc)
{
    return syscall_param2(SYSCALL_KEYBOARD_QUERY,
                KEYBOARD_SYSCALL_FUNCTION_IS_KEY_PRESSED, kc) != 0;
}

void register_key_msg()
{
    syscall_param1(SYSCALL_SYSMSG_OPERATION,
                   SYSMSG_SYSCALL_FUNCTION_REGISTER_KEYBOARD_MSG);
}

void register_char_msg()
{
    syscall_param1(SYSCALL_SYSMSG_OPERATION,
                   SYSMSG_SYSCALL_FUNCTION_REGISTER_CHAR_MSG);
}

