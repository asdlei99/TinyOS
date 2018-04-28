#ifndef TINY_OS_IO_PORT
#define TINY_OS_IO_PORT

#include <shared/intdef.h>

static inline uint8_t _in_byte_from_port(uint16_t port)
{
    uint8_t rt;
    asm volatile ("in %1, %0" : "=a" (rt) : "d" (port));
    return rt;
}

static inline uint16_t _in_double_byte_from_port(uint16_t port)
{
    uint16_t rt;
    asm volatile ("in %1,%0" : "=a" (rt) : "d" (port));
    return rt;
}

static inline void _out_byte_to_port(uint16_t port, uint8_t data)
{
    asm volatile ("out %0, %1" : : "a" (data), "d" (port));
}

static inline void _out_double_byte_to_port(uint16_t port, uint16_t data)
{
    asm volatile ("out %0, %1" : : "a" (data), "d" (port));
}

static inline void _load_IDT(uint64_t arg)
{
    asm volatile ("lidt %0" : : "m" (arg));
}

/*
    返回从低往高第一个不为0的位的位置
    bits不得为0
*/
static inline uint32_t _find_lowest_nonzero_bit(uint32_t bits)
{
    uint32_t rt;
    asm volatile ("bsf %1, %%eax; movl %%eax, %0" : "=r" (rt) : "r" (bits) : "%eax");
    return rt;
}

/*
    返回从低往高最后一个不为0的位的位置
    bits不得为0
*/
static inline uint32_t _find_highest_nonzero_bit(uint32_t bits)
{
    uint32_t rt;
    asm volatile ("bsr %1, %%eax; movl %%eax, %0" : "=r" (rt) : "r" (bits) : "%eax");
    return rt;
}

static inline void _enable_intr(void)
{
    asm volatile ("sti");
}

static inline void _disable_intr(void)
{
    asm volatile ("cli");
}

static inline void _load_cr3(uint32_t val)
{
    asm volatile ("movl %0, %%cr3" : : "r" (val) : "memory");
}

static inline void _refresh_vir_addr_in_TLB(uint32_t vir_addr)
{
    asm volatile ("invlpg %0" : : "m" (vir_addr) : "memory");
}

static inline uint32_t _get_cr2(void)
{
    uint32_t rt;
    asm volatile ("movl %%cr2, %0" : "=r" (rt));
    return rt;
}

static inline uint32_t _get_cr3(void)
{
    uint32_t rt;
    asm volatile ("movl %%cr3, %0" : "=r" (rt));
    return rt;
}

static inline uint32_t _get_eflag(void)
{
    uint32_t rt;
    asm volatile ("pushfl; popl %0" : "=g" (rt));
    return rt;
}

static inline void _load_GDT(uint32_t base, uint32_t size)
{
    uint64_t opr = ((uint64_t)base << 16) | size;
    asm volatile ("lgdt %0" : : "m" (opr));
}

static inline void _ltr(uint16_t sel)
{
    asm volatile ("ltr %w0" : : "r" (sel));
}

#endif /* TINY_OS_IO_PORT */
