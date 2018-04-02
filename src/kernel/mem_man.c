#include <kernel/boot.h>
#include <kernel/mem_man.h>
#include <kernel/print.h>

size_t total_mem_bytes;

void *static_kernel_mem_top;

static struct mem_page_pool *phy_mem_page_pool;

// return ceil(a / b)
static inline size_t ceil_int_div(size_t a, size_t b)
{
    return (a / b) + (a % b ? 1 : 0);
}

static inline void set_bitmap32(uint32_t *bms, size_t bm_count)
{
    for(size_t i = 0;i != bm_count; ++i)
        bms[i] = 0xffffffff;
}

static inline void clr_bitmap32(uint32_t *bms, size_t bm_count)
{
    for(size_t i = 0;i != bm_count; ++i)
        bms[i] = 0x00000000;
}

static inline void set_bit(uint32_t *bms, size_t i)
{
    size_t arr_idx = i / 32;
    size_t bmp_idx = i % 32;
    bms[arr_idx] |= (1 << bmp_idx);
}

static inline void clr_bit(uint32_t *bms, size_t i)
{
    size_t arr_idx = i / 32;
    size_t bmp_idx = i % 32;
    bms[arr_idx] &= ~(1 << bmp_idx);
}

static bool init_ker_phy_mem_pool(void)
{
    //物理内存的低KERNEL_RESERVED_PHY_MEM_END字节由内核保留使用
    //bootloader又花了两页，分别作为内核页目录和0号页表
    size_t pool_begin = (KERNEL_RESERVED_PHY_MEM_END / 4096 + 2);
    size_t pool_end   = get_mem_total_bytes() / 4096;
    
    //没有空闲物理页，这时候是不是应该assert挂掉……
    if(pool_begin >= pool_end)
    {
        phy_mem_page_pool = NULL;
        return false;
    }

    //页面总数和位图数量
    size_t pool_pages = pool_end - pool_begin;
    size_t count_L3 = ceil_int_div(pool_pages, 32);
    size_t count_L2 = ceil_int_div(count_L3, 32);
    size_t count_L1 = ceil_int_div(count_L2, 32);

    //需要的总bitmap32数量
    // L1 + L2 + L3 + stay (stay与L3相同)
    size_t total_bitmap32_count = count_L1 + count_L2 + (count_L3 << 1);

    //结构体字节数
    size_t struct_size = sizeof(struct mem_page_pool)
                       + sizeof(uint32_t) * total_bitmap32_count;
    
    struct mem_page_pool *pool = phy_mem_page_pool =
                alloc_static_kernel_mem(struct_size, 4);
    
    pool->struct_size = struct_size;
    pool->begin       = pool_begin;
    pool->end         = pool_end;
    pool->pool_pages  = pool_pages;
    
    pool->bitmap_count_L1 = count_L1;
    pool->bitmap_count_L2 = count_L2;
    pool->bitmap_count_L3 = count_L3;

    //bitmap_data区域分割：
    //    L1 L2 L3 stay
    pool->bitmap_L1       = pool->bitmap_data;
    pool->bitmap_L2       = pool->bitmap_L1 + count_L1;
    pool->bitmap_L3       = pool->bitmap_L2 + count_L2;
    pool->bitmap_resident = pool->bitmap_L3 + count_L3;

    //初始化物理页分配情况
    //之前bootloader用了的都已经被排除在这个池子外了
    //所以都初始化成未使用就行
    set_bitmap32(&pool->bitmap_L0, 1);
    set_bitmap32(pool->bitmap_L1, pool->bitmap_count_L1);
    set_bitmap32(pool->bitmap_L2, pool->bitmap_count_L2);
    set_bitmap32(pool->bitmap_L3, pool->bitmap_count_L3);
    
    //resident位图其实初步初始化无所谓……
    clr_bitmap32(pool->bitmap_resident, pool->bitmap_count_L3);

    return true;
}

void init_mem_man(void)
{
    // 内存总量
    total_mem_bytes = *(size_t*)TOTAL_MEMORY_SIZE_ADDR;

    static_kernel_mem_top = (void*)0xc0100000;
    
    if(!init_ker_phy_mem_pool())
    {
        put_str("Fatal error: failed to initialize physical memory pool");
        while(true);
    }
}

size_t get_mem_total_bytes(void)
{
    return total_mem_bytes;
}

void *alloc_static_kernel_mem(size_t bytes, size_t align_bytes)
{
    static_kernel_mem_top += (size_t)static_kernel_mem_top % align_bytes;
    void *rt = static_kernel_mem_top;
    static_kernel_mem_top = (char*)static_kernel_mem_top + bytes;
    return rt;
}
