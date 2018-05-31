#include <shared/sys.h>

int main(int argc, char *argv[])
{
    alloc_fg();

    for(int i = 0; i < argc; ++i)
        printf("%s\n", argv[i]);

    register_char_msg();
    register_key_msg();

    while(true)
    {
        wait_for_sysmsg();
        struct sysmsg msg;
        while(peek_sysmsg(SYSMSG_SYSCALL_PEEK_OPERATION_REMOVE, &msg))
        {
            if(msg.type == SYSMSG_TYPE_CHAR)
            {
                put_char(get_chmsg_char(&msg));
            }
            else if(msg.type == SYSMSG_TYPE_KEYBOARD)
            {
                if(get_kbmsg_key(&msg) == VK_ESCAPE)
                    return 0;
            }
        }
    }

    return 0;
}
