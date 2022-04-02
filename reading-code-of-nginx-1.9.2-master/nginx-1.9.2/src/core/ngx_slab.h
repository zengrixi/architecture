
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SLAB_H_INCLUDED_
#define _NGX_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_slab_page_s  ngx_slab_page_t;
//ͼ�λ����ο�:http://blog.csdn.net/u013009575/article/details/17743261
struct ngx_slab_page_s { //��ʼ����ֵ��ngx_slab_init
    //��������������;  
    //����Ҫ�����µ�ҳ��ʱ�򣬷���N��ҳngx_slab_page_s�ṹ�е�һ��ҳ��slab��ʾ���һ�������˶��ٸ�ҳ //�����������������page�������Ҳ�����page������һ�η���3��page,�����pageΪ[1-3]����page[1].slab=3  page[2].slab=page[3].slab=NGX_SLAB_PAGE_BUSY��¼
    //���OBJ<128һ��ҳ�д�ŵ��Ƕ��obj(����128��32�ֽ�obj),��slab��¼�����obj�Ĵ�С����ngx_slab_alloc_locked
    //���obj��λ��СΪngx_slab_exact_shift��Ҳ����obj128�ֽڣ�page->slab = 1;page->slab�洢obj��bitmap,��������Ϊ1����ʾ˵��һ��obj�����ȥ��   ��ngx_slab_alloc_locked
    //���obj��λ��СΪngx_slab_exact_shift��Ҳ����obj>128�ֽڣ�page->slab = ((uintptr_t) 1 << NGX_SLAB_MAP_SHIFT) | shift;//����128��Ҳ��������256�,4K���Ҳ��16��256�����ֻ��Ҫslab�ĸ�16λ��ʾobjλͼ����
    //������ĳЩ��С��obj��ʱ��(һ������ҳ��Ŷ��obj)��slab��ʾ������Ļ����ռ�����(�Ƿ����)����bitλ����ʾ
    //���
    uintptr_t         slab; //ngx_slab_init�г�ʼ��ֵΪ�����ڴ���ʣ��ҳ�ĸ���
    //��ngx_slab_init�г�ʼ����9��ngx_slab_page_sͨ��next������һ��
    //���ҳ�е�ojb<128 = 128 ����>128 ,��nextֱ��ָ���Ӧ��ҳslots[slot].next = page; ͬʱpages_m[]ָ��page->next = &slots[slot];
    ngx_slab_page_t  *next; //�ڷ����Сobj��ʱ��nextָ��slab page��pool->pages��λ��    //��һ��pageҳ  
    //����ָ����4�ı���,��ô����λһ��Ϊ0,��ʱ���ǿ�������ָ��ĺ���λ�����,������ÿռ�. �õ���λ��¼NGX_SLAB_PAGE�ȱ��
    //���ҳ�е�obj<128,��Ǹ�ҳ�д洢����С��128��obj page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_SMALL 
    //obj=128 page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_EXACT; 
    uintptr_t         prev;//��һ��pageҳ  
};
/*
�����ڴ����ʵ��ַ��ʼ������:ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t)(slots_m[]) + pages * sizeof(ngx_slab_page_t)(pages_m[]) +pages*ngx_pagesize(����ʵ�ʵ����ݲ��֣�
ÿ��ngx_pagesize����ǰ���һ��ngx_slab_page_t���й�������ÿ��ngx_pagesize��ǰ�˵�һ��obj��ŵ���һ�����߶��int����bitmap�����ڹ���ÿ������ȥ���ڴ�)

m_slot[0]:����pageҳ��,����pageҳ�滮�ֵ�slot���СΪ2^3
m_slot[1]:����pageҳ��,����pageҳ�滮�ֵ�slot���СΪ2^4
m_slot[2]:����pageҳ��,����pageҳ�滮�ֵ�slot���СΪ2^5
������������.
m_slot[8]:����pageҳ��,����pageҳ�滮�ֵ�slot���СΪ2k(2048)

m_page����:������ÿ��Ԫ�ض�Ӧһ��pageҳ.
m_page[0]��Ӧpage[0]ҳ��
m_page[1]��Ӧpage[1]ҳ��
m_page[2]��Ӧpage[2]ҳ��
��������������������.
m_page[k]��Ӧpage[k]ҳ��
��������е�m_page[]û����Ӧҳ���������Ӧ.

*/
//ͼ�λ����ο�:http://blog.csdn.net/u013009575/article/details/17743261
typedef struct { //��ʼ����ֵ��ngx_slab_init  slab�ṹ����Ϲ����ڴ�ʹ�õ�  ������limit reqģ��Ϊ�����ο�ngx_http_limit_req_module
    ngx_shmtx_sh_t    lock; //mutex����  

    size_t            min_size; //�ڴ滺��obj��С�Ĵ�С��һ����1��byte   //��С����Ŀռ���8byte ��ngx_slab_init 
    //slab pool��shift���ȽϺͼ�����������obj��С��ÿ������ҳ�ܹ�����obj�����Լ��������ҳ�ڻ���ռ��λ��  
    size_t            min_shift; //ngx_init_zone_pool��Ĭ��Ϊ3
/*
�����ڴ����ʵ��ַ��ʼ������:ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t)(slots_m[]) + pages * sizeof(ngx_slab_page_t)(pages_m[]) +pages*ngx_pagesize(����ʵ�ʵ����ݲ��֣�
ÿ��ngx_pagesize����ǰ���һ��ngx_slab_page_t���й�������ÿ��ngx_pagesize��ǰ�˵�һ��obj��ŵ���һ�����߶��int����bitmap�����ڹ���ÿ������ȥ���ڴ�)
*/
    //ָ��ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t) + pages * sizeof(ngx_slab_page_t) +pages*ngx_pagesize(����ʵ�ʵ����ݲ���)�е�pages * sizeof(ngx_slab_page_t)��ͷ��
    ngx_slab_page_t  *pages; //slab page�ռ�Ŀ�ͷ   ��ʼָ��pages * sizeof(ngx_slab_page_t)�׵�ַ
    ngx_slab_page_t  *last; // Ҳ����ָ��ʵ�ʵ�����ҳpages*ngx_pagesize��ָ�����һ��pagesҳ
    //����free��ҳ��   ��һ������ͷ,�������ӿ���ҳ��.
    ngx_slab_page_t   free; //��ʼ����ֵ��ngx_slab_init  free->nextָ��pages * sizeof(ngx_slab_page_t)  �´δ�free.next���´η���ҳʱ�����ڿ�ʼ����ҳ�ռ�

    u_char           *start; //ʵ�ʻ���obj�Ŀռ�Ŀ�ͷ   ����ǶԵ�ַ�ռ����ngx_pagesize��������ʼ��ַ����ngx_slab_init
    u_char           *end;

    ngx_shmtx_t       mutex; //ngx_init_zone_pool->ngx_shmtx_create->sem_init���г�ʼ��

    u_char           *log_ctx;//pool->log_ctx = &pool->zero;
    u_char            zero;

    unsigned          log_nomem:1; //ngx_slab_init��Ĭ��Ϊ1

    //ngx_http_file_cache_init��cache->shpool->data = cache->sh;
    void             *data; //ָ��ngx_http_file_cache_t->sh
    void             *addr; //ָ��ngx_slab_pool_t�Ŀ�ͷ    //ָ�����ڴ�ngx_shm_zone_t�е�addr+sizeβ����ַ
} ngx_slab_pool_t;

//ͼ�λ����ο�:http://blog.csdn.net/u013009575/article/details/17743261
void ngx_slab_init(ngx_slab_pool_t *pool);
void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size);
void ngx_slab_free(ngx_slab_pool_t *pool, void *p);
void ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p);


#endif /* _NGX_SLAB_H_INCLUDED_ */
