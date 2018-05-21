#include <lib/filesys.h>
#include <shared/syscall/common.h>

enum filesys_opr_result open_file(filesys_dp_handle dp,
                                  const char *path, bool writing,
                                  usr_file_handle *result)
{
    struct syscall_filesys_open_params args =
    {
        .writing = writing,
        .dp      = dp,
        .path    = path,
        .result  = result
    };
    return syscall_param1(SYSCALL_FILESYS_OPEN, &args);
}

enum filesys_opr_result close_file(usr_file_handle file)
{
    return syscall_param1(SYSCALL_FILESYS_CLOSE, file);
}

enum filesys_opr_result make_file(filesys_dp_handle dp,
                                  const char *path)
{
    return syscall_param2(SYSCALL_FILESYS_MKFILE, dp, path);
}

enum filesys_opr_result remove_file(filesys_dp_handle dp,
                                    const char *path)
{
    return syscall_param2(SYSCALL_FILESYS_RMFILE, dp, path);
}

enum filesys_opr_result make_directory(filesys_dp_handle dp,
                                       const char *path)
{
    return syscall_param2(SYSCALL_FILESYS_MKDIR, dp, path);
}

enum filesys_opr_result remove_directory(filesys_dp_handle dp,
                                         const char *path)
{
    return syscall_param2(SYSCALL_FILESYS_RMDIR, dp, path);
}

uint32_t get_file_size(usr_file_handle file)
{
    return syscall_param1(SYSCALL_FILESYS_GET_SIZE, file);
}

enum filesys_opr_result write_file(usr_file_handle file,
                                  uint32_t fpos, uint32_t byte_size,
                                  const void *data)
{
    struct syscall_filesys_write_params args =
    {
        .file      = file,
        .fpos      = fpos,
        .byte_size = byte_size,
        .data_src  = data
    };
    return syscall_param1(SYSCALL_FILESYS_WRITE, &args);
}

enum filesys_opr_result read_file(usr_file_handle file,
                                  uint32_t fpos, uint32_t byte_size,
                                  void *data)
{
    struct syscall_filesys_read_params args =
    {
        .file      = file,
        .fpos      = fpos,
        .byte_size = byte_size,
        .data_dst  = data
    };
    return syscall_param1(SYSCALL_FILESYS_READ, &args);
}
