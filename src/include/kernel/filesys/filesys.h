#ifndef TINY_OS_FILESYS_FILESYS_H
#define TINY_OS_FILESYS_FILESYS_H

#include <shared/filesys.h>
#include <shared/stdint.h>

void init_filesys();

void destroy_filesys();

/* （只读模式）打开一个文件 */
file_handle kopen_regular_reading(filesys_dp_handle dp, const char *path,
                                 enum filesys_opr_result *rt);

/* 以写模式打开一个文件 */
file_handle kopen_regular_writing(filesys_dp_handle dp, const char *path,
                                 enum filesys_opr_result *rt);

/* 获知一个目录下有多少文件 */
uint32_t kget_child_file_count(filesys_dp_handle dp, const char *path,
                               enum filesys_opr_result *rt);

/* 取得某目录下第idx个文件的信息 */
enum filesys_opr_result kget_child_file_info(filesys_dp_handle dp, const char *path, uint32_t idx,
                                             struct syscall_filesys_file_info *info);

/* 关闭一个文件 */
enum filesys_opr_result kclose_file(filesys_dp_handle dp, file_handle file);

/* 创建一个空文件 */
enum filesys_opr_result kmake_regular(filesys_dp_handle dp, const char *path);

/* 删除一个文件 */
enum filesys_opr_result kremove_regular(filesys_dp_handle dp, const char *path);

/* 创建一个空目录 */
enum filesys_opr_result kmake_directory(filesys_dp_handle dp, const char *path);

/* 删除一个空目录 */
enum filesys_opr_result kremove_directory(filesys_dp_handle dp, const char *path);

/* 取得一个文件的字节数 */
uint32_t kget_regular_size(filesys_dp_handle dp, file_handle file);

/*
    向一个常规文件写入二进制内容
    设文件大小为file_size，则写入合法当且仅当
        fpos <= file_size
*/
enum filesys_opr_result kwrite_to_regular(
                            filesys_dp_handle dp, file_handle file,
                            uint32_t fpos, uint32_t size,
                            const void *data);

/* 从常规文件中读二进制内容 */
enum filesys_opr_result kread_from_regular(
                            filesys_dp_handle dp, file_handle file,
                            uint32_t fpos, uint32_t size,
                            void *data);

#endif /* TINY_OS_FILESYS_FILESYS_H */
