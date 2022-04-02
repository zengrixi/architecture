
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>

/*
由于指针是4的倍数,那么后两位一定为0,此时我们可以利用指针的后两位做标记,充分利用空间.
在nginx的slab中,我们使用ngx_slab_page_s结构体中的指针pre的后两位做标记,用于指示该page页面的slot块数与ngx_slab_exact_size的关系.
当page划分的slot块小于32时候,pre的后两位为NGX_SLAB_SMALL.
当page划分的slot块等于32时候,pre的后两位为NGX_SLAB_EXACT
当page划分的slot大于32块时候,pre的后两位为NGX_SLAB_BIG
当page页面不划分slot时候,即将整个页面分配给用户,pre的后两位为NGX_SLAB_PAGE
*/
#define NGX_SLAB_PAGE_MASK   3
#define NGX_SLAB_PAGE        0
#define NGX_SLAB_BIG         1
#define NGX_SLAB_EXACT       2
#define NGX_SLAB_SMALL       3

#if (NGX_PTR_SIZE == 4)

#define NGX_SLAB_PAGE_FREE   0
//标记这是连续分配多个page，并且我不是首page，例如一次分配3个page,分配的page为[1-3]，则page[1].slab=3  page[2].slab=page[3].slab=NGX_SLAB_PAGE_BUSY记录
#define NGX_SLAB_PAGE_BUSY   0xffffffff
#define NGX_SLAB_PAGE_START  0x80000000

#define NGX_SLAB_SHIFT_MASK  0x0000000f
#define NGX_SLAB_MAP_MASK    0xffff0000
#define NGX_SLAB_MAP_SHIFT   16

#define NGX_SLAB_BUSY        0xffffffff

#else /* (NGX_PTR_SIZE == 8) */

#define NGX_SLAB_PAGE_FREE   0
#define NGX_SLAB_PAGE_BUSY   0xffffffffffffffff
#define NGX_SLAB_PAGE_START  0x8000000000000000

#define NGX_SLAB_SHIFT_MASK  0x000000000000000f
#define NGX_SLAB_MAP_MASK    0xffffffff00000000
#define NGX_SLAB_MAP_SHIFT   32

#define NGX_SLAB_BUSY        0xffffffffffffffff

#endif


#if (NGX_DEBUG_MALLOC)

#define ngx_slab_junk(p, size)     ngx_memset(p, 0xA5, size)

#elif (NGX_HAVE_DEBUG_MALLOC)

#define ngx_slab_junk(p, size)                                                \
    if (ngx_debug_malloc)          ngx_memset(p, 0xA5, size)

#else

#define ngx_slab_junk(p, size)

#endif

static ngx_slab_page_t *ngx_slab_alloc_pages(ngx_slab_pool_t *pool,
    ngx_uint_t pages);
static void ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
    ngx_uint_t pages);
static void ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level,
    char *text);

//slab页面的大小,32位Linux中为4k,  
static ngx_uint_t  ngx_slab_max_size;//设置ngx_slab_max_size = 2048B。如果一个页要存放多个obj，则obj size要小于这个数值 
/*
由于指针是4的倍数,那么后两位一定为0,此时我们可以利用指针的后两位做标记,充分利用空间.
在nginx的slab中,我们使用ngx_slab_page_s结构体中的指针pre的后两位做标记,用于指示该page页面的slot块数与ngx_slab_exact_size的关系.
当page划分的slot块小于32时候,pre的后两位为NGX_SLAB_SMALL.
当page划分的slot块等于32时候,pre的后两位为NGX_SLAB_EXACT
当page划分的slot大于32块时候,pre的后两位为NGX_SLAB_BIG
当page页面不划分slot时候,即将整个页面分配给用户,pre的后两位为NGX_SLAB_PAGE
*/ //划分32个slot块,每个slot的大小就是ngx_slab_exact_size  
static ngx_uint_t  ngx_slab_exact_size;//设置ngx_slab_exact_size = 128B。分界是否要在缓存区分配额外空间给bitmap  
static ngx_uint_t  ngx_slab_exact_shift;//ngx_slab_exact_shift = 7，即128的位表示 //每个slot块大小的位移是ngx_slab_exact_shift  

/*
注意，在ngx_slab_pool_t里面有两种类型的slab page，虽然都是ngx_slab_page_t定义的结构，但是功能不尽相同。一种是slots，用来表示存
放较小obj的内存块(如果页大小是 4096B，则是<2048B的obj，即小于1/2页)，另一种来表示所要分配的空间在缓存区的位置。Nginx把缓存obj分
成大的 (>=2048B)和小的(<2048B)。每次给大的obj分配一整个页，而把多个小obj存放在一个页中间，用bitmap等方法来表示 其占用情况。而小
的obj又分为3种：小于128B，等于128B，大于128B且小于2048B。其中小于128B的obj需要在实际缓冲区额外分配 bitmap空间来表示内存使用情况
(因为slab成员只有4个byte即32bit，一个缓存页4KB可以存放的obj超过32个，所以不能用slab 来表示)，这样会造成一定的空间损失。等于或大
于128B的obj因为可以用一个32bit的整形来表示其状态，所以就可以直接用slab成员。每次分配 的空间是2^n，最小是8byte，8，16，32，64，
128，256，512，1024，2048。小于2^i且大于2^(i-1)的obj会被分 配一个2^i的空间，比如56byte的obj就会分配一个64byte的空间。
*/ //http://adam281412.blog.163.com/blog/static/33700067201111283235134/
/*
共享内存的其实地址开始处数据:ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t)(slots_m[]) + pages * sizeof(ngx_slab_page_t)(pages_m[]) +pages*ngx_pagesize(这是实际的数据部分，
每个ngx_pagesize都由前面的一个ngx_slab_page_t进行管理，并且每个ngx_pagesize最前端第一个obj存放的是一个或者多个int类型bitmap，用于管理每块分配出去的内存)
*/

    //图形化理解参考:http://blog.csdn.net/u013009575/article/details/17743261
void
ngx_slab_init(ngx_slab_pool_t *pool)//pool指向的是整个共享内存空间的起始地址   slab结构是配合共享内存使用的 可以以limit req模块为例，参考ngx_http_limit_req_module
{
    u_char           *p;
    size_t            size;
    ngx_int_t         m;
    ngx_uint_t        i, n, pages;
    ngx_slab_page_t  *slots;

    /*
     //假设每个page是4KB  
    //设置ngx_slab_max_size = 2048B。如果一个页要存放多个obj，则obj size要小于这个数值  
    //设置ngx_slab_exact_size = 128B。分界是否要在缓存区分配额外空间给bitmap  
    //ngx_slab_exact_shift = 7，即128的位表示  
     */
    /* STUB */
    if (ngx_slab_max_size == 0) {
        ngx_slab_max_size = ngx_pagesize / 2;
        ngx_slab_exact_size = ngx_pagesize / (8 * sizeof(uintptr_t));
        for (n = ngx_slab_exact_size; n >>= 1; ngx_slab_exact_shift++) {
            /* void */
        }
    }
    /**/

    pool->min_size = 1 << pool->min_shift; //最小分配的空间是8byte  

    p = (u_char *) pool + sizeof(ngx_slab_pool_t); //共享内存前面的sizeof(ngx_slab_pool_t)是给slab poll的
    size = pool->end - p; //size是总的共享内存 - sizeof(ngx_slab_pool_t)字节后的长度

    ngx_slab_junk(p, size);

    slots = (ngx_slab_page_t *) p;
    n = ngx_pagesize_shift - pool->min_shift;//12-3=9

/*
这些slab page是给大小为8，16，32，64，128，256，512，1024，2048byte的内存块 这些slab page的位置是在pool->pages的前面初始化  
共享内存的其实地址开始处数据:ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t)(slots_m[]) + pages * sizeof(ngx_slab_page_t)(pages_m[]) +pages*ngx_pagesize(这是实际的数据部分，
每个ngx_pagesize都由前面的一个ngx_slab_page_t进行管理，并且每个ngx_pagesize最前端第一个obj存放的是一个或者多个int类型bitmap，用于管理每块分配出去的内存)
*/
    for (i = 0; i < n; i++) { //这9个slots[]由9 * sizeof(ngx_slab_page_t)指向
        slots[i].slab = 0;
        slots[i].next = &slots[i];
        slots[i].prev = 0;
    }

    p += n * sizeof(ngx_slab_page_t); //跳过上面那些slab page  

    //**计算这个空间总共可以分配的缓存页(4KB)的数量，每个页的overhead是一个slab page的大小  
    //**这儿的overhead还不包括之后给<128B物体分配的bitmap的损耗  

    //这里 + sizeof(ngx_slab_page_t)的原因是每个ngx_pagesize都有对应的ngx_slab_page_t进行管理
    pages = (ngx_uint_t) (size / (ngx_pagesize + sizeof(ngx_slab_page_t))); //这里的size是不是应该把头部n * sizeof(ngx_slab_page_t)减去后在做计算更加准确?
    //把每个缓存页最前面的sizeof(ngx_slab_page_t)字节对应的slab page归0  
    ngx_memzero(p, pages * sizeof(ngx_slab_page_t));

    pool->pages = (ngx_slab_page_t *) p;

    //初始化free，free.next是下次分配页时候的入口  
    pool->free.prev = 0;
    pool->free.next = (ngx_slab_page_t *) p;

    //更新第一个slab page的状态，这儿slab成员记录了整个缓存区的页数目  
    pool->pages->slab = pages; //第一个pages->slab指定了共享内存中除去头部外剩余页的个数
    pool->pages->next = &pool->free;
    pool->pages->prev = (uintptr_t) &pool->free;

    //实际缓存区(页)的开头，对齐   //因为对齐的原因,使得m_page数组和数据区域之间可能有些内存无法使用,  
    pool->start = (u_char *)
                  ngx_align_ptr((uintptr_t) p + pages * sizeof(ngx_slab_page_t),
                                 ngx_pagesize);

    //根据实际缓存区的开始和结尾再次更新内存页的数目  
    //由于内存对齐操作(pool->start处内存对齐),可能导致pages减少,  
    //所以要调整.m为调整后page页面的减小量.  
    //其实下面几行代码就相当于:  
    // pages =(pool->end - pool->start) / ngx_pagesize  
    //pool->pages->slab = pages;  
    m = pages - (pool->end - pool->start) / ngx_pagesize;
    if (m > 0) {
        pages -= m;
        pool->pages->slab = pages;
    }

    //跳过pages * sizeof(ngx_slab_page_t)，也就是指向实际的数据页pages*ngx_pagesize
    pool->last = pool->pages + pages;

    pool->log_nomem = 1;
    pool->log_ctx = &pool->zero;
    pool->zero = '\0';
}

//由于是共享内存，所以在进程间需要用锁来保持同步
void *
ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = ngx_slab_alloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}

/*
对于给定size,从slab_pool中分配内存.
1.如果size大于等于一页,那么从m_page中查找,如果有则直接返回,否则失败.
2.如果size小于一页,如果链表中有空余slot块.
     (1).如果size大于ngx_slab_exact_size,
a.设置slab的高16位(32位系统)存放solt对应的map,并且该对应为map的地位对应page中高位的slot块.例如1110对应为第1块slot是可用的,2-4块不可用.slab的低16为存储slot块大小的位移.
b.设置m_page元素的pre指针为NGX_SLAB_BIG.
c.如果page的全部slot都被使用了,那么将此页面从m_slot数组元素的链表中移除.
   (2).如果size等于ngx_slab_exact_size
a.设置slab存储slot的map,同样slab中的低位对应高位置的slot.
b.设置m_page元素的pre指针为NGX_SLAB_EXACT.
c.如果page的全部slot都被使用了,那么将此页面从m_slot数组元素的链表中移除.
   (3).如果size小于ngx_slab_exact_size
a.用page中的前几个slot存放slot的map,同样低位对应高位.
b.设置m_page元素的pre指针为NGX_SLAB_SMALL.
b.如果page的全部slot都被使用了,那么将此页面从m_slot数组元素的链表中移除.
 3.如果链表中没有空余的slot块,则在free链表中找到一个空闲的页面分配给m_slot数组元素中的链表.
   (1).如果size大于ngx_slab_exact_size,
a.设置slab的高16位(32位系统)存放solt对应的map,并且该对应为map的地位对应page中高位的slot块.slab的低16为存储slot块大小的位移.
b.设置m_page元素的pre指针为NGX_SLAB_BIG.
c.将分配的页面链入m_slot数组元素的链表中.
   (2).如果size等于ngx_slab_exact_size
a.设置slab存储slot的map,同样slab中的低位对应高位置的slot.
b.设置m_page元素的pre指针为NGX_SLAB_EXACT.
c.将分配的页面链入m_slot数组元素的链表中.
   (3).如果size小于ngx_slab_exact_size
a.用page中的前几个slot存放slot的map,同样低位对应高位.
b.设置m_page元素的pre指针为NGX_SLAB_SMALL.
c.将分配的页面链入m_slot数组元素的链表中.
4.成功则返回分配的内存块,即指针p,否则失败,返回null.

*/
//图形化理解参考:http://blog.csdn.net/u013009575/article/details/17743261
//返回的值是所要分配的空间在内存缓存区的位置  

/*
在一个page页中获取小与2048的obj块的时候，都会设置page->next = &slots[slot]; page->prev = &slots[slot]，slots[slot].next = page;也就是作为obj块的页
page[]都会和对应的slots[]关联，如果是分配大于2048的空间，则会分配整个页，其page[]和slots就没有关系
当该page页用完后，则会重新把page[]的next和prev置为NULL，同时把对应的slot[]的next和prev指向slot[]本身
当page用完后释放其中一个obj后，有恢复为page->next = &slots[slot]; page->prev = &slots[slot]，slots[slot].next = page;
*/
void * 
ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size)
{ //这儿假设page_size是4KB  
    size_t            s;
    uintptr_t         p, n, m, mask, *bitmap;
    ngx_uint_t        i, slot, shift, map;
    ngx_slab_page_t  *page, *prev, *slots;

    if (size > ngx_slab_max_size) { //如果是large obj, size >= 2048B  

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                       "slab alloc: %uz", size);

        //分配1个或多个内存页  
        page = ngx_slab_alloc_pages(pool, (size >> ngx_pagesize_shift)
                                          + ((size % ngx_pagesize) ? 1 : 0)); //例如size刚好是4K,则page=1,如果是4K+1，则page=2
        if (page) {
            //获得page向对于page[0]的偏移量由于m_page和page数组是相互对应的,即m_page[0]管理page[0]页面,m_page[1]管理page[1]页面.  
            //所以获得page相对于m_page[0]的偏移量就可以根据start得到相应页面的偏移量.  
            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t) pool->start; //得到实际分配的页的起始地址

        } else {
            p = 0;
        }

        goto done;
    }

    //较小的obj, size < 2048B根据需要分配的size来确定在slots的位置，每个slot存放一种大小的obj的集合，如slots[0]表示8byte的空间，
    //slots[3]表示64byte的空间如果obj过小(<1B)，slot的位置是1B空间的位置，即最小分配1B  
    if (size > pool->min_size) { //计算使用哪个slots[]，也就是需要分配的空间是多少  例如size=9,则会使用slot[1]，也就是16字节
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) { /* void */ }
        slot = shift - pool->min_shift;

    } else {
        size = pool->min_size;
        shift = pool->min_shift;
        slot = 0;
    }

    //ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t) + pages * sizeof(ngx_slab_page_t) +pages*ngx_pagesize(这是实际的数据部分)
    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %uz slot: %ui", size, slot);
                   
    //指向9 * sizeof(ngx_slab_page_t) ，也就是slots[0-8]数组 = 8 - 2048
    slots = (ngx_slab_page_t *) ((u_char *) pool + sizeof(ngx_slab_pool_t));
    //在ngx_slab_init中slots[]->next默认是指向自己的
    page = slots[slot].next;//根据m_slot[slot]获得相应的m_page元素,然后找到相应页面.  

    if (page->next != page) { //如果之前已经有此类大小obj且那个已经分配的内存缓存页还未满  9 个ngx_slab_page_t都还没有用完

        if (shift < ngx_slab_exact_shift) { //小obj，size < 128B，更新内存缓存页中的bitmap，并返回待分配的空间在缓存的位置  

            do {
                p = (page - pool->pages) << ngx_pagesize_shift; 
                bitmap = (uintptr_t *) (pool->start + p);//pool->start开始的128字节为需要分配的空间，其后紧跟一个int 4字节空间用于bitmap

                /*  
               例如要分配的size为54字节，则在前面计算出的shift对应的字节数应该是64字节，由于一个页面全是64字节obj大小，所以一共有64
               个64字节的obj，64个obj至少需要64位来表示每个obj是否已使用，因此至少需要64位(也就是8字节，2个int),所以至少要暂用一个64
               字节obj来存储该bitmap信息，第一个64字节obj实际上只用了8字节，其他56字节未用
               */
                //计算需要多少个字节来存储bitmap  
                map = (1 << (ngx_pagesize_shift - shift))
                          / (sizeof(uintptr_t) * 8);

                for (n = 0; n < map; n++) {

                    if (bitmap[n] != NGX_SLAB_BUSY) {//如果page的obj块空闲,也就是bitmap指向的32个obj是否都已经被分配出去了  
                        //如果整个page页的所有slab已经用完，则会在后面的ngx_slab_alloc_pages从新获取空间并划分slab，然后分配
                        for (m = 1, i = 0; m; m <<= 1, i++) {//如果obj块被占用,继续查找  从这32个obj中找出第一个未被分配出去的obj
                            if ((bitmap[n] & m)) {
                                continue;
                            }

                            //找到了，该bitmap对应的第n个(注意是位移操作后的m)未使用，使用他，同时置位该位，表示该bitmp[n]已经不能再被分配，因为已经本次分配出去了
                            bitmap[n] |= m;

                            i = ((n * sizeof(uintptr_t) * 8) << shift)
                                + (i << shift); //该bit所处的整个bitmap中的第几位(例如需要3个bitmap表示所有的slab块，则现在是第三个bitmap的第一位，则i=32+32+1-1,bit从0开始)

                            if (bitmap[n] == NGX_SLAB_BUSY) { //如果该32位图在这次取到最后第31位(0-31)的时候，前面的bitmap[n] |= m后;使其刚好NGX_SLAB_BUSY，也就是位图填满
                                for (n = n + 1; n < map; n++) {
                                     if (bitmap[n] != NGX_SLAB_BUSY) {//如果该bitmap后面的几个bitmap还没有用完，则直接返回该bitmap地址
                                         p = (uintptr_t) bitmap + i;

                                         goto done;
                                     }
                                }
                                //& ~NGX_SLAB_PAGE_MASK这个的原因是需要恢复原来的地址，因为低两位在第一次获取空间的时候，做了特殊意义处理
                                prev = (ngx_slab_page_t *)
                                            (page->prev & ~NGX_SLAB_PAGE_MASK); //也就是该obj对应的slot_m[]

                                //pages_m[i]和slot_m[i]取消对应的引用关系，因为该pages_m[i]指向的页page已经用完了
                                prev->next = page->next;
                                page->next->prev = page->prev; //slot_m[i]结构的next和prev指向自己

                                page->next = NULL; //page的next和prev指向NULL，表示不再可用来分配slot[]对应的obj
                                page->prev = NGX_SLAB_SMALL;
                            }

                            p = (uintptr_t) bitmap + i;

                            goto done;
                        }
                    }
                }

                page = page->next;

            } while (page);

        } else if (shift == ngx_slab_exact_shift) {
            //size == 128B，因为一个页可以放32个，用slab page的slab成员来标注每块内存的占用情况，不需要另外在内存缓存区分配bitmap，
            //并返回待分配的空间在缓存的位置  

            do {
                if (page->slab != NGX_SLAB_BUSY) { //该page是否已经用完

                    for (m = 1, i = 0; m; m <<= 1, i++) { //如果整个page页的所有slab已经用完，则会在后面的ngx_slab_alloc_pages从新获取空间并划分slab，然后分配
                        if ((page->slab & m)) {
                            continue;
                        }

                        page->slab |= m; //标记第m个slab现在分配出去了

                        if (page->slab == NGX_SLAB_BUSY) {//执行完page->slab |= m;后，有可能page->slab == NGX_SLAB_BUSY，表示最后一个obj已经分配出去了
                            //pages_m[i]和slot_m[i]取消对应的引用关系，因为该pages_m[i]指向的页page已经用完了
                            prev = (ngx_slab_page_t *)
                                            (page->prev & ~NGX_SLAB_PAGE_MASK);
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            page->next = NULL;
                            page->prev = NGX_SLAB_EXACT;
                        }

                        p = (page - pool->pages) << ngx_pagesize_shift;
                        p += i << shift;
                        p += (uintptr_t) pool->start; //返回该obj对应的地址

                        goto done; 
                    }
                }

                page = page->next;

            } while (page);

        } else { /* shift > ngx_slab_exact_shift */

   //size > 128B，也是更新slab page的slab成员，但是需要预先设置slab的部分bit，因为一个页的obj数量小于32个，并返回待分配的空间在缓存的位置  
            n = ngx_pagesize_shift - (page->slab & NGX_SLAB_SHIFT_MASK);
            n = 1 << n;
            n = ((uintptr_t) 1 << n) - 1;
            mask = n << NGX_SLAB_MAP_SHIFT;

            do {//如果整个page页的所有slab已经用完，则会在后面的ngx_slab_alloc_pages从新获取空间并划分slab，然后分配
                if ((page->slab & NGX_SLAB_MAP_MASK) != mask) {

                    for (m = (uintptr_t) 1 << NGX_SLAB_MAP_SHIFT, i = 0;
                         m & mask;
                         m <<= 1, i++)
                    {
                        if ((page->slab & m)) {
                            continue;
                        }

                        page->slab |= m;

                        if ((page->slab & NGX_SLAB_MAP_MASK) == mask) {
                            prev = (ngx_slab_page_t *)
                                            (page->prev & ~NGX_SLAB_PAGE_MASK);
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            page->next = NULL;
                            page->prev = NGX_SLAB_BIG;
                        }

                        p = (page - pool->pages) << ngx_pagesize_shift;
                        p += i << shift;
                        p += (uintptr_t) pool->start;

                        goto done;
                    }
                }

                page = page->next;

            } while (page);
        }
    }

    //分出一页加入到m_slot数组对应元素中  
    page = ngx_slab_alloc_pages(pool, 1);

    /*  
       例如要分配的size为54字节，则在前面计算出的shift对应的字节数应该是64字节，由于一个页面全是64字节obj大小，所以一共有64
       个64字节的obj，64个obj至少需要64位来表示每个obj是否已使用，因此至少需要64位(也就是8字节，2个int),所以至少要暂用一个64
       字节obj来存储该bitmap信息，第一个64字节obj实际上只用了8字节，其他56字节未用
     */
    if (page) {
        //size<128
        if (shift < ngx_slab_exact_shift) {
            p = (page - pool->pages) << ngx_pagesize_shift;//slot块的map存储在page的slot中定位到对应的page  
            bitmap = (uintptr_t *) (pool->start + p); //page页的起始地址的一个uintptr_t类型4字节用来存储bitmap信息

            /*  
               例如要分配的size为54字节，则在前面计算出的shift对应的字节数应该是64字节，由于一个页面全是64字节obj大小，所以一共有64
               个64字节的obj，64个obj至少需要64位来表示每个obj是否已使用，因此至少需要64位(也就是8字节，2个int),所以至少要暂用一个64
               字节obj来存储该bitmap信息，第一个64字节obj实际上只用了8字节，其他56字节未用
               */
            s = 1 << shift;//s表示每个slot块的大小,字节为单位  
            n = (1 << (ngx_pagesize_shift - shift)) / 8 / s;  //计算bitmap需要多少个slot obj块  
        
            if (n == 0) {
                n = 1; //至少需要一个4M页面大小的一个obj(2<<shift字节)来存储bitmap信息
            }

            //设置对应的slot块的map,对于存放map的slot设置为1,表示已使用并且设置第一个可用的slot块(不是用于记录map的slot块)
            //标记为1,因为本次即将使用.
            bitmap[0] = (2 << n) - 1; //这里是获取该页的第二个obj，因为第一个已经用于bitmap了，所以第一个和第二个在这里表示已用，bitmap[0]=3

            //计算所有obj的位图需要多少个uintptr_t存储。例如每个obj大小为64字节，则4K里面有64个obj，也就需要8字节，两个bitmap
            map = (1 << (ngx_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

            for (i = 1; i < map; i++) {
                bitmap[i] = 0; //从第二个bitmap开始的所有位先清0
            }

            page->slab = shift; //该页的一个obj对应的字节移位数大小
            /* ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t)(slots_m[]) + pages * sizeof(ngx_slab_page_t)(pages_m[]) +pages*ngx_pagesize */
            page->next = &slots[slot];//指向上面的slots_m[i],例如obj大小64字节，则指向slots[2]   slots[0-8] -----8-2048
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_SMALL; //标记该页中存储的是小与128的obj

            slots[slot].next = page;

            p = ((page - pool->pages) << ngx_pagesize_shift) + s * n;
            //返回对应地址.  例如为64字节obj，则返回的start为第二个开始处obj，下次分配从第二个开始获取地址空间obj
            p += (uintptr_t) pool->start;//返回对应地址.,

            goto done;

        } else if (shift == ngx_slab_exact_shift) {

            page->slab = 1;//slab设置为1   page->slab存储obj的bitmap,例如这里为1，表示说第一个obj分配出去了 4K有32个128字节obj,因此一个slab位图刚好可以表示这32个obj
            page->next = &slots[slot];
            //用指针的后两位做标记,用NGX_SLAB_SMALL表示slot块小于ngx_slab_exact_shift  
            /*
                设置slab的高16位(32位系统)存放solt对应的map,并且该对应为map的地位对应page中高位的slot块.slab的低16为存储slot块大小的位移.
               */ 
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_EXACT; 

            slots[slot].next = page;

            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t) pool->start;//返回对应地址.  

            goto done;

        } else { /* shift > ngx_slab_exact_shift */

            //大于128，也就是至少256?,4K最多也就16个256，因此只需要slab的高16位表示obj位图即可
            page->slab = ((uintptr_t) 1 << NGX_SLAB_MAP_SHIFT) | shift;
            page->next = &slots[slot];
            //用指针的后两位做标记,用NGX_SLAB_BIG表示slot块大于ngx_slab_exact_shift  
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_BIG;

            slots[slot].next = page;

            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t) pool->start;

            goto done;
        }
    }

    p = 0;

done:

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab alloc: %p", p);

    return (void *) p;
}


void *
ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = ngx_slab_calloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


void *
ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    p = ngx_slab_alloc_locked(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


void
ngx_slab_free(ngx_slab_pool_t *pool, void *p)
{
    ngx_shmtx_lock(&pool->mutex);

    ngx_slab_free_locked(pool, p);

    ngx_shmtx_unlock(&pool->mutex);
}

/*
根据给定的指针p,释放相应内存块.
1.找到p对应的内存块和对应的m_page数组元素,
2.根据m_page数组元素的pre指针确定页面类型
     (1).如果NGX_SLAB_SMALL类型,即size小于ngx_slab_exact_size
a.设置相应slot块为可用
b.设如果整个页面都可用,则将页面归入free中
    (2).如果NGX_SLAB_EXACT类型,即size等于ngx_slab_exact_size
a.设置相应slot块为可用
b.设如果整个页面都可用,则将页面归入free中
    (3).如果NGX_SLAB_BIG类型,即size大于ngx_slab_exact_size
a.设置相应slot块为可用
b.设如果整个页面都可用,则将页面归入free中
     (4).如果NGX_SLAB_PAGE类型,即size大小大于等于一个页面
a.设置相应页面块为可用
b.将页面归入free中
*/
void
ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p)
{
    size_t            size;
    uintptr_t         slab, m, *bitmap;
    ngx_uint_t        n, type, slot, shift, map;
    ngx_slab_page_t  *slots, *page;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab free: %p", p);

    if ((u_char *) p < pool->start || (u_char *) p > pool->end) {
        ngx_slab_error(pool, NGX_LOG_ALERT, "ngx_slab_free(): outside of pool");
        goto fail;
    }

    //根据p找到需要释放的m_page元素  
    n = ((u_char *) p - pool->start) >> ngx_pagesize_shift;
    page = &pool->pages[n];
    slab = page->slab; //如果分配的时候一次性分配多个page，则第一个page的slab指定本次一次性分配了多少个页page
    //据pre的低两位来判断该页面中的slot大小和ngx_slab_exact_size的大小关系  
    type = page->prev & NGX_SLAB_PAGE_MASK;

    switch (type) {

    case NGX_SLAB_SMALL://slot小于ngx_slab_exact_size  

        //块obj大小的偏移  
        shift = slab & NGX_SLAB_SHIFT_MASK;
        size = 1 << shift;//计算块obj的大小  

        if ((uintptr_t) p & (size - 1)) {//由于对齐,p的地址一定是obj大小的整数倍  
            goto wrong_chunk;
        }

        n = ((uintptr_t) p & (ngx_pagesize - 1)) >> shift;//求出p对应的obj块的位置  
        m = (uintptr_t) 1 << (n & (sizeof(uintptr_t) * 8 - 1));////求出在uintptr_t中,p的偏移,即求出在uintptr_t中的第几位  
        //由于map是根据uintptr_t划分的,所以求出该块对应的uintptr_t的偏移,即求出第几个uintptr_t  
        n /= (sizeof(uintptr_t) * 8); //该obj所在的是那个bitmap，例如一个页64个obj，则需要2个bitmap表示这64个obj的位图
        bitmap = (uintptr_t *)
                             ((uintptr_t) p & ~((uintptr_t) ngx_pagesize - 1));//求出p对应的page页所在位图的位置  

        if (bitmap[n] & m) {//如果第m位确实为1,  

            if (page->next == NULL) { //如果页面的当前状态是全部已使用,就把他链入slot_m[]中
                slots = (ngx_slab_page_t *)
                                   ((u_char *) pool + sizeof(ngx_slab_pool_t));//定位slot_m数组  

                //找到对应的slot_m[]的元素  
                slot = shift - pool->min_shift;

                //链入对应的slot[]中，表示该页可以继续使用了，在ngx_slab_calloc_locked又可以遍历到该页，从中分配obj
                page->next = slots[slot].next;
                slots[slot].next = page;

                //设置m_page的pre  
                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_SMALL;
                page->next->prev = (uintptr_t) page | NGX_SLAB_SMALL;
            }

             //页面的当前状态是部分已使用,即已经在slot中  设置slot对应位置为可用,即0  
            bitmap[n] &= ~m;

            //下面的操作主要是查看这个页面是否都没用, 如果都没使用,则将页面归入free中  
            
            n = (1 << (ngx_pagesize_shift - shift)) / 8 / (1 << shift);

            if (n == 0) {//计算需要多少个4字节bitmap来存储位图，例如64个obj需要2个bitmap来表示64位,需要多少个obj来存储这两个bitmap，一般第一个obj够存储两个bitmap，也就是用64字节中的8字节
                n = 1;
            }

            //查看第一个uintptr_t中的块obj是否都可用  
            if (bitmap[0] & ~(((uintptr_t) 1 << n) - 1)) {
                goto done;
            }

             //计算位图使用了多少个uintptr_t来存储
            map = (1 << (ngx_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

            for (n = 1; n < map; n++) {//查看其他uintptr_t是否都没使用  
                if (bitmap[n]) {
                    goto done;
                }
            }

            ngx_slab_free_pages(pool, page, 1); //整个页面都没有使用，归还给free 

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_EXACT:
        //p所对应的在slab(obj位图)中的位置.  
        m = (uintptr_t) 1 <<
                (((uintptr_t) p & (ngx_pagesize - 1)) >> ngx_slab_exact_shift);
        size = ngx_slab_exact_size;

        if ((uintptr_t) p & (size - 1)) {//如果p为page中的obj块,那么一定是size整数倍  
            goto wrong_chunk;
        }

        if (slab & m) {//slab(位图)中对应的位为1  
            if (slab == NGX_SLAB_BUSY) {//如果整个页面中的所有obj块都被使用,则该页page[]和slot[]没有对应关系,因此需要把页page[]和slot[]对应关系加上
                 //定位slot[]数组  
                slots = (ngx_slab_page_t *)
                                   ((u_char *) pool + sizeof(ngx_slab_pool_t));
                slot = ngx_slab_exact_shift - pool->min_shift;//计算该页面要链入slot[]的哪个槽中  

                //设置page[]元素和slot[]的对应关系，通过prev和next指向
                page->next = slots[slot].next;
                slots[slot].next = page;
                
                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_EXACT;
                page->next->prev = (uintptr_t) page | NGX_SLAB_EXACT;
            }

            page->slab &= ~m;//将slab块对应位置设置为0  

            if (page->slab) {//page页面中还有正在使用的obj块,因为slab位图不为0
                goto done;
            }

            ngx_slab_free_pages(pool, page, 1);//page页面中所有slab块都没有使用  

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_BIG://用户请求内存的大小大于ngx_slab_exact_size  
        //slab的高16位是slot块的位图,低16位用于存储slot块大小的偏移  

        shift = slab & NGX_SLAB_SHIFT_MASK;
        size = 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }
        //找到该slab块在位图中的位置.这里要注意一下,  
        //位图存储在slab的高16位,所以要+16(即+ NGX_SLAB_MAP_SHIFT)  
        m = (uintptr_t) 1 << ((((uintptr_t) p & (ngx_pagesize - 1)) >> shift)
                              + NGX_SLAB_MAP_SHIFT);

        if (slab & m) {//该slab块确实正在被使用  

            if (page->next == NULL) {//如果整个页面中的所有obj块都被使用,则该页page[]和slot[]没有对应关系,因此需要把页page[]和slot[]对应关系加上
                //定位slot[]数组  
                slots = (ngx_slab_page_t *)
                                   ((u_char *) pool + sizeof(ngx_slab_pool_t));
                slot = shift - pool->min_shift;
                //找到slot[]数组中对应的位置,添加slot[]和page[]的对应关系
                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_BIG;
                page->next->prev = (uintptr_t) page | NGX_SLAB_BIG;
            }

            page->slab &= ~m;//设置slab块对应的位图位置为0,即可用  
            
            //如果slab页中有slot块还在被使用  
            if (page->slab & NGX_SLAB_MAP_MASK) {
                goto done;
            }

            //如果page页中所有slab块都不在使用就将该页面链入free中  
            ngx_slab_free_pages(pool, page, 1);

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_PAGE://用户归还整个页面  

        if ((uintptr_t) p & (ngx_pagesize - 1)) { //p是也对齐的，检查下
            goto wrong_chunk;
        }

        if (slab == NGX_SLAB_PAGE_FREE) { 
        
            ngx_slab_error(pool, NGX_LOG_ALERT,
                           "ngx_slab_free(): page is already free");
            goto fail;
        }

        if (slab == NGX_SLAB_PAGE_BUSY) {
        //说明是连续分配多个page的非首个page，不能直接释放，不许这几个page一起释放，因此p指针指向必须是首page
            ngx_slab_error(pool, NGX_LOG_ALERT,
                           "ngx_slab_free(): pointer to wrong page");
            goto fail;
        }

        //计算页面对应的page[]槽  
        n = ((u_char *) p - pool->start) >> ngx_pagesize_shift;
        size = slab & ~NGX_SLAB_PAGE_START;//计算归还page的个数  

        ngx_slab_free_pages(pool, &pool->pages[n], size); //归还页面  

        ngx_slab_junk(p, size << ngx_pagesize_shift);

        return;
    }

    /* not reached */

    return;

done:

    ngx_slab_junk(p, size);

    return;

wrong_chunk:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): pointer to wrong chunk");

    goto fail;

chunk_already_free:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): chunk is already free");

fail:

    return;
}

/*
返回一个slab page，这个slab page之后会被用来确定所需分配的空间在内存缓存的位置 

例如总共有6个page[]，ngx_slab_init中page[0]的next和prev都指向free，free的next也指向page[0]。当调用ngx_slab_alloc_pages向获取3个pages的时候
则前三个pages(page[0], page[1], page[2])会被分配好，最末尾page[5]的prev指向page[3],并且page[3]的slab指定现在只有6-3=3个page可以分配了，
然后page[3]的next和prev指向free,free的next和prev也指向page[3]，也就是下次只能从page[3]开始获取页

*/ //分配一个页面,并将页面从free中摘除.

/* 
-------------------------------------------------------------------
| page1  | page2 | page3 | page4| page5) | page6 | page7 | page8 |
--------------------------------------------------------------------
初始状态: pool->free指向page1,page1指向poll->free,其他的page的next和prev默认指向NULL,也就是pool->free指向整个page体，page1->slab=8

1.假设第一次ngx_slab_alloc_pages获取两个page，则page1和page2会分配出去，page1为这两个page的首page1->slab只出这两个连续page是一起分配的,
    page2->slab = NGX_SLAB_PAGE_BUSY;表示这是跟随page1一起分配出去的，并且本page不是首page。这时候pool->free指向page3,并表示page3->slab=6，
    表示page3开始还有6个page可以使用
2.假设第二次又获取了3个page(page3-5),则page3是首page,page3->slab=3,同时page4,page5->slab = NGX_SLAB_PAGE_BUSY;表示这是跟随page1一
    起分配出去的，并且本page不是首page。这时候pool->free指向page6,并表示page6->slab=3，表示page6开始还有3个page可以使用
3. 同理再获取1个page,page6。这时候pool->free指向page7,并表示page7->slab=2，表示page7开始还有2个page可以使用
4. 现在释放第1步page1开始的2个page，则在ngx_slab_free_pages中会把第3步后剩余的两个未分配的page7(实际上是把page7开始的2个page标识为1个大page)
   和page1(page1实际上这时候标识的是第一步中的page1开始的2个page)和pool->free形成一个双向链表环，可以见
  
                      pool->free  
                    ----------  /                 
 -----------------\|         | --------------------------------------  
|  ----------------|         | -----------------------------------   |
|  |                 -----------                                  |  |
|  |                                                              |  |
|  |     page1 2                                   page7 2        |  |
|  |  \ ----------                          \  -----------   /    |  |
|   --- |    |    |----------------------------|    |     | -------  | 
------- |    |    |--------------------------- |    |     | ---------- 
        ----------  \                           ---------

5.当释放ngx_slab_free_pagespage3开始的3个page页后，page3也会连接到双向环表中，链如pool->free与page1[i]之间，注意这时候的page1和page2是紧靠一起的page
  但没有对他们进行合并，page1->slab还是=2  page2->slab还是=3。并没有把他们合并为一个整体page->slab=5,如果下次想alloc一个4page的空间，是
  分配不成功的
*/
static ngx_slab_page_t *
ngx_slab_alloc_pages(ngx_slab_pool_t *pool, ngx_uint_t pages) //与ngx_slab_free_pages配合阅读理解
{
    ngx_slab_page_t  *page, *p;

    //初始化的时候pool->free.next默认指向第一个pool->pages
    //从pool->free.next开始，每次取(slab page) page = page->next  
    for (page = pool->free.next; page != &pool->free; page = page->next) { //如果一个可用page页都没有，就不会进入循环体

    /*
    本个slab page剩下的缓存页数目>=需要分配的缓存页数目pages则可以分配，否则继续遍历free,直到下一个首page及其后连续page数和大于等于需要分配的pages数，才可以分配 
    slab是首次分配page开始的slab个页的时候指定的，在释放的时候slab还是首次分配时候的slab，不会变，也就是说释放page后不会把相邻的两个page页的slab数合并，
    例如首次开辟page1开始的3个page页空间，page1->slab=3,紧接着开辟page2开始的2个page页空间，page2->slab=2,当连续释放page1和page2对应的空间后，他们还是
    两个独立的page[]空间，slab分别是2和3,而不会把这两块连续空间进行合并为1个(也就是新的page3,page3首地址等于page2，并且page3->slab=3+2=5)
    */
        if (page->slab >= pages) {

            //例如总共有6个page[]，ngx_slab_init中page[0]的next和prev都指向free，free的next也指向page[0]。当调用ngx_slab_alloc_pages向获取3个pages的时候
            //则前三个pages(page[0], page[1], page[2])会被分配好，最末尾page[5]的prev指向page[3],并且page[3]的slab指定现在只有6-3=3个page可以分配了，
            //然后page[3]的next和prev指向free,free的next和prev也指向page[3]，也就是下次只能从page[3]开始获取页
            if (page->slab > pages) {  
                page[page->slab - 1].prev = (uintptr_t) &page[pages];

                //更新从本个slab page开始往下第pages个slab page的缓存页数目为本个slab page数目减去pages  
                //让下次可以从page[pages]开始分配的页的next和prev指向pool->free,只要页的next和prev指向了free，则表示可以从该页开始分配页page
                page[pages].slab = page->slab - pages; //下次开始从这个page[]处开始获取页，并且现在可用page只有page->slab - pages多了
                page[pages].next = page->next; //该可用页的next指向pool->free.next
                page[pages].prev = page->prev; //该可用页的prev指向pool->free.next

                p = (ngx_slab_page_t *) page->prev;
                p->next = &page[pages];//更新pool->free.next = &page[pages]，下次从第pages个slab page开始进行上面的for()循环遍历
                page->next->prev = (uintptr_t) &page[pages]; //poll->free->prev = &page[pages]指向下次可以分配页的页地址

            } else { //page页不够用了，则free的next和prev都指向自己，所以下次再进入该函数进入for()循环的时候无法进入循环体中，也分配不到page
                //如果free指向的page页可用页大小为2，单该函数要求获取3个页，则直接把这两个页返回出去，就是说这两个页你可以先用着，总比没有好。
                p = (ngx_slab_page_t *) page->prev; //获取poll->free
                p->next = page->next; //poll->free->next = poll->free
                page->next->prev = page->prev; ////poll->free->prev = poll->free   free的next和prev都指向了自己，说明没有多余空间分配了
            }

            //NGX_SLAB_PAGE_START标记page是分配的pages个页的第一个页，并在第一个页page中记录出其后连续的pages个页是一起分配的
            page->slab = pages | NGX_SLAB_PAGE_START; //更新被分配的page slab中的第一个的slab成员，即页的个数和占用情况  
            //page的next和prev都相当于指向了NULL了，
            page->next = NULL; 
            page->prev = NGX_SLAB_PAGE; //page页面不划分slot时候,即将整个页面分配给用户,pre的后两位为NGX_SLAB_PAGE

            if (--pages == 0) { //pages为1。则直接返回该page
                return page;
            }

            for (p = page + 1; pages; pages--) {
                //如果分配的页数pages>1，更新后面page slab的slab成员为NGX_SLAB_PAGE_BUSY  
                p->slab = NGX_SLAB_PAGE_BUSY; 
                //标记这是连续分配多个page，并且我不是首page，例如一次分配3个page,分配的page为[1-3]，则page[1].slab=3  page[2].slab=page[3].slab=NGX_SLAB_PAGE_BUSY记录
                p->next = NULL;
                p->prev = NGX_SLAB_PAGE;
                p++;
            }

            return page;
        }
    }

    if (pool->log_nomem) {
        ngx_slab_error(pool, NGX_LOG_CRIT,
                       "ngx_slab_alloc() failed: no memory");
    }

    //没有找到空余的页  
    return NULL;
}

//释放page页开始的pages个页面
static void
ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
    ngx_uint_t pages) //释放pages个页面,并将页面放入free中
{
    ngx_uint_t        type;
    ngx_slab_page_t  *prev, *join;

    page->slab = pages--; //释放的pages页数-1，这是要干嘛?

    if (pages) {
        ngx_memzero(&page[1], pages * sizeof(ngx_slab_page_t)); //如果一次性分配了大于等于2的page，则需要把首page后的其他page恢复清0操作
    }

    if (page->next) { 
        //解除slot[]和page[]的关联，让slot[]的next和prev指向slot[]自身
        prev = (ngx_slab_page_t *) (page->prev & ~NGX_SLAB_PAGE_MASK);
        prev->next = page->next;
        page->next->prev = page->prev;
    }
    
    //这是要释放的页pages的最后一个page，有可能一次释放3个页，则join代表第三个页的起始地址,如果pages为1，则join直接指向分配的page
    join = page + page->slab; 

    if (join < pool->last) { //join不是pool中最后一个page 
        type = join->prev & NGX_SLAB_PAGE_MASK;
        
        if (type == NGX_SLAB_PAGE) {//和ngx_slab_alloc_pages配合阅读，
            
            if (join->next != NULL) { //该if应该始终满足不了???/  在alloc的时候首页page的next是指向NULL
                pages += join->slab;
                page->slab += join->slab;

                prev = (ngx_slab_page_t *) (join->prev & ~NGX_SLAB_PAGE_MASK);
                prev->next = join->next;
                join->next->prev = join->prev;

                join->slab = NGX_SLAB_PAGE_FREE;
                join->next = NULL;
                join->prev = NGX_SLAB_PAGE;
            }
        }
    }

    if (page > pool->pages) {
        join = page - 1;
        type = join->prev & NGX_SLAB_PAGE_MASK;

        if (type == NGX_SLAB_PAGE) {

            if (join->slab == NGX_SLAB_PAGE_FREE) {
                join = (ngx_slab_page_t *) (join->prev & ~NGX_SLAB_PAGE_MASK);
            }

            if (join->next != NULL) {
                pages += join->slab;
                join->slab += page->slab;

                prev = (ngx_slab_page_t *) (join->prev & ~NGX_SLAB_PAGE_MASK);
                prev->next = join->next;
                join->next->prev = join->prev;

                page->slab = NGX_SLAB_PAGE_FREE;
                page->next = NULL;
                page->prev = NGX_SLAB_PAGE;

                page = join;
            }
        }
    }

    if (pages) { //例如一次alloc 3个page，分配是page[3 4 5],则page[5]的prev指向page[3]
        page[pages].prev = (uintptr_t) page;
    }

    /*
    freepage[i]为本次释放的freepage[i]开始的pages个page页
    unusepage[i]为还未分配使用的原始page页开始处，可能会有多个page[]紧跟其后。
                          pool->free  
                        ----------  /                 
     -----------------\|         | --------------------------------------  
    |  ----------------|         | -----------------------------------   |
    |  |                 -----------                                  |  |
    |  |                                                              |  |
    |  |     freepage[i]                           unusepage[i]       |  |
    |  |  \ ----------                          \  -----------   /    |  |
    |   --- |    |    |----------------------------|    |     | -------  | 
    ------- |    |    |--------------------------- |    |     | ---------- 
            ----------  \                           ---------

    走到这里后，pool->free指向了该释放的page[]，注意这时候首page->slab就是

   
     */
    

    //和ngx_slab_alloc_pages配合阅读， 这里释放后就会把之前free指向的可用page页与释放的page页以及pool->free形成一个环形链表
    page->prev = (uintptr_t) &pool->free;
    page->next = pool->free.next;

    page->next->prev = (uintptr_t) page;

    pool->free.next = page;
}


static void
ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level, char *text)
{
    ngx_log_error(level, ngx_cycle->log, 0, "%s%s", text, pool->log_ctx);
}
