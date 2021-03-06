#include <kernel/assert.h>
#include <kernel/console/console.h>
#include <kernel/explorer/cmds.h>
#include <kernel/explorer/disp.h>
#include <kernel/explorer/explorer.h>
#include <kernel/explorer/screen.h>
#include <kernel/filesys/dpt.h>
#include <kernel/filesys/import/import.h>
#include <kernel/interrupt.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/process/process.h>
#include <kernel/execelf/execelf.h>

#include <shared/ctype.h>
#include <shared/explorer.h>
#include <shared/string.h>
#include <shared/sys.h>

/*
    IMPROVE: 这个explorer是赶工的结果，写得烂如翔（
             谁来重写一个……
*/

/* explorer命令输入buffer大小，三行 */
#define CMD_INPUT_BUF_SIZE (SCR_CMD_WIDTH * SCR_CMD_HEIGHT)

/* explorer显示区和命令区的默认标题 */
#define DISP_TITLE ("Output")
#define CMD_TITLE ("Command")

/* explorer工作目录buffer大小 */
#define EXPL_WORKING_DIR_BUF_SIZE 256

/* explorer初始工作目录 */
#define INIT_WORKING_DIR ("/")

/* explorer应有的PID */
#define EXPL_PID 1

/* Explorer状态 */
enum explorer_state
{
    es_fg,  // explorer本身处于前台，可接受输入
    es_bg,  // 有接管了输入输出的进程处于前台
};

/*
    当前处于前台的进程
    为空表示explorer自身应被显示
*/
static struct PCB *cur_fg_proc;

/* explorer自身的进程PCB，永不失效（ */
static struct PCB *expl_proc;

/* explorer状态 */
static enum explorer_state expl_stat;

/* explorer命令输入buffer */
static char *expl_cmd_input_buf;
static uint32_t expl_cmd_input_size;

/* explorer工作路径buffer */
static filesys_dp_handle expl_working_dp;
static char *expl_working_dir;

/* 上一个成功创建的进程号 */
static uint32_t last_created_proc_pid;

static void init_explorer()
{
    intr_state is = fetch_and_disable_intr();

    // explorer进程永远处于“前台”，以遍接收键盘消息
    // 话虽如此，并不能随便输出东西到系统控制台上

    struct PCB *pcb = get_cur_PCB();

    // explorer应该是1号进程
    ASSERT(pcb->pid == EXPL_PID);

    // 把自己从PCB列表中摘除
    erase_from_ilist(&pcb->processes_node);

    // explorer的pis标志永远是foreground，因为它必须相应如C+z这样的事件
    pcb->pis      = pis_foreground;
    pcb->disp_buf = kalloc_con_buf();

    cur_fg_proc   = NULL;
    expl_proc     = pcb;
    expl_stat     = es_fg;

    last_created_proc_pid = 0;

    // 初始化输入缓冲区
    expl_cmd_input_buf =
        (char*)alloc_static_kernel_mem(CMD_INPUT_BUF_SIZE, 1);
    expl_cmd_input_buf[0]  = '\0';
    expl_cmd_input_size    = 0;

    // 初始化工作目录缓冲区
    expl_working_dp  = 0;
    expl_working_dir =
        (char*)alloc_static_kernel_mem(EXPL_WORKING_DIR_BUF_SIZE, 1);
    strcpy(expl_working_dir, INIT_WORKING_DIR);

    cmd_char2(0, '_');

    scr_disp_caption(DISP_TITLE);
    scr_cmd_caption(CMD_TITLE);

    // 注册键盘消息
    register_key_msg();
    register_char_msg();

    init_disp();

    set_intr_state(is);
}

static void clear_input()
{
    clr_cmd();
    expl_cmd_input_buf[0] = '\0';
    expl_cmd_input_size   = 0;
    cmd_char2(0, '_');
}

/* 把一个进程调到前台来，失败时（如进程不存在）返回false */
static bool make_proc_foreground(uint32_t pid)
{
    intr_state is = fetch_and_disable_intr();

    struct PCB *pcb = get_PCB_by_pid(pid);
    if(!pcb || pid < 2)
    {
        set_intr_state(is);
        return false;
    }

    ASSERT(cur_fg_proc == NULL);
    ASSERT(expl_stat == es_fg && pcb->pis == pis_background);

    expl_stat      = es_bg;
    expl_proc->pis = pcb->disp_buf ? pis_expl_foreground : pis_foreground;
    pcb->pis       = pis_foreground;
    cur_fg_proc    = pcb;

    if(pcb->disp_buf)
    {
        copy_scr_to_con_buf(expl_proc);
        copy_con_buf_to_scr(pcb);
    }

    set_intr_state(is);
    return true;
}

/* 将当前前台进程切换到后台 */
static void explorer_usr_interrupt()
{
    intr_state is = fetch_and_disable_intr();

    if(!cur_fg_proc)
    {
        ASSERT(expl_stat == es_fg);
        set_intr_state(is);
        return;
    }

    ASSERT(expl_stat == es_bg);

    cur_fg_proc->pis = pis_background;
    copy_scr_to_con_buf(cur_fg_proc);
    cur_fg_proc = NULL;
    if(expl_proc->pis == pis_expl_foreground)
        copy_con_buf_to_scr(expl_proc);

    expl_stat = es_fg;
    expl_proc->pis = pis_foreground;

    set_intr_state(is);
}

/* 杀死当前前台进程 */
static void explorer_usr_exit()
{
    intr_state is = fetch_and_disable_intr();

    if(cur_fg_proc)
    {
        // 预先把目标进程的pis设置成后台
        // 这样杀死它就不会通知explorer导致重复copy显示数据什么的
        cur_fg_proc->pis = pis_background;
        
        kill_process(cur_fg_proc);
        cur_fg_proc    = NULL;
        expl_stat      = es_fg;
        if(expl_proc->pis == pis_expl_foreground)
            copy_con_buf_to_scr(expl_proc);
        expl_proc->pis = pis_foreground;
    }

    set_intr_state(is);
}

/*
    匹配形如 str1 str2 ... strN 的命令，会改写buf为
        str1 \0 str2 \0 ... strN \0
    所有空白字符均会被跳过
    返回识别到的str数量
*/
static uint32_t expl_parse_cmd(char *buf)
{
    uint32_t ri = 0, wi = 0; //ri用于读buf，wi用于改写buf
    uint32_t cnt = 0;

    while(true)
    {
        // 跳过空白字符
        while(buf[ri] && isspace(buf[ri]))
            ++ri;

        // 到头了，该结束了
        if(!buf[ri])
            break;

        // 取得一条str
        while(buf[ri] && !isspace(buf[ri]))
            buf[wi++] = buf[ri++];
        ++cnt;

        // 把str末端置为\0
        if(buf[ri])
            ++ri;
        buf[wi++] = '\0';
    }

    return cnt;
}

static const char *next_strs(const char *str, const char **output,
                             uint32_t total_cnt, uint32_t out_buf_size,
                             uint32_t *out_size)
{
    ASSERT(str && output && out_buf_size && out_size);

    uint32_t cnt = 0;
    while(true)
    {
        if(cnt >= out_buf_size || cnt >= total_cnt)
        {
            *out_size = cnt;
            return str;
        }

        output[cnt++] = str;

        if(str[0] == '|' && !str[1])
        {
            *out_size = cnt;
            return str += 2;
        }

        while(*str++)
            ;
    }
}

/* 就是一条一条地暴力匹配命令，反正这个不是很重要，先用着吧…… */
static bool explorer_exec_cmd(const char *strs, uint32_t str_cnt)
{
    uint32_t seg_cnt;
    const char *segs[EXEC_ELF_ARG_MAX_COUNT + 1];
    strs = next_strs(strs, segs, str_cnt, EXEC_ELF_ARG_MAX_COUNT, &seg_cnt);

    ASSERT(seg_cnt);
    if(seg_cnt >= EXEC_ELF_ARG_MAX_COUNT)
        goto INVALID_ARGUMENT;
    
    const char *cmd = segs[0];
    uint32_t arg_cnt = seg_cnt - 1;
    const char **args = &segs[1];

    str_cnt -= seg_cnt;

    if(strcmp(cmd, "cd") == 0)
    {
        if(arg_cnt != 1 || str_cnt)
            goto INVALID_ARGUMENT;
        
        disp_new_line();
        expl_cd(&expl_working_dp, expl_working_dir,
                EXPL_WORKING_DIR_BUF_SIZE - 1, args[0]);
    }
    else if(strcmp(cmd, "clear") == 0)
    {
        if(arg_cnt || str_cnt)
            goto INVALID_ARGUMENT;

        clr_disp();
        disp_set_cursor(0, 0);
    }
    else if(strcmp(cmd, "exec") == 0)
    {
        if(arg_cnt < 1 || str_cnt) // exec命令并不支持管道
            goto INVALID_ARGUMENT;
        
        clr_sysmsgs();
        disp_new_line();
        uint32_t pid;
        if(!expl_exec(expl_working_dp, expl_working_dir,
                      cmd, args[0], args + 1, arg_cnt - 1, &pid))
            goto INVALID_ARGUMENT;
        last_created_proc_pid = pid;
    }
    else if(strcmp(cmd, "exit") == 0)
    {
        if(arg_cnt || str_cnt)
            goto INVALID_ARGUMENT;

        return false;
    }
    else if(strcmp(cmd, "fg") == 0)
    {
        if(!arg_cnt || str_cnt)
        {
            if(!make_proc_foreground(last_created_proc_pid))
            {
                disp_new_line();
                disp_printf("Last created process: null");
                last_created_proc_pid = 0;
            }
        }
        else
        {
            uint32_t pid;
            if(arg_cnt != 1 || str_cnt || !str_to_uint32(args[0], &pid))
                goto INVALID_ARGUMENT;

            if(!make_proc_foreground(pid))
            {
                disp_new_line();
                disp_printf("Invalid pid");
            }
        }
    }
    else if(strcmp(cmd, "kill") == 0)
    {
        if(!arg_cnt || str_cnt)
            goto INVALID_ARGUMENT;
        for(uint32_t i = 0; i < arg_cnt; ++i)
        {
            uint32_t pid;
            if(!str_to_uint32(args[i], &pid) || pid < 2)
                goto INVALID_ARGUMENT;
            intr_state is = fetch_and_disable_intr();
            struct PCB *pcb = get_PCB_by_pid(pid);
            if(pcb)
                kill_process(pcb);
            set_intr_state(is);
        }
    }
    else if(strcmp(cmd, "ps") == 0)
    {
        if(arg_cnt || str_cnt)
            goto INVALID_ARGUMENT;

        disp_new_line();
        expl_show_procs();
    }
    else
    {
        clr_sysmsgs();

        intr_state is = fetch_and_disable_intr();

        uint32_t pid = 0;
        struct PCB *last_pcb = NULL;

        disable_thread_scheduler();

        while(seg_cnt)
        {
            if(strcmp(segs[seg_cnt - 1], "|") == 0)
                --seg_cnt;
            else if(str_cnt)
            {
                set_intr_state(is);
                enable_thread_scheduler();
                goto INVALID_ARGUMENT;
            }

            if(!expl_exec(expl_working_dp, expl_working_dir,
                          cmd, cmd, segs + 1, seg_cnt - 1, &pid))
            {
                set_intr_state(is);
                enable_thread_scheduler();
                goto INVALID_ARGUMENT;
            }

            struct PCB *this_pcb = get_PCB_by_pid(pid);
            if(last_pcb)
            {
                last_pcb->out_pid = pid;
                last_pcb->out_uid = this_pcb->uid;
            }
            last_pcb = this_pcb;

            strs = next_strs(strs, segs, str_cnt, EXEC_ELF_ARG_MAX_COUNT, &seg_cnt);
            str_cnt -= seg_cnt;
            cmd = segs[0];
        }

        enable_thread_scheduler();

        last_created_proc_pid = pid;
        set_intr_state(is);
    }

    return true;

INVALID_ARGUMENT:

    disp_new_line();
    disp_put_str("Invalid command/argument(s), "
                 "enter 'help' for command documentation");
    return true;
}

/* 提交并执行一条命令 */
static bool explorer_submit_cmd()
{
    bool ret = true;

    clr_cmd();

    // 命令参数解析
    uint32_t str_cnt = expl_parse_cmd(expl_cmd_input_buf);
    
    if(str_cnt)
        ret = explorer_exec_cmd(expl_cmd_input_buf, str_cnt);

    expl_cmd_input_buf[0] = '\0';
    expl_cmd_input_size   = 0;

    scr_disp_caption(DISP_TITLE);
    scr_cmd_caption(CMD_TITLE);

    cmd_char2(0, '_');

    return ret;
}

/* 提交一个命令字符 */
static void explorer_new_cmd_char(char ch)
{
    // 退格
    if(ch == '\b')
    {
        if(expl_cmd_input_size > 0)
        {
            cmd_char2(expl_cmd_input_size, ' ');
            expl_cmd_input_buf[--expl_cmd_input_size] = '\0';
            cmd_char2(expl_cmd_input_size, '_');
        }
        return;
    }

    // 命令缓冲区已满
    if(expl_cmd_input_size >= CMD_INPUT_BUF_SIZE - 1)
        return;

    // 忽略换行符和制表符
    if(ch == '\n' || ch == '\t')
        return;

    cmd_char2(expl_cmd_input_size, ch);
    expl_cmd_input_buf[expl_cmd_input_size] = ch;
    expl_cmd_input_buf[++expl_cmd_input_size] = '\0';
    cmd_char2(expl_cmd_input_size, '_');
}

static void explorer_submit_proc_input()
{
    ASSERT(!is_intr_on(get_intr_state()));

    uint32_t ipos = 0;
    while(true)
    {
        struct expl_input_msg msg;
        msg.type = SYSMSG_TYPE_EXPL_INPUT;
        uint32_t remain = expl_cmd_input_size - ipos;

        if(remain <= SYSMSG_PARAM_SIZE - 1)
        {
            memcpy(msg.params, expl_cmd_input_buf + ipos, remain);
            msg.params[remain] = '\n';
            if(remain <= SYSMSG_PARAM_SIZE - 2)
                msg.params[remain + 1] = '\0';

            send_sysmsg(&cur_fg_proc->sys_msgs, (struct sysmsg *)&msg);
            break;
        }

        memcpy(msg.params, expl_cmd_input_buf + ipos, SYSMSG_PARAM_SIZE);
        send_sysmsg(&cur_fg_proc->sys_msgs, (struct sysmsg *)&msg);
        ipos += SYSMSG_PARAM_SIZE;
    }

    clear_input();
}

/* 提交一个托管输入字符 */
static void explorer_new_proc_input_char(char ch)
{
    ASSERT(!is_intr_on(get_intr_state()));

    // 退格
    if(ch == '\b')
    {
        if(expl_cmd_input_size > 0)
        {
            cmd_char2(expl_cmd_input_size, ' ');
            expl_cmd_input_buf[--expl_cmd_input_size] = '\0';
            cmd_char2(expl_cmd_input_size, '_');
        }
        return;
    }

    // 命令缓冲区已满
    if(expl_cmd_input_size >= CMD_INPUT_BUF_SIZE - 1)
        return;

    // 忽略制表符
    if(ch == '\t')
        return;

    // 遇到换行符，可以提交一条输入了
    if(ch == '\n')
    {
        explorer_submit_proc_input();
        return;
    }
    
    cmd_char2(expl_cmd_input_size, ch);
    expl_cmd_input_buf[expl_cmd_input_size] = ch;
    expl_cmd_input_buf[++expl_cmd_input_size] = '\0';
    cmd_char2(expl_cmd_input_size, '_');
}

static bool process_expl_sysmsg(struct sysmsg *msg)
{
    struct expl_output_msg *m = (struct expl_output_msg *)msg; 

    if(msg->type == SYSMSG_TYPE_EXPL_OUTPUT)
    {
        ASSERT(!m->out_uid);
        disp_put_char(m->ch);
        return true;
    }

    if(msg->type == SYSMSG_TYPE_EXPL_NEW_LINE)
    {
        disp_new_line();
        return true;
    }

    return false;
}

/*
    进行一次explorer状态转移
    返回false时停机
*/
static bool explorer_transfer()
{
    if(expl_stat == es_fg)
    {
        struct sysmsg msg;
        if(peek_sysmsg(SYSMSG_SYSCALL_PEEK_OPERATION_REMOVE, &msg))
        {
            // 取得一条键盘按下的消息
            if(msg.type == SYSMSG_TYPE_KEYBOARD &&
               is_kbmsg_down(&msg))
            {
                uint8_t key = get_kbmsg_key(&msg);

                // 按回车提交一条命令
                if(key == VK_ENTER)
                    return explorer_submit_cmd();
            }
            else if(msg.type == SYSMSG_TYPE_CHAR) // 取得一条字符消息
            {
                explorer_new_cmd_char(get_chmsg_char(&msg));
            }
            else
                process_expl_sysmsg(&msg);
        }
    }
    else if(expl_stat == es_bg)
    {
        struct sysmsg msg;
        if(peek_sysmsg(SYSMSG_SYSCALL_PEEK_OPERATION_REMOVE, &msg))
        {
            // 取得一条键盘按下的消息
            if(msg.type == SYSMSG_TYPE_KEYBOARD &&
               is_kbmsg_down(&msg))
            {
                uint8_t key = get_kbmsg_key(&msg);
                
                // 试图将当前前台程序切到后台
                if(key == 'Z' && is_key_pressed(VK_LCTRL))
                {
                    explorer_usr_interrupt();
                    clr_sysmsgs();
                    return true;
                }

                // 试图干掉当前前台程序
                if(key == 'C' && is_key_pressed(VK_LCTRL))
                {
                    explorer_usr_exit();
                    clr_sysmsgs();
                    return true;
                }
            }
            else if(process_expl_sysmsg(&msg))
            {
                return true;
            }
            else if(msg.type == SYSMSG_TYPE_CHAR)
            {
                // 如果前台进程没有显示缓存，那么字符消息应该作为中断输入缓存下来
                // 因为涉及到进程状态，这里对前台进程显示缓存的查询需要关中断
                intr_state is = fetch_and_disable_intr();

                if(!cur_fg_proc || cur_fg_proc->disp_buf)
                {
                    set_intr_state(is);
                    return true;
                }
                
                explorer_new_proc_input_char(get_chmsg_char(&msg));

                set_intr_state(is);
                return true;
            }
        }
    }
    else // 暂时没有实现 explorer IO delegation，所以es_dg是非法状态
        FATAL_ERROR("invalid explorer state");

    return true;
}

void explorer()
{
    init_explorer();

    reformat_dp(0, DISK_PT_AFS);

    make_directory(0, "/apps");
    make_directory(0, "/docs");
    ipt_import_from_dp(get_dpt_unit(DPT_UNIT_COUNT - 1)->sector_begin);

    static const char welcome[] =
        "Welcome to TinyOS\n"
        "Enter 'help' for command documentation";
    disp_put_str(welcome);

    while(explorer_transfer())
        ;

    destroy_kernel();
    clr_scr();
    while(true)
        ;
}

void foreground_exit(struct PCB *pcb)
{
    intr_state is = fetch_and_disable_intr();
    
    if(pcb != cur_fg_proc)
    {
        set_intr_state(is);
        return;
    }

    cur_fg_proc    = NULL;
    expl_stat      = es_fg;
    if(expl_proc->pis == pis_expl_foreground)
        copy_con_buf_to_scr(expl_proc);
    expl_proc->pis = pis_foreground;

    set_intr_state(is);
}

uint32_t syscall_alloc_fg_impl()
{
    struct PCB *pcb = get_cur_PCB();
    if(cur_fg_proc)
        return cur_fg_proc == pcb;
    return make_proc_foreground(pcb->pid);
}

uint32_t syscall_free_fg_impl()
{
    if(cur_fg_proc != get_cur_PCB())
        return false;
    explorer_usr_interrupt();
    return true;
}

uint32_t syscall_alloc_con_buf_impl()
{
    struct PCB *pcb = get_cur_PCB();
    if(pcb->disp_buf || cur_fg_proc == pcb)
        return false;
    pcb->disp_buf = kalloc_con_buf();
    return pcb->disp_buf != NULL;
}

static struct PCB *get_out_pcb(struct PCB *src)
{
    struct PCB *ret = get_PCB_by_pid(src->out_pid);
    if(!ret || ret->uid != src->out_uid)
        return NULL;
    return ret;
}

static struct sysmsg_queue *get_out_queue(struct PCB *src)
{
    if(src->out_uid)
    {
        struct PCB *pcb = get_out_pcb(src);
        return pcb ? &pcb->sys_msgs : NULL;
    }
    return &expl_proc->sys_msgs;
}

uint32_t syscall_put_char_expl_impl(uint32_t arg)
{
    struct PCB *pcb = get_cur_PCB();

    struct sysmsg msg;

    if(pcb->out_uid)
    {
        struct expl_input_msg *m = (struct expl_input_msg *)&msg;
        m->type      = SYSMSG_TYPE_EXPL_INPUT;
        m->params[0] = arg & 0xff;
        m->params[1] = '\0';
    }
    else
    {
        struct expl_output_msg *m = (struct expl_output_msg *)&msg;
        m->type = SYSMSG_TYPE_EXPL_OUTPUT;
        m->ch   = arg & 0xff;
        m->out_pid = pcb->out_pid;
        m->out_uid = pcb->out_uid;
    }

    // 这里如果消息发送失败（常常是因为对方消息队列满了），yield_cpu()的时候目标进程可能会挂掉
    // 所以每一轮都要重新获取目标消息队列
    struct sysmsg_queue *dst_que;
    while((dst_que = get_out_queue(pcb)) && !send_sysmsg(dst_que, (struct sysmsg *)&msg))
    {
        thread_syscall_protector_entry();
        yield_cpu();
        thread_syscall_protector_exit();
    }

    return 0;
}

uint32_t syscall_expl_new_line_impl()
{
    struct expl_input_msg msg;
    msg.type = SYSMSG_TYPE_EXPL_NEW_LINE;

    while(!send_sysmsg(&expl_proc->sys_msgs, (struct sysmsg *)&msg))
    {
        thread_syscall_protector_entry();
        yield_cpu();
        thread_syscall_protector_exit();
    }
    
    return 0;
}

uint32_t syscall_expl_pipe_null_char()
{
    struct PCB *pcb = get_cur_PCB();

    // 只对到管道的输出有效
    if(!pcb->out_uid)
        return 0;
    
    struct sysmsg msg;
    msg.type = SYSMSG_TYPE_PIPE_NULL_CHAR;

    struct sysmsg_queue *dst_que;
    while((dst_que = get_out_queue(pcb)) && !send_sysmsg(dst_que, (struct sysmsg *)&msg))
    {
        thread_syscall_protector_entry();
        yield_cpu();
        thread_syscall_protector_exit();
    }

    return 0;
}
