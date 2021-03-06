#include <kernel/asm.h>
#include <kernel/assert.h>
#include <kernel/console/console.h>
#include <kernel/console/print.h>
#include <kernel/process/process.h>

#include <shared/stdbool.h>
#include <shared/string.h>

void _fatal_error_impl(const char *prefix, const char *filename, const char *function, int line, const char *msg)
{
    _disable_intr();
    
    struct con_buf *buf = get_sys_con_buf();

    kput_char(buf, '\n');
    kput_str(buf, prefix);
    kput_str(buf, filename);
    kput_str(buf, ", ");
    kput_str(buf, function);
    kput_str(buf, ", line ");

    char int_str_buf[40];
    uint32_to_str(line, int_str_buf);
    kput_str(buf, int_str_buf);

    uint32_to_str(get_cur_PCB()->pid, int_str_buf);
    kput_str(buf, " from proc ");
    kput_str(buf, int_str_buf);
    
    kput_str(buf, ": ");
    kput_str(buf, msg);

    while(true)
        ;
}
