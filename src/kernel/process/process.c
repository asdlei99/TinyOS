#include <kernel/asm.h>
#include <kernel/boot.h>
#include <kernel/console/con_buf.h>
#include <kernel/explorer/explorer.h>
#include <kernel/filesys/dpt.h>
#include <kernel/filesys/filesys.h>
#include <kernel/interrupt.h>
#include <kernel/memory.h>
#include <kernel/process/process.h>
#include <kernel/process/thread.h>
#include <kernel/rlist_node_alloc.h>
#include <kernel/seg_desc.h>

#include <shared/freelist.h>
#include <shared/ptrlist.h>
#include <shared/stdint.h>
#include <shared/string.h>
#include <shared/utility.h>

/*=====================================================================
    用户段相关
=====================================================================*/

/* TSS段结构 */
struct TSS
{
    uint32_t BL;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t LDT;
    uint32_t trace;
    uint32_t IO;
};

/*
    TSS段内容
    跟linux类似，整个系统就这一个
*/
static struct TSS tss;

/* 初始化TSS描述符 */
static void init_TSS()
{
    memset((char*)&tss, 0x0, sizeof(struct TSS));
    tss.ss0 = SEG_SEL_KERNEL_STACK;
    tss.IO = sizeof(struct TSS);
    make_iGDT((struct iGDT*)((char*)GDT_START + 3 * 8),
              (uint32_t)&tss, sizeof(struct TSS) - 1, 
              TSS_ATTRIB_LOW, TSS_ATTRIB_HIGH);
}

/* 初始化用户代码段和数据段描述符 */
static void init_user_segments()
{
    make_iGDT((struct iGDT*)((char*)GDT_START + 4 * 8),
              0, 0xbffff,
              GDT_ATTRIB_LOW_CODE, GDT_ATTRIB_HIGH);
    make_iGDT((struct iGDT*)((char*)GDT_START + 5 * 8),
              0, 0xbffff,
              GDT_ATTRIB_LOW_DATA, GDT_ATTRIB_HIGH);
}

/*=====================================================================
    进程本体相关
=====================================================================*/

struct intr_stack_bak
{
    // 最后压入的是中断向量号
    uint32_t intr_number;

    // 通用寄存器
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;

    // 段选择子
    // 虽然应该是16位的，但压栈的结果是32位，只用低16位即可
    uint32_t gs, fs, es, ds;

    // 中断恢复相关，由CPU自动压栈的内容
    uint32_t err_code; // 有的中断有错误码而有的没有，interrupt.s中将其统一为有
    uint32_t eip;      // C标准不保证函数指针能放到uint32_t中，但x86下没问题
    uint32_t cs;
    uint32_t eflags;   // 标志寄存器
    uint32_t esp;
    uint32_t ss;
};

/* 进程列表 */
static ilist processes;

/* PID -> PCB映射 */
struct PCB *pid_to_pcb[MAX_PROCESS_COUNT];

/* 下一个进程uid */
static uint32_t next_uid;

/* PCB空间自由链表 */
static freelist_handle PCB_freelist;

#define FILE_TABLE_ZONE_BYTE_SIZE (4096 / 8)

/* 空闲的文件表空间自由链表 */
static freelist_handle file_table_fl;

/* 申请一块file table zone */
static void *alloc_file_table_zone()
{
    if(is_freelist_empty(&file_table_fl))
    {
        char *data = alloc_ker_page(true);
        uint32_t end = 4096 / FILE_TABLE_ZONE_BYTE_SIZE;
        for(uint32_t i = 0; i < end; ++i)
        {
            add_freelist(&file_table_fl, data);
            data += FILE_TABLE_ZONE_BYTE_SIZE;
        }
    }
    return fetch_freelist(&file_table_fl);
}

static void free_file_table_zone(void *zone)
{
    add_freelist(&file_table_fl, zone);
}

/* 申请一块PCB空间 */
static struct PCB *alloc_PCB()
{
    if(is_freelist_empty(&PCB_freelist))
    {
        struct PCB *new_PCBs = (struct PCB*)alloc_ker_page(true);
        size_t end = 4096 / sizeof(struct PCB);
        for(size_t i = 0;i != end; ++i)
            add_freelist(&PCB_freelist, &new_PCBs[i]);
    }
    struct PCB *ret = fetch_freelist(&PCB_freelist);

    init_atrc(&ret->file_table, ATRC_ELEM_SIZE(struct pcb_file_record),
              alloc_file_table_zone(), FILE_TABLE_ZONE_BYTE_SIZE);
    init_spinlock(&ret->file_table_lock);

    ret->pis      = pis_background;
    ret->disp_buf = NULL;
    ret->uid      = next_uid++;

    ret->out_pid = 0;
    ret->out_uid = 0;

    return ret;
}

/*
    用一个已有的虚拟地址空间创建一个不包含任何线程的进程记录
    进程不包含任何线程是invalid的，所以这么创建出的进程记录是不完整的
*/
static struct PCB *create_empty_process_with_addr_space(
        const char *name, vir_addr_space *addr_space, bool is_PL_0)
{
    struct PCB *pcb = alloc_PCB();

    // 虚拟地址空间初始化

    pcb->addr_space = addr_space;
    pcb->pid = get_usr_vir_addr_idx(pcb->addr_space) + 1;
    pcb->is_PL_0 = is_PL_0;
    pcb->addr_space_inited = false;
    pcb->sysmsg_blocked_tcb = NULL;

    pid_to_pcb[pcb->pid] = pcb;

    init_ilist(&pcb->threads_list);
    init_sysmsg_queue(&pcb->sys_msgs);
    init_sysmsg_sources_list(&pcb->sys_msg_srcs);

    // 名字复制
    strcpy_s(pcb->name, name, PROCESS_NAME_MAX_LENGTH + 1);

    push_back_ilist(&processes, &pcb->processes_node);
    
    return pcb;
}

/* 创建一个进程记录，但是不包含任何线程 */
static struct PCB *create_empty_process(const char *name, bool is_PL_0)
{
    return create_empty_process_with_addr_space(
                name, create_vir_addr_space(), is_PL_0);
}

/*
    此函数被调用时一定在用户虚拟地址空间中
    在最低地址的位图中查找一个空闲的用户栈空间
*/
static void *alloc_thread_user_stack()
{
    uint32_t *usr_bmps = (uint32_t*)USER_STACK_BITMAP_ADDR;
    for(size_t i = 0;i != USER_THREAD_STACK_BITMAP_COUNT; ++i)
    {
        if(!usr_bmps[i])
            continue;
        uint32_t local_idx = find_lowest_nonzero_bit(usr_bmps[i]);
        usr_bmps[i] &= ~(1 << local_idx);
        return (void*)((uint32_t)USER_STACK_TOP_ADDR -
            (uint32_t)(((i << 5) + local_idx) * ((uint32_t)USER_STACK_SIZE) + 4));
    }
    return NULL;
}

/* 初始化进程地址空间 */
static void init_process_addr_space()
{
    // 填充线程栈位图
    for(size_t i = 0;i != USER_THREAD_STACK_BITMAP_COUNT; ++i)
        ((uint32_t*)USER_STACK_BITMAP_ADDR)[i] = 0xffffffff;
}

/* 进程入口 */
void process_thread_entry(process_exec_func func,
                          uint32_t data_seg_sel, uint32_t code_seg_sel,
                          uint32_t stack_seg_sel, bool is_PL_0)
{
    // 这里直接关中断
    // 等会儿伪装的中断退出函数那里，eflags中IF是打开的
    _disable_intr();

    struct TCB *tcb = get_cur_TCB();
    struct PCB *pcb = tcb->pcb;

    // 初始化进程地址空间
    if(!pcb->addr_space_inited)
    {
        init_process_addr_space();
        pcb->addr_space_inited = true;
    }

    // 填充内核栈最高处的intr_stack，为降低特权级做准备

    // 注意到这里并没有把内核进程的调用栈初始化到0~3GB中
    // 这是因为内核进程被调度切换时，调用栈不会自动发生变化
    // 如果栈在0~3GB中，那么进程虚拟地址空间的切换，会导致
    // 调用栈无效化，当场去世

    tcb->ker_stack += sizeof(struct thread_init_stack);
    struct intr_stack_bak *intr_stack =
        (struct intr_stack_bak*)tcb->ker_stack;

    intr_stack->edi = 0;
    intr_stack->esi = 0;
    intr_stack->ebp = 0;
    intr_stack->esp_dummy = 0;
    intr_stack->ebx = 0;
    intr_stack->edx = 0;
    intr_stack->ecx = 0;
    intr_stack->eax = 0;
    intr_stack->gs = data_seg_sel;
    intr_stack->ds = data_seg_sel;
    intr_stack->es = data_seg_sel;
    intr_stack->fs = data_seg_sel;
    intr_stack->eip = (uint32_t)func;
    intr_stack->cs = code_seg_sel;
    intr_stack->eflags = (1 << 1) | (1 << 9);

    if(!is_PL_0)
    {
        intr_stack->esp = (uint32_t)alloc_thread_user_stack();
        intr_stack->ss = stack_seg_sel;
    }

    extern void intr_proc_end(); // defined in interrupt.s
    
    asm volatile ("movl %0, %%esp;"
                  "jmp intr_proc_end"
                  :
                  : "g" (intr_stack)
                  : "memory");
}

static void process_thread_entry_PL_0(process_exec_func func)
{
    process_thread_entry(func,
                         SEG_SEL_KERNEL_DATA, SEG_SEL_KERNEL_CODE,
                         SEG_SEL_KERNEL_STACK, true);
}

static void process_thread_entry_PL_3(process_exec_func func)
{
    process_thread_entry(func,
                         SEG_SEL_USER_DATA, SEG_SEL_USER_CODE,
                         SEG_SEL_USER_STACK, false);
}

/* 把bootloader以来一直在跑的东西封装成进程 */
static void init_bootloader_process()
{
    struct PCB *pcb = alloc_PCB();
    struct TCB *tcb = get_cur_TCB();

    pcb->pis = pis_foreground;

    strcpy(pcb->name, "kernel process");

    pcb->addr_space = get_ker_vir_addr_space();
    pcb->addr_space_inited = true;

    pcb->pid = 0;
    pcb->is_PL_0 = true;
    pcb->sysmsg_blocked_tcb = NULL;

    pid_to_pcb[0] = pcb;

    init_sysmsg_queue(&pcb->sys_msgs);
    init_sysmsg_sources_list(&pcb->sys_msg_srcs);

    init_ilist(&pcb->threads_list);

    push_back_ilist(&pcb->threads_list, &tcb->threads_in_proc_node);
        
    tcb->pcb = pcb;
}

/* 对GP异常等应用程序的锅造成的中断，把该进程干掉就好了 */
static void intr_kill_proc(uint32_t intr_number)
{
    kill_process(get_cur_PCB());
    while(true)
        ;
}

void init_process_man()
{
    for(size_t i = 0;i != MAX_PROCESS_COUNT; ++i)
        pid_to_pcb[i] = NULL;
    
    next_uid = 1;

    init_TSS();
    init_user_segments();

    _load_GDT((uint32_t)GDT_START, 8 * 6 - 1);
    _ltr(TSS_SEL);

    init_ilist(&processes);
    init_freelist(&PCB_freelist);

    init_bootloader_process();

    set_intr_function(INTR_NUMBER_DIVIDE_ERROR        , intr_kill_proc);
    set_intr_function(INTR_NUMBER_DEBUG               , intr_kill_proc);
    set_intr_function(INTR_NUMBER_BREAKPOINT          , intr_kill_proc);
    set_intr_function(INTR_NUMBER_INT_OVERFLOW        , intr_kill_proc);
    set_intr_function(INTR_NUMBER_BOUND_OUT           , intr_kill_proc);
    set_intr_function(INTR_NUMBER_UNDEFINED_INSTR     , intr_kill_proc);
    set_intr_function(INTR_NUMBER_DOUBLE_FAULT        , intr_kill_proc);
    set_intr_function(INTR_NUMBER_NO_FLOAT_PROC       , intr_kill_proc);
    set_intr_function(INTR_NUMBER_SEG_NOT_P           , intr_kill_proc);
    set_intr_function(INTR_NUMBER_STK_SEG_FAULT       , intr_kill_proc);
    set_intr_function(INTR_NUMBER_GENERAL_PROTECTION  , intr_kill_proc);
    set_intr_function(INTR_NUMBER_FLOAT_ERROR         , intr_kill_proc);
    set_intr_function(INTR_NUMBER_ALIGNMENT_CHECK     , intr_kill_proc);
    set_intr_function(INTR_NUMBER_MACHINE_CHECK       , intr_kill_proc);
    set_intr_function(INTR_NUMBER_SIMD_FLOAT_EXCEPTION, intr_kill_proc);
}

uint32_t create_process(const char *name, process_exec_func func, bool is_PL_0)
{
    intr_state intr_s = fetch_and_disable_intr();

    struct PCB *pcb = create_empty_process(name, is_PL_0);

    thread_exec_func thread_entry = (thread_exec_func)(is_PL_0 ? process_thread_entry_PL_0 :
                                                                 process_thread_entry_PL_3);

    struct TCB *tcb = create_thread(thread_entry, func, pcb);
    push_back_ilist(&pcb->threads_list, &tcb->threads_in_proc_node);

    uint32_t ret = pcb->pid;
    set_intr_state(intr_s);
    return ret;
}

uint32_t create_process_with_addr_space(const char *name, process_exec_func func,
                                    vir_addr_space *addr_space, bool is_PL_0)
{
    intr_state intr_s = fetch_and_disable_intr();

    struct PCB *pcb = create_empty_process_with_addr_space(name, addr_space, is_PL_0);

    thread_exec_func thread_entry = (thread_exec_func)(is_PL_0 ? process_thread_entry_PL_0 :
                                                                 process_thread_entry_PL_3);

    struct TCB *tcb = create_thread(thread_entry, func, pcb);
    push_back_ilist(&pcb->threads_list, &tcb->threads_in_proc_node);

    uint32_t ret = pcb->pid;
    set_intr_state(intr_s);
    return ret;
}

void add_proc_thread(process_exec_func func)
{
    intr_state is = fetch_and_disable_intr();

    struct PCB *pcb = get_cur_PCB();

    thread_exec_func thread_entry =
        (thread_exec_func)(pcb->is_PL_0 ? process_thread_entry_PL_0 :
                                          process_thread_entry_PL_3);

    struct TCB *tcb = create_thread(thread_entry, func, pcb);
    push_back_ilist(&pcb->threads_list, &tcb->threads_in_proc_node);

    set_intr_state(is);
}

void kill_process(struct PCB *pcb)
{
    intr_state intr_s = fetch_and_disable_intr();
    struct TCB *this = get_cur_TCB();
    bool kill_this = false;

    while(!is_ilist_empty(&pcb->threads_list))
    {
        struct TCB *tcb = GET_STRUCT_FROM_MEMBER(
            struct TCB, threads_in_proc_node, pop_front_ilist(&pcb->threads_list));
        if(tcb != this)
        {
            if(!kill_thread(tcb))
                yield_CPU(); // 延时销毁依赖于线程调度，故主动让出CPU，否则会在这里死循环
        }
        else
            kill_this = true;
    }

    if(kill_this)
        kill_thread(this);

    set_intr_state(intr_s);
}

void kill_all_processes()
{
    intr_state is = fetch_and_disable_intr();

    while(!is_ilist_empty(&processes))
    {
        struct ilist_node *node = processes.next;
        struct PCB *pcb = GET_STRUCT_FROM_MEMBER(
            struct PCB, processes_node, node);
        kill_process(pcb);
    }

    set_intr_state(is);
}

struct PCB *get_cur_PCB()
{
    return get_cur_TCB()->pcb;
}

ilist *get_all_processes()
{
    return &processes;
}

struct PCB *get_PCB_by_pid(uint32_t pid)
{
    if(pid >= MAX_PROCESS_COUNT)
        return NULL;
    return pid_to_pcb[pid];
}

void _set_tss_esp0(uint32_t esp0)
{
    tss.esp0 = esp0;
}

void release_process_resources(struct PCB *pcb)
{
    intr_state is = fetch_and_disable_intr();

    if(pcb->pis == pis_foreground)
        foreground_exit(pcb);

    pid_to_pcb[pcb->pid] = NULL;
    erase_from_ilist(&pcb->processes_node);
    destroy_sysmsg_queue(&pcb->sys_msgs);
    destroy_sysmsg_source_list(&pcb->sys_msg_srcs);

    if(pcb->disp_buf)
        free_con_buf(pcb->disp_buf);

    // 关闭所有文件
    for(atrc_elem_handle i = atrc_begin(&pcb->file_table,
                                        ATRC_ELEM_SIZE(struct pcb_file_record));
        i != atrc_end();
        i = atrc_next(&pcb->file_table,
                      ATRC_ELEM_SIZE(struct pcb_file_record),
                      i))
    {
        struct pcb_file_record *rcd =
            (struct pcb_file_record*)get_atrc_unit(
                        &pcb->file_table,
                        ATRC_ELEM_SIZE(struct pcb_file_record),
                        i);
        kclose_file(rcd->dp, rcd->file);
    }
    free_file_table_zone(pcb->file_table.data);

    set_intr_state(is);
}

void release_PCB(struct PCB *pcb)
{
    destroy_vir_addr_space(pcb->addr_space);
    add_freelist(&PCB_freelist, pcb);
}

uint32_t syscall_get_cur_PID_impl()
{
    return get_cur_TCB()->pcb->pid;
}

uint32_t syscall_yield_CPU_impl()
{
    yield_CPU();
    return 0;
}

uint32_t syscall_thread_exit_impl()
{
    kexit_thread();
    return 0;
}

uint32_t syscall_new_thread_impl(void (*entry)())
{
    if((uint32_t)entry >= KER_ADDR_BEGIN)
        return false;

    struct PCB *pcb = get_cur_PCB();
    thread_exec_func thread_entry = (thread_exec_func)
        (pcb->is_PL_0 ? process_thread_entry_PL_0 :
                        process_thread_entry_PL_3);
    struct TCB *tcb = create_thread(thread_entry, entry, pcb);
    push_back_ilist(&pcb->threads_list, &tcb->threads_in_proc_node);

    return true;
}
