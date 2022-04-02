
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t                    caches;  /* ngx_http_file_cache_t * */
} ngx_http_fastcgi_main_conf_t;


typedef struct { //�����ռ�͸�ֵ��ngx_http_fastcgi_init_params
    ngx_array_t                   *flushes;
    ngx_array_t                   *lengths;//fastcgi_param  HTTP_xx  XXX���õķ�HTTP_��������ر����ĳ���code��ӵ�������
    ngx_array_t                   *values;//fastcgi_param  HTTP_xx  XXX���õķ�HTTP_��������ر�����valueֵcode��ӵ�������
    ngx_uint_t                     number; //fastcgi_param  HTTP_  XXX;������ͨ��fastcgi_param���õ�HTTP_xx��������
    ngx_hash_t                     hash;//fastcgi_param  HTTP_  XXX;������ͨ��fastcgi_param���õ�HTTP_xxͨ��hash����浽��hash����,ʵ�ʴ浽hash��ʱ���HTTP_��5���ַ�ȥ��
} ngx_http_fastcgi_params_t; //ngx_http_fastcgi_loc_conf_t->params(params_source)�д洢


typedef struct {
    ngx_http_upstream_conf_t       upstream; //����fastcgi��ngx_http_fastcgi_pass�д���upstream�ռ䣬ngx_http_xxx_pass

    ngx_str_t                      index;
    //��ngx_http_fastcgi_init_params��ͨ���ű���������ѱ���code��ӵ�params��
    ngx_http_fastcgi_params_t      params; //Params���ݰ������ڴ���ִ��ҳ������Ҫ�Ĳ����ͻ���������
#if (NGX_HTTP_CACHE)
    ngx_http_fastcgi_params_t      params_cache;
#endif

    //fastcgi_param���õĴ��͵�FastCGI����������ز�������ӵ��������У���ngx_http_upstream_param_set_slot
    ngx_array_t                   *params_source;  //���ջ���ngx_http_fastcgi_init_params��ͨ���ű���������ѱ���code��ӵ�params��

    /*
    Sets a string to search for in the error stream of a response received from a FastCGI server. If the string is found then 
    it is considered that the FastCGI server has returned an invalid response. This allows handling application errors in nginx, for example: 
    
    location /php {
        fastcgi_pass backend:9000;
        ...
        fastcgi_catch_stderr "PHP Fatal error";
        fastcgi_next_upstream error timeout invalid_header;
    }
    */ //�����˷��ص�fastcgi ERRSTD��Ϣ�е�data���ִ���fastcgi_catch_stderr���õ��ַ��������������һ����˷����� �ο�ngx_http_fastcgi_process_header
    ngx_array_t                   *catch_stderr; //fastcgi_catch_stderr xxx_catch_stderr

    //��ngx_http_fastcgi_eval��ִ�ж�Ӧ��code���Ӷ�����ر���ת��Ϊ��ͨ�ַ���   
    //��ֵ��ngx_http_fastcgi_pass
    ngx_array_t                   *fastcgi_lengths; //fastcgi��ز����ĳ���code  ���fastcgi_pass xxx���б������������Ϊ��
    ngx_array_t                   *fastcgi_values; //fastcgi��ز�����ֵcode

    ngx_flag_t                     keep_conn; //fastcgi_keep_conn  on | off  Ĭ��off

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t       cache_key;
    //fastcgi_cache_key proxy_cache_keyָ���ʱ���������ĸ��ӱ��ʽ�ṹ�������flcf->cache_key�� ngx_http_fastcgi_cache_key ngx_http_proxy_cache_key
#endif

#if (NGX_PCRE)
    ngx_regex_t                   *split_regex;
    ngx_str_t                      split_name;
#endif
} ngx_http_fastcgi_loc_conf_t;

//http://my.oschina.net/goal/blog/196599
typedef enum { //��Ӧngx_http_fastcgi_header_t�ĸ����ֶ�   �ο�ngx_http_fastcgi_process_record�������� �������ngx_http_fastcgi_create_request
    //fastcgiͷ��
    ngx_http_fastcgi_st_version = 0,
    ngx_http_fastcgi_st_type,
    ngx_http_fastcgi_st_request_id_hi,
    ngx_http_fastcgi_st_request_id_lo,
    ngx_http_fastcgi_st_content_length_hi,
    ngx_http_fastcgi_st_content_length_lo,
    ngx_http_fastcgi_st_padding_length,
    ngx_http_fastcgi_st_reserved,
    
    ngx_http_fastcgi_st_data, //fastcgi����
    ngx_http_fastcgi_st_padding //8�ֽڶ�������ֶ�
} ngx_http_fastcgi_state_e; //fastcgi���ĸ�ʽ��ͷ��(8�ֽ�)+����(һ����8����ͷ��+����)+����ֶ�(8�ֽڶ������������ֽ���) 
 

typedef struct {
    u_char                        *start;
    u_char                        *end;
} ngx_http_fastcgi_split_part_t; //�����͸�ֵ��ngx_http_fastcgi_process_header  ���һ�ν���fastcgiͷ������Ϣû��ɣ���Ҫ�ٴζ�ȡ������ݽ���

//�ڽ����Ӻ�˷��͹�����fastcgiͷ����Ϣ��ʱ���õ�����ngx_http_fastcgi_process_header
typedef struct { //ngx_http_fastcgi_handler����ռ�
//��������¼ÿ�ζ�ȡ���������еĸ���״̬(�����Ҫ���epoll������ȡ������Ҫ��¼ǰ���ȡ����ʱ���״̬)f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);
    ngx_http_fastcgi_state_e       state; //��ʶ��������fastcgi 8�ֽ�ͷ���е��Ǹ��ط�
    u_char                        *pos; //ָ��Ҫ�������ݵ�ͷ
    u_char                        *last;//ָ��Ҫ�������ݵ�β��
    ngx_uint_t                     type; //������ʶ������NGX_HTTP_FASTCGI_STDOUT��
    size_t                         length; //����fastcgi��Ϣ�İ������ݳ��� ������padding���
    size_t                         padding; //����˶��ٸ��ֽڣ��Ӷ�8�ֽڶ���

    ngx_chain_t                   *free;
    ngx_chain_t                   *busy;

    unsigned                       fastcgi_stdout:1; //��ʶ���յ�fastcgi stdout��ʶ��Ϣ
    unsigned                       large_stderr:1; //��ʶ���յ�fastcgi stderr��ʶ��Ϣ
    unsigned                       header_sent:1;
    //�����͸�ֵ��ngx_http_fastcgi_process_header  ���һ�ν���fastcgiͷ������Ϣû��ɣ���Ҫ�ٴζ�ȡ������ݽ���
    ngx_array_t                   *split_parts;

    ngx_str_t                      script_name;
    ngx_str_t                      path_info;
} ngx_http_fastcgi_ctx_t; 
//��������¼ÿ�ζ�ȡ���������еĸ���״̬(�����Ҫ���epoll������ȡ������Ҫ��¼ǰ���ȡ����ʱ���״̬)f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

#define NGX_HTTP_FASTCGI_KEEP_CONN      1  //NGX_HTTP_FASTCGI_RESPONDER��ʶ��fastcgi header�е�flagΪ��ֵ��ʾ�ͺ��ʹ�ó�����

//FASTCGI�������̱�ʶ�����Բο�http://my.oschina.net/goal/blog/196599
#define NGX_HTTP_FASTCGI_RESPONDER      1 //����˷������ı�ʶ��Ϣ �ο�ngx_http_fastcgi_create_request  �����ʶЯ�������ӻ��Ƕ�����ngx_http_fastcgi_request_start

#define NGX_HTTP_FASTCGI_BEGIN_REQUEST  1 //����˷������ı�ʶ��Ϣ �ο�ngx_http_fastcgi_create_request  ����ʼ ngx_http_fastcgi_request_start
#define NGX_HTTP_FASTCGI_ABORT_REQUEST  2 
#define NGX_HTTP_FASTCGI_END_REQUEST    3 //��˵�nginx �ο�ngx_http_fastcgi_process_record
#define NGX_HTTP_FASTCGI_PARAMS         4 //����˷������ı�ʶ��Ϣ �ο�ngx_http_fastcgi_create_request �ͻ����������е�HTTP_xx��Ϣ��fastcgi_params����ͨ��������
#define NGX_HTTP_FASTCGI_STDIN          5 //����˷������ı�ʶ��Ϣ �ο�ngx_http_fastcgi_create_request  �ͻ��˷��͵�����˵İ����������ʶ

#define NGX_HTTP_FASTCGI_STDOUT         6 //��˵�nginx �ο�ngx_http_fastcgi_process_record  �ñ�ʶһ���Я�����ݣ�ͨ����������ngx_http_fastcgi_ctx_t->length��ʾ���ݳ���
#define NGX_HTTP_FASTCGI_STDERR         7 //��˵�nginx �ο�ngx_http_fastcgi_process_record
#define NGX_HTTP_FASTCGI_DATA           8  


/*
typedef struct {     
unsigned char version;     
unsigned char type;     
unsigned char requestIdB1;     
unsigned char requestIdB0;     
unsigned char contentLengthB1;     
unsigned char contentLengthB0;     
unsigned char paddingLength;     //����ֽ���
unsigned char reserved;    

unsigned char contentData[contentLength]; //���ݲ���
unsigned char paddingData[paddingLength];  //����ַ�
} FCGI_Record; 

*/
//fastcgi���ĸ�ʽ��ͷ��(8�ֽ�)+����(һ����8����ͷ��+����)+����ֶ�(8�ֽڶ������������ֽ���)  ���Բο�http://my.oschina.net/goal/blog/196599
typedef struct { //������ʱ���Ӧǰ���ngx_http_fastcgi_state_e
    u_char  version;
    u_char  type; //NGX_HTTP_FASTCGI_BEGIN_REQUEST  ��
    u_char  request_id_hi;//���кţ�����Ӧ��һ��һ��
    u_char  request_id_lo;
    u_char  content_length_hi; //�����ֽ���
    u_char  content_length_lo;
    u_char  padding_length; //����ֽ���
    u_char  reserved;//�����ֶ�
} ngx_http_fastcgi_header_t; //   �ο�ngx_http_fastcgi_process_record�������� �������ngx_http_fastcgi_create_request


typedef struct {
    u_char  role_hi;
    u_char  role_lo; //NGX_HTTP_FASTCGI_RESPONDER����0
    u_char  flags;//NGX_HTTP_FASTCGI_KEEP_CONN����0  ��������˺ͺ�˳�����flcf->keep_conn��ΪNGX_HTTP_FASTCGI_KEEP_CONN����Ϊ0����ngx_http_fastcgi_create_request
    u_char  reserved[5];
} ngx_http_fastcgi_begin_request_t;//������ngx_http_fastcgi_request_start_t


typedef struct {
    u_char  version;
    u_char  type;
    u_char  request_id_hi;
    u_char  request_id_lo;
} ngx_http_fastcgi_header_small_t; //������ngx_http_fastcgi_request_start_t


typedef struct {
    ngx_http_fastcgi_header_t         h0;//����ʼͷ��������ͷ�����Ͽ�ʼ�����ͷ����
    ngx_http_fastcgi_begin_request_t  br;
    
    //����ʲô�?Ī������һ������Ĳ������ֵ�ͷ����Ԥ��׷���ڴ�?�ԣ���ΪNGX_HTTP_FASTCGI_PARAMSģʽʱ������ֱ��׷��KV
    ngx_http_fastcgi_header_small_t   h1;
} ngx_http_fastcgi_request_start_t; //��ngx_http_fastcgi_request_start


static ngx_int_t ngx_http_fastcgi_eval(ngx_http_request_t *r,
    ngx_http_fastcgi_loc_conf_t *flcf);
#if (NGX_HTTP_CACHE)
static ngx_int_t ngx_http_fastcgi_create_key(ngx_http_request_t *r);
#endif
static ngx_int_t ngx_http_fastcgi_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_fastcgi_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_fastcgi_body_output_filter(void *data,
    ngx_chain_t *in);
static ngx_int_t ngx_http_fastcgi_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_fastcgi_input_filter_init(void *data);
static ngx_int_t ngx_http_fastcgi_input_filter(ngx_event_pipe_t *p,
    ngx_buf_t *buf);
static ngx_int_t ngx_http_fastcgi_non_buffered_filter(void *data,
    ssize_t bytes);
static ngx_int_t ngx_http_fastcgi_process_record(ngx_http_request_t *r,
    ngx_http_fastcgi_ctx_t *f);
static void ngx_http_fastcgi_abort_request(ngx_http_request_t *r);
static void ngx_http_fastcgi_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

static ngx_int_t ngx_http_fastcgi_add_variables(ngx_conf_t *cf);
static void *ngx_http_fastcgi_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_fastcgi_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_fastcgi_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_fastcgi_init_params(ngx_conf_t *cf,
    ngx_http_fastcgi_loc_conf_t *conf, ngx_http_fastcgi_params_t *params,
    ngx_keyval_t *default_params);

static ngx_int_t ngx_http_fastcgi_script_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_fastcgi_path_info_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_http_fastcgi_ctx_t *ngx_http_fastcgi_split(ngx_http_request_t *r,
    ngx_http_fastcgi_loc_conf_t *flcf);

static char *ngx_http_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_fastcgi_split_path_info(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static char *ngx_http_fastcgi_store(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#if (NGX_HTTP_CACHE)
static char *ngx_http_fastcgi_cache(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_fastcgi_cache_key(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#endif

static char *ngx_http_fastcgi_lowat_check(ngx_conf_t *cf, void *post,
    void *data);


static ngx_conf_post_t  ngx_http_fastcgi_lowat_post =
    { ngx_http_fastcgi_lowat_check };


static ngx_conf_bitmask_t  ngx_http_fastcgi_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_header"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("http_500"), NGX_HTTP_UPSTREAM_FT_HTTP_500 },
    { ngx_string("http_503"), NGX_HTTP_UPSTREAM_FT_HTTP_503 },
    { ngx_string("http_403"), NGX_HTTP_UPSTREAM_FT_HTTP_403 },
    { ngx_string("http_404"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("updating"), NGX_HTTP_UPSTREAM_FT_UPDATING },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


ngx_module_t  ngx_http_fastcgi_module;


static ngx_command_t  ngx_http_fastcgi_commands[] = {
/*
�﷨��fastcgi_pass fastcgi-server 
Ĭ��ֵ��none 
ʹ���ֶΣ�http, server, location 
ָ��FastCGI�����������˿����ַ�������Ǳ�������������
fastcgi_pass   localhost:9000;

ʹ��Unix socket:
fastcgi_pass   unix:/tmp/fastcgi.socket;

ͬ������ʹ��һ��upstream�ֶ����ƣ�
upstream backend  {
  server   localhost:1234;
}
 
fastcgi_pass   backend;
*/
    { ngx_string("fastcgi_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

/*
fastcgi_index 
�﷨��fastcgi_index file 
Ĭ��ֵ��none 
ʹ���ֶΣ�http, server, location 
���URI��б�߽�β���ļ�����׷�ӵ�URI���棬���ֵ���洢�ڱ���$fastcgi_script_name�С����磺

fastcgi_index  index.php;
fastcgi_param  SCRIPT_FILENAME  /home/www/scripts/php$fastcgi_script_name;����"/page.php"�Ĳ���SCRIPT_FILENAME��������Ϊ
"/home/www/scripts/php/page.php"������"/"Ϊ"/home/www/scripts/php/index.php"��
*/
    { ngx_string("fastcgi_index"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, index),
      NULL },
/*
�﷨��fastcgi_split_path_info regex 
ʹ���ֶΣ�location 
���ð汾��0.7.31���ϣ�ʾ����

location ~ ^(.+\.php)(.*)$ {
...
fastcgi_split_path_info ^(.+\.php)(.*)$;
fastcgi_param SCRIPT_FILENAME /path/to/php$fastcgi_script_name;
fastcgi_param PATH_INFO $fastcgi_path_info;
fastcgi_param PATH_TRANSLATED $document_root$fastcgi_path_info;
...
}����"/show.php/article/0001"�Ĳ���SCRIPT_FILENAME������Ϊ"/path/to/php/show.php"������PATH_INFOΪ"/article/0001"��
*/
    { ngx_string("fastcgi_split_path_info"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_split_path_info,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

/*
�﷨��fastcgi_store [on | off | path] 
Ĭ��ֵ��fastcgi_store off 
ʹ���ֶΣ�http, server, location 
�ƶ��˴洢ǰ���ļ���·��������onָ���˽�ʹ��root��aliasָ����ͬ��·����off��ֹ�洢�����⣬�����п���ʹ�ñ���ʹ·��������ȷ��

fastcgi_store   /data/www$original_uri;Ӧ���е�"Last-Modified"ͷ�������ļ�������޸�ʱ�䣬Ϊ��ʹ��Щ�ļ����Ӱ�ȫ�����Խ�����һ��Ŀ¼�д�Ϊ��ʱ�ļ���ʹ��fastcgi_temp_pathָ�
���ָ���������Ϊ��Щ���Ǿ����ı�ĺ�˶�̬����������ؿ����Ĺ����С��磺

location /images/ {
  root                 /data/www;
  error_page           404 = /fetch$uri;
}
 
location /fetch {
  internal;
 
  fastcgi_pass           fastcgi://backend;
  fastcgi_store          on;
  fastcgi_store_access   user:rw  group:rw  all:r;
  fastcgi_temp_path      /data/temp;
 
  alias                  /data/www;
}fastcgi_store�����ǻ��棬ĳЩ��������������һ������
*/   //fastcgi_store��fastcgi_cacheֻ������һ��
    { ngx_string("fastcgi_store"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_store,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

/*
�﷨��fastcgi_store_access users:permissions [users:permission ...] 
Ĭ��ֵ��fastcgi_store_access user:rw 
ʹ���ֶΣ�http, server, location 
�������ָ�������ļ���Ŀ¼��Ȩ�ޣ����磺

fastcgi_store_access  user:rw  group:rw  all:r;���Ҫָ��һ������˵����Ȩ�ޣ����Բ�д�û����磺
fastcgi_store_access  group:rw  all:r;
*/
    { ngx_string("fastcgi_store_access"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
      ngx_conf_set_access_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.store_access),
      NULL },

/*
�﷨��fastcgi_buffers the_number is_size; 
Ĭ��ֵ��fastcgi_buffers 8 4k/8k; 
ʹ���ֶΣ�http, server, location 
�������ָ���˴�FastCGI������������Ӧ�𣬱��ؽ��ö��ٺͶ��Ļ�������ȡ��Ĭ������������ڷ�ҳ��С�����ݻ����Ĳ�ͬ������4K, 8K��16K��
*/
    { ngx_string("fastcgi_buffering"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.buffering),
      NULL },

    //�����Ƿ񻺴�����body ������ʱ  ע��fastcgi_request_buffering��fastcgi_buffering������һ���ǿͻ��˰��壬һ���Ǻ�˷�����Ӧ��İ���
    { ngx_string("fastcgi_request_buffering"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.request_buffering),
      NULL },

/*
�﷨��fastcgi_ignore_client_abort on|off 
Ĭ��ֵ��fastcgi_ignore_client_abort off 
ʹ���ֶΣ�http, server, location 
"�����ǰ��������FastCGI������ʧ�ܣ�Ϊ��ֹ����nginx�������Ͽ����ӣ����������ָ�
*/
    { ngx_string("fastcgi_ignore_client_abort"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.ignore_client_abort),
      NULL },

    { ngx_string("fastcgi_bind"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_bind_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.local),
      NULL },

/*
�﷨��fastcgi_connect_timeout time 
Ĭ��ֵ��fastcgi_connect_timeout 60 
ʹ���ֶΣ�http, server, location 
ָ��ͬFastCGI�����������ӳ�ʱʱ�䣬���ֵ���ܳ���75�롣
*/ //accept��˷�������ʱ���п���connect����NGX_AGAIN����ʾɢ�����ֵ�ack��û�л�����������������ʱ�¼���ʾ���60s��û��ack������������ʱ����
    { ngx_string("fastcgi_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.connect_timeout),
      NULL },
    //��FastCGI��������ĳ�ʱʱ�䣬���ֵ��ָ�Ѿ�����������ֺ���FastCGI��������ĳ�ʱʱ�䡣
    { ngx_string("fastcgi_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("fastcgi_send_lowat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.send_lowat),
      &ngx_http_fastcgi_lowat_post },

/*
�﷨��fastcgi_buffer_size the_size ;
Ĭ��ֵ��fastcgi_buffer_size 4k/8k ;
ʹ���ֶΣ�http, server, location 
�������ָ�����ö��Ļ���������ȡ��FastCGI����������Ӧ��ĵ�һ���֡�
ͨ����˵����������а���һ��С��Ӧ��ͷ��
Ĭ�ϵĻ�������СΪfastcgi_buffersָ���е�ÿ���С�����Խ����ֵ���ø�С��
*/ //ͷ���в���(Ҳ���ǵ�һ��fastcgi data��ʶ��Ϣ������Ҳ��Я��һ������ҳ����)��fastcgi��ʶ��Ϣ���ٵĿռ���buffer_size����ָ��
//ngx_http_upstream_process_header�з���fastcgi_buffer_sizeָ���Ŀռ�
    { ngx_string("fastcgi_buffer_size"),  //ע��ͺ����fastcgi_buffers������  //ָ���Ŀռ俪����ngx_http_upstream_process_header
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("fastcgi_pass_request_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.pass_request_headers),
      NULL },

    //�Ƿ�ת���ͻ�������������İ��嵽���ȥ
    { ngx_string("fastcgi_pass_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.pass_request_body),
      NULL },

/*
�﷨��fastcgi_intercept_errors on|off 
Ĭ��ֵ��fastcgi_intercept_errors off 
ʹ���ֶΣ�http, server, location 
���ָ��ָ���Ƿ񴫵�4xx��5xx������Ϣ���ͻ��ˣ���������nginxʹ��error_page���������Ϣ��
�������ȷ����error_page��ָ��������ʹ���������Ч������Igor��˵�����û���ʵ��Ĵ�������nginx��������һ�������������
������ʾ�Լ���Ĭ��ҳ�棬��������ͨ��ĳЩ�������ش���
*/
    { ngx_string("fastcgi_intercept_errors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.intercept_errors),
      NULL },
/*
�﷨��fastcgi_read_timeout time 
Ĭ��ֵ��fastcgi_read_timeout 60 
ʹ���ֶΣ�http, server, location 
ǰ��FastCGI����������Ӧ��ʱʱ�䣬�����һЩֱ�������������������ĳ�ʱ�����е�FastCGI���̣������ڴ�����־�г���ǰ�˷�������Ӧ��
ʱ���󣬿�����Ҫ�������ֵ��
*/ //����FastCGIӦ��ĳ�ʱʱ�䣬���ֵ��ָ�Ѿ�����������ֺ����FastCGIӦ��ĳ�ʱʱ�䡣
    { ngx_string("fastcgi_read_timeout"), //��ȡ������ݵĳ�ʱʱ��
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.read_timeout),
      NULL },

    /* ��ȡ���˵ĵ�һ��fastcgi data��ʶ���ݺ�(һ����ͷ���кͲ������ݣ��ò��ֿռ��С��fastcgi_buffer_size)ָ���������ҳ����Ƚϴ�
        ����Ҫ���ٶ���µ�buff���洢���岿�֣�����fastcgi_buffers 5 3K����ʾ����ⲿ�ֿռ�����(�������ٶȿ죬���ͻ����ٶ���)�����ڽ���
        ������ݣ��ȴ���5��3K�еĲ��ַ��ͳ�ȥ���������ÿ�������Ŀռ���պ������
     */ //ע�����ֻ��buffing��ʽ��Ч  //���������õĿռ���������ռ���//��ngx_event_pipe_read_upstream�д����ռ�
    { ngx_string("fastcgi_buffers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_bufs_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.bufs),
      NULL },

    //xxx_buffersָ��Ϊ���պ�˷�����������࿪����ô��ռ䣬xxx_busy_buffers_sizeָ��һ�η��ͺ��п�������û��ȫ�����ͳ�ȥ����˷���busy����
    //��û�з��ͳ�ȥ��busy���е�bufָ�������(����bufָ��Ŀռ�δ���͵�����)�ﵽxxx_busy_buffers_size�Ͳ��ܴӺ�˶�ȡ���ݣ�ֻ��busy���е����ݷ���һ���ֳ�ȥ��С��xxx_busy_buffers_size���ܼ�����ȡ
    //buffring��ʽ����Ч����Ч�ط����Բο�ngx_event_pipe_write_to_downstream
    { ngx_string("fastcgi_busy_buffers_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.busy_buffers_size_conf),
      NULL },

    { ngx_string("fastcgi_force_ranges"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.force_ranges),
      NULL },

    { ngx_string("fastcgi_limit_rate"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.limit_rate),
      NULL },

#if (NGX_HTTP_CACHE)
/*
       nginx�Ĵ洢ϵͳ�����࣬һ����ͨ��proxy_store�����ģ��洢��ʽ�ǰ���url�е��ļ�·�����洢�ڱ��ء�����/file/2013/0001/en/test.html��
     ��ônginx�ͻ���ָ���Ĵ洢Ŀ¼�����ν�������Ŀ¼���ļ�����һ����ͨ��proxy_cache���������ַ�ʽ�洢���ļ����ǰ���url·������֯�ģ�
     ����ʹ��һЩ���ⷽʽ�������(�����Ϊ�Զ��巽ʽ)���Զ��巽ʽ��������Ҫ�ص�����ġ���ô�����ַ�ʽ����ʲô�����أ�


    ��url·���洢�ļ��ķ�ʽ�������������Ƚϼ򵥣��������ܲ��С������е�url�޳�������Ҫ�ڱ����ļ�ϵͳ�Ͻ���������Ŀ¼����ô�ļ��Ĵ�
    �Ͳ��Ҷ��ܻ����(����kernel��ͨ��·��������inode�Ĺ��̰�)�����ʹ���Զ��巽ʽ������ģʽ������Ҳ�벻���ļ���·����������������url����
    ���������������Ӻ����ܵĽ��͡���ĳ��������˵����һ���û�̬�ļ�ϵͳ������͵�Ӧ������squid�е�CFS��nginxʹ�õķ�ʽ��Լ򵥣���Ҫ����
    url��md5ֵ������
     */

/*
�﷨��fastcgi_cache zone|off; 
Ĭ��ֵ��off 
ʹ���ֶΣ�http, server, location 
Ϊ����ʵ��ʹ�õĹ����ڴ�ָ��һ��������ͬ������������ڲ�ͬ�ĵط���
*/   //fastcgi_store��fastcgi_cacheֻ������һ��
//xxx_cache(proxy_cache fastcgi_cache) abc����xxx_cache_path(proxy_cache_path fastcgi_cache_path) xxx keys_zone=abc:10m;һ�𣬷�����ngx_http_proxy_merge_loc_conf��ʧ�ܣ���Ϊû��Ϊ��abc����ngx_http_file_cache_t
//fastcgi_cache ָ��ָ�����ڵ�ǰ��������ʹ���ĸ�����ά��������Ŀ��������Ӧ�Ļ������������ fastcgi_cache_path ָ��塣 
//��ȡ�ýṹngx_http_upstream_cache_get��ʵ������ͨ��proxy_cache xxx����fastcgi_cache xxx����ȡ�����ڴ�����ģ���˱�������proxy_cache����fastcgi_cache

    { ngx_string("fastcgi_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_cache,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },
/*
�﷨��fastcgi_cache_key line
Ĭ��ֵ��none 
ʹ���ֶΣ�http, server, location 
���û���Ĺؼ��֣��磺

fastcgi_cache_key localhost:9000$request_uri;
*/ //proxy��fastcgi����:Default:  proxy_cache_key $scheme$proxy_host$request_uri; fastcgi_cache_keyû��Ĭ��ֵ
    { ngx_string("fastcgi_cache_key"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_fastcgi_cache_key,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

/*
�﷨��fastcgi_cache_path path [levels=m:n] keys_zone=name:size [inactive=time] [max_size=size] 
Ĭ��ֵ��none 
ʹ���ֶΣ�http 
clean_time������0.7.45�汾���Ѿ��Ƴ���
���ָ��ָ��FastCGI�����·���Լ�������һЩ���������е��������ļ�����ʽ�洢������Ĺؼ���(key)���ļ���Ϊ�����url�������MD5ֵ��
Level�������û���Ŀ¼��Ŀ¼�ּ��Լ���Ŀ¼������������ָ���������Ϊ��

fastcgi_cache_path  /data/nginx/cache  levels=1:2   keys_zone=one:10m;��ô�����ļ����洢Ϊ��
����levels 2:2������256*256����Ŀ¼��keys_zone���������ռ������
/data/nginx/cache/c/29/b7f54b2df7773722d382f4809d65029c�����е��ļ����ȱ�д��һ����ʱ�ļ���������ƶ�������Ŀ¼�����λ�ã�0.8.9�汾
֮����Խ���ʱ�ļ��ͻ����ļ��洢�ڲ�ͬ���ļ�ϵͳ��������Ҫ���������ƶ������Ǽ򵥵�ԭ��������ϵͳ���ã����������ļ��Ŀ������������
��fastcgi_temp_path��fastcgi_cache_path��ֵ��ʹ����ͬ���ļ�ϵͳ��
���⣬���л�Ĺؼ��ּ����������Ϣ���洢�ڹ����ڴ�أ����ֵ�����ƺʹ�Сͨ��key_zone����ָ����inactive����ָ�����ڴ��е����ݴ洢ʱ�䣬Ĭ��Ϊ10���ӡ�
max_size�������û�������ֵ��һ��ָ����cache manager���̽������Ե�ɾ���ɵĻ������ݡ�
*/ //XXX_cache��������д��xxx_temp_path���Ƶ�xxx_cache_path������������Ŀ¼�����ͬһ������
//xxx_cache(proxy_cache fastcgi_cache) abc����xxx_cache_path(proxy_cache_path fastcgi_cache_path) xxx keys_zone=abc:10m;һ�𣬷�����ngx_http_proxy_merge_loc_conf��ʧ�ܣ���Ϊû��Ϊ��abc����ngx_http_file_cache_t
//fastcgi_cache ָ��ָ�����ڵ�ǰ��������ʹ���ĸ�����ά��������Ŀ��������Ӧ�Ļ������������ fastcgi_cache_path ָ��塣 
//��ȡ�ýṹngx_http_upstream_cache_get��ʵ������ͨ��proxy_cache xxx����fastcgi_cache xxx����ȡ�����ڴ�����ģ���˱�������proxy_cache����fastcgi_cache

/*
�ǻ��淽ʽ(p->cacheable=0)p->temp_file->path = u->conf->temp_path; ��ngx_http_fastcgi_temp_pathָ��·��
���淽ʽ(p->cacheable=1) p->temp_file->path = r->cache->file_cache->temp_path;��proxy_cache_path����fastcgi_cache_path use_temp_path=ָ��·�� 
��ngx_http_upstream_send_response 

��ǰfastcgi_buffers ��fastcgi_buffer_size���õĿռ䶼�Ѿ������ˣ�����Ҫ������д����ʱ�ļ���ȥ���ο�ngx_event_pipe_read_upstream
*/  //��ngx_http_file_cache_update���Կ��������������д����ʱ�ļ�����д��xxx_cache_path�У���ngx_http_file_cache_update
    { ngx_string("fastcgi_cache_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_2MORE,
      ngx_http_file_cache_set_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_main_conf_t, caches),
      &ngx_http_fastcgi_module },

    //XXX_cache_bypass  xx1 xx2���õ�xx2��Ϊ�ջ��߲�Ϊ0���򲻻�ӻ�����ȡ������ֱ�ӳ��˶�ȡ  ������Щ����ĺ����Ӧ������Ȼ���Ա� upstream ģ�黺�档 
    //XXX_no_cache  xx1 xx2���õ�xx2��Ϊ�ջ��߲�Ϊ0�����˻��������ݲ��ᱻ����
    { ngx_string("fastcgi_cache_bypass"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_set_predicate_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_bypass),
      NULL },

/*
�﷨��fastcgi_no_cache variable [...]
Ĭ��ֵ��None 
ʹ���ֶΣ�http, server, location 
ȷ���ں�������»����Ӧ�𽫲���ʹ�ã�ʾ����

fastcgi_no_cache $cookie_nocache  $arg_nocache$arg_comment;
fastcgi_no_cache $http_pragma     $http_authorization;���Ϊ���ַ������ߵ���0�����ʽ��ֵ����false�����磬�����������У����
��������������cookie "nocache"�����潫���ƹ���
*/
    //XXX_cache_bypass  xx1 xx2���õ�xx2��Ϊ�ջ��߲�Ϊ0���򲻻�ӻ�����ȡ������ֱ�ӳ��˶�ȡ
    //XXX_no_cache  xx1 xx2���õ�xx2��Ϊ�ջ��߲�Ϊ0�����˻��������ݲ��ᱻ����
    { ngx_string("fastcgi_no_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_set_predicate_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.no_cache),
      NULL },

/*
�﷨��fastcgi_cache_valid [http_error_code|time] 
Ĭ��ֵ��none 
ʹ���ֶΣ�http, server, location 
Ϊָ����http���ش���ָ������ʱ�䣬���磺

fastcgi_cache_valid  200 302  10m;
fastcgi_cache_valid  404      1m;����Ӧ״̬��Ϊ200��302����10���ӣ�404����1���ӡ�
Ĭ������»���ֻ����200��301��302��״̬��
ͬ��Ҳ������ָ����ʹ��any��ʾ�κ�һ����

fastcgi_cache_valid  200 302 10m;
fastcgi_cache_valid  301 1h;
fastcgi_cache_valid  any 1m;
*/
    /*
       ע��open_file_cache inactive=20s��fastcgi_cache_valid 20s������ǰ��ָ��������ͻ�����20s��û�������������Ѹû����ļ���Ӧ��stat������Ϣ
       ��ngx_open_file_cache_t->rbtree(expire_queue)��ɾ��(�ͻ��˵�һ�������uri��Ӧ�Ļ����ļ���ʱ���Ѹ��ļ���Ӧ��stat��Ϣ�ڵ�ngx_cached_open_file_s��ӵ�
       ngx_open_file_cache_t->rbtree(expire_queue)��)���Ӷ���߻�ȡ�����ļ���Ч��
       fastcgi_cache_validָ���Ǻ�ʱ�����ļ����ڣ�������ɾ������ʱִ��ngx_cache_manager_process_handler->ngx_http_file_cache_manager
    */ //�����˳��󻺴��ļ��ᱻȫ���������ʹû�е���
 
    { ngx_string("fastcgi_cache_valid"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_file_cache_valid_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_valid),
      NULL },
/*
�﷨��fastcgi_cache_min_uses n 
Ĭ��ֵ��fastcgi_cache_min_uses 1 
ʹ���ֶΣ�http, server, location 
ָ��ָ���˾������ٴ��������ͬURL�������档
*/
    { ngx_string("fastcgi_cache_min_uses"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_min_uses),
      NULL },

/*
�﷨��proxy_cache_use_stale [error|timeout|updating|invalid_header|http_500|http_502|http_503|http_504|http_404|off] [��]; 
Ĭ��ֵ��proxy_cache_use_stale off; 
ʹ���ֶΣ�http, server, location 
���ָ�����nginx��ʱ�Ӵ��������ṩһ�����ڵ���Ӧ������������proxy_next_upstreamָ�
Ϊ�˷�ֹ����ʧЧ���ڶ���߳�ͬʱ���±��ػ���ʱ���������ָ��'updating'������������ֻ֤��һ���߳�ȥ���»��棬���������
�̸߳��»���Ĺ������������߳�ֻ����Ӧ��ǰ�����еĹ��ڰ汾��
*/ 
/*
�������������fastcgi_cache_use_stale updating����ʾ˵��Ȼ�û����ļ�ʧЧ�ˣ��Ѿ��������ͻ��������ڻ�ȡ������ݣ����Ǹÿͻ����������ڻ�û�л�ȡ������
��ʱ��Ϳ��԰���ǰ���ڵĻ��淢�͸���ǰ����Ŀͻ��� //�������ngx_http_upstream_cache�Ķ�
*/
    { ngx_string("fastcgi_cache_use_stale"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_use_stale),
      &ngx_http_fastcgi_next_upstream_masks },

/*
�﷨��fastcgi_cache_methods [GET HEAD POST]; 
Ĭ��ֵ��fastcgi_cache_methods GET HEAD; 
ʹ���ֶΣ�main,http,location 
�޷�����GET/HEAD ����ʹ��ֻ���������ã�
fastcgi_cache_methods  POST;
*/
    { ngx_string("fastcgi_cache_methods"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_methods),
      &ngx_http_upstream_cache_method_mask },

/*
�����Ҫ���һ������:
���������������ͻ��ˣ�һ���ͻ������ڻ�ȡ������ݣ����Һ�˷�����һ���֣���nginx�Ỻ����һ���֣����ҵȴ����к�����ݷ��ؼ������档
�����ڻ���Ĺ���������ͻ���2ҳ������ȥͬ��������uri�ȶ�һ�������ȥ���ͻ��˻���һ������ݣ���ʱ��Ϳ���ͨ�������������������⣬
Ҳ���ǿͻ���1��û������ȫ�����ݵĹ����пͻ���2ֻ�еȿͻ���1��ȡ��ȫ��������ݣ����߻�ȡ��proxy_cache_lock_timeout��ʱ����ͻ���2ֻ�дӺ�˻�ȡ����
*/
    { ngx_string("fastcgi_cache_lock"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_lock),
      NULL },

    { ngx_string("fastcgi_cache_lock_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_lock_timeout),
      NULL },

    { ngx_string("fastcgi_cache_lock_age"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_lock_age),
      NULL },

    { ngx_string("fastcgi_cache_revalidate"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.cache_revalidate),
      NULL },

#endif
        /*
    Ĭ�������p->temp_file->path = u->conf->temp_path; Ҳ������ngx_http_fastcgi_temp_pathָ��·������������ǻ��淽ʽ(p->cacheable=1)��������
    proxy_cache_path(fastcgi_cache_path) /a/b��ʱ�����use_temp_path=off(��ʾ��ʹ��ngx_http_fastcgi_temp_path���õ�path)��
    ��p->temp_file->path = r->cache->file_cache->temp_path; Ҳ������ʱ�ļ�/a/b/temp��use_temp_path=off��ʾ��ʹ��ngx_http_fastcgi_temp_path
    ���õ�·������ʹ��ָ������ʱ·��/a/b/temp   ��ngx_http_upstream_send_response 
    */
/*������ݶ�ȡ��ϣ�����ȫ��д����ʱ�ļ���Ż�ִ��rename���̣�Ϊʲô��Ҫ��ʱ�ļ���ԭ����:����֮ǰ�Ļ�������ˣ������и��������ڴӺ��
��ȡ����д����ʱ�ļ��������ֱ��д�뻺���ļ������ڻ�ȡ������ݹ����У��������һ���ͻ��������������proxy_cache_use_stale updating����
������������ֱ�ӻ�ȡ֮ǰ�ϾɵĹ��ڻ��棬�Ӷ����Ա����ͻ(ǰ�������д�ļ�������������ȡ�ļ�����) 
*/
    ////XXX_cache��������д��xxx_temp_path���Ƶ�xxx_cache_path������������Ŀ¼�����ͬһ������
    { ngx_string("fastcgi_temp_path"), //��ngx_http_file_cache_update���Կ��������������д����ʱ�ļ�����д��xxx_cache_path�У���ngx_http_file_cache_update
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1234,
      ngx_conf_set_path_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.temp_path),
      NULL },

/*
��buffering��־λΪ1ʱ����������ٶȿ��������ٶȣ����п��ܰ��������ε���Ӧ�洢����ʱ�ļ��У���max_temp_file_sizeָ������ʱ�ļ���
��󳤶ȡ�ʵ���ϣ���������ngx_event_pipe_t�ṹ���е�temp_file
*/
    { ngx_string("fastcgi_max_temp_file_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.max_temp_file_size_conf),
      NULL },
//��ʾ���������е���Ӧд����ʱ�ļ�ʱһ��д���ַ�������󳤶�
    { ngx_string("fastcgi_temp_file_write_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.temp_file_write_size_conf),
      NULL },
/*
�﷨��fastcgi_next_upstream error|timeout|invalid_header|http_500|http_503|http_404|off 
Ĭ��ֵ��fastcgi_next_upstream error timeout 
ʹ���ֶΣ�http, server, location 
ָ��ָ������������󽫱�ת������һ��FastCGI��������
��error �� �����е�����������ڶ�ȡӦ��ͷ�����������ӷ�������ʱ��������
��timeout �� �����е�����������ڶ�ȡӦ��ͷ�����������ӷ�������ʱ��ʱ��
��invalid_header �� ���������ؿյĻ�����Ч��Ӧ��
��http_500 �� ����������500Ӧ����롣
��http_503 �� ����������503Ӧ����롣
��http_404 �� ����������404Ӧ����롣
��off �� ��ֹ�����͵���һ��FastCGI��������
ע�⴫�������ڴ��͵���һ��������֮ǰ�����Ѿ����յ����ݴ��͵��˿ͻ��ˣ����ԣ���������ݴ������д�����߳�ʱ���������ָ�����
�޷��޸�һЩ���ʹ���
*/
    { ngx_string("fastcgi_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.next_upstream),
      &ngx_http_fastcgi_next_upstream_masks },

    { ngx_string("fastcgi_next_upstream_tries"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.next_upstream_tries),
      NULL },

    { ngx_string("fastcgi_next_upstream_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.next_upstream_timeout),
      NULL },
/*
�﷨��fastcgi_param parameter value 
Ĭ��ֵ��none 
ʹ���ֶΣ�http, server, location 
ָ��һЩ���ݵ�FastCGI�������Ĳ�����
����ʹ���ַ�������������������ϣ���������ò���̳е��������ֶΣ������ڵ�ǰ�ֶλ�������κ�֮ǰ�Ķ��塣
������һ��PHP��Ҫʹ�õ����ٲ�����

  fastcgi_param  SCRIPT_FILENAME  /home/www/scripts/php$fastcgi_script_name;
  fastcgi_param  QUERY_STRING     $query_string;
  PHPʹ��SCRIPT_FILENAME����������Ҫִ���ĸ��ű���QUERY_STRING���������е�ĳЩ������
���Ҫ����POST��������Ҫ������������������

  fastcgi_param  REQUEST_METHOD   $request_method;
  fastcgi_param  CONTENT_TYPE     $content_type;
  fastcgi_param  CONTENT_LENGTH   $content_length;���PHP�ڱ���ʱ����--enable-force-cgi-redirect������봫��ֵΪ200��REDIRECT_STATUS������

fastcgi_param  REDIRECT_STATUS  200;
*/
    { ngx_string("fastcgi_param"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE23,
      ngx_http_upstream_param_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, params_source),
      NULL },

    { ngx_string("fastcgi_pass_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.pass_headers),
      NULL },
/*
�﷨��fastcgi_hide_header name 
ʹ���ֶΣ�http, server, location 
Ĭ�������nginx���Ὣ����FastCGI��������"Status"��"X-Accel-..."ͷ���͵��ͻ��ˣ��������Ҳ��������ĳЩ������ͷ��
������봫��"Status"��"X-Accel-..."ͷ�������ʹ��fastcgi_pass_headerǿ���䴫�͵��ͻ��ˡ�
*/
    { ngx_string("fastcgi_hide_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.hide_headers),
      NULL },
/*
�﷨��fastcgi_ignore_headers name [name...] 
ʹ���ֶΣ�http, server, location 
���ָ���ֹ����һЩFastCGI������Ӧ���ͷ���ֶΣ��������ָ����"X-Accel-Redirect", "X-Accel-Expires", "Expires"��"Cache-Control"�ȡ�
If not disabled, processing of these header fields has the following effect:

��X-Accel-Expires��, ��Expires��, ��Cache-Control��, ��Set-Cookie��, and ��Vary�� set the parameters of response caching;
��X-Accel-Redirect�� performs an internal redirect to the specified URI;
��X-Accel-Limit-Rate�� sets the rate limit for transmission of a response to a client;
��X-Accel-Buffering�� enables or disables buffering of a response;
��X-Accel-Charset�� sets the desired charset of a response.
*/
    { ngx_string("fastcgi_ignore_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, upstream.ignore_headers),
      &ngx_http_upstream_ignore_headers_masks },

/*
Sets a string to search for in the error stream of a response received from a FastCGI server. If the string is found then it is 
considered that the FastCGI server has returned an invalid response. This allows handling application errors in nginx, for example: 

location /php {
    fastcgi_pass backend:9000;
    ...
    fastcgi_catch_stderr "PHP Fatal error";
    fastcgi_next_upstream error timeout invalid_header;
}
*/
    { ngx_string("fastcgi_catch_stderr"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, catch_stderr),
      NULL },

    { ngx_string("fastcgi_keep_conn"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_fastcgi_loc_conf_t, keep_conn),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_fastcgi_module_ctx = {
    ngx_http_fastcgi_add_variables,        /* preconfiguration */
    NULL,                                  /* postconfiguration */

    ngx_http_fastcgi_create_main_conf,     /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_fastcgi_create_loc_conf,      /* create location configuration */
    ngx_http_fastcgi_merge_loc_conf        /* merge location configuration */
};

/*
step1. web �������յ��ͻ��ˣ��������������Http Request������CGI���򣬲�ͨ��������������׼���봫������
step2. cgi�����������������������ã���ҵ��������ã������������������������ݿ�����������߼������
step3. cgi�̽�������ͨ����׼�������׼���󣬴��ݸ�web ������
step4. web �������յ�cgi���صĽ��������Http Response���ظ��ͻ��ˣ���ɱ��cgi����

http://blog.sina.com.cn/s/blog_4d8cf3140101pa8c.html
FastCGI ����Ҫ�ŵ��ǰѶ�̬���Ժ�HTTP Server���뿪��������Nginx��PHP/PHP-FPM
�����������ڲ�ͬ�ķ������ϣ��Էֵ�ǰ��Nginx��������ѹ����ʹNginx
רһ����̬�����ת����̬���󣬶�PHP/PHP-FPM������רһ����PHP��̬����

*/ //http://chenzhenianqing.cn/articles/category/%e5%90%84%e7%a7%8dserver/nginx
ngx_module_t  ngx_http_fastcgi_module = {
    NGX_MODULE_V1,
    &ngx_http_fastcgi_module_ctx,          /* module context */
    ngx_http_fastcgi_commands,             /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_fastcgi_request_start_t  ngx_http_fastcgi_request_start = { //�ο�ngx_http_fastcgi_create_request
    //��ͷ����ʾnginx��ʼ����, ������FCGI_BEGIN_REQUEST��ʼ
    { 1,                                               /* version */
      NGX_HTTP_FASTCGI_BEGIN_REQUEST,                  /* type */
      0,                                               /* request_id_hi */
      1,                                               /* request_id_lo */
      0,                                               /* content_length_hi */
      sizeof(ngx_http_fastcgi_begin_request_t),        /* content_length_lo */
      0,                                               /* padding_length */
      0 },                                             /* reserved */

    //��ͷ��˵���Ƿ�ͺ�˲��ó�����
    { 0,                                               /* role_hi */
      NGX_HTTP_FASTCGI_RESPONDER,                      /* role_lo */
      0, /* NGX_HTTP_FASTCGI_KEEP_CONN */              /* flags */
      { 0, 0, 0, 0, 0 } },                             /* reserved[5] */

    //params����ͷ����ǰ4�ֽڣ�ʣ���ȫ���ڲ�����һ����䣬���Բο�ngx_http_fastcgi_create_request
    { 1,                                               /* version */
      NGX_HTTP_FASTCGI_PARAMS,                         /* type */
      0,                                               /* request_id_hi */
      1 },                                             /* request_id_lo */

};


static ngx_http_variable_t  ngx_http_fastcgi_vars[] = {

    { ngx_string("fastcgi_script_name"), NULL,
      ngx_http_fastcgi_script_name_variable, 0,
      NGX_HTTP_VAR_NOCACHEABLE|NGX_HTTP_VAR_NOHASH, 0 },

    { ngx_string("fastcgi_path_info"), NULL,
      ngx_http_fastcgi_path_info_variable, 0,
      NGX_HTTP_VAR_NOCACHEABLE|NGX_HTTP_VAR_NOHASH, 0 },

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};

//������ӵ���ngx_http_upstream_conf_t->hide_headers_hash����  ����Ҫ���͸��ͻ���
static ngx_str_t  ngx_http_fastcgi_hide_headers[] = {
    ngx_string("Status"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_null_string
};


#if (NGX_HTTP_CACHE)

static ngx_keyval_t  ngx_http_fastcgi_cache_headers[] = {
    { ngx_string("HTTP_IF_MODIFIED_SINCE"),
      ngx_string("$upstream_cache_last_modified") },
    { ngx_string("HTTP_IF_UNMODIFIED_SINCE"), ngx_string("") },
    { ngx_string("HTTP_IF_NONE_MATCH"), ngx_string("$upstream_cache_etag") },
    { ngx_string("HTTP_IF_MATCH"), ngx_string("") },
    { ngx_string("HTTP_RANGE"), ngx_string("") },
    { ngx_string("HTTP_IF_RANGE"), ngx_string("") },
    { ngx_null_string, ngx_null_string }
};

#endif


static ngx_path_init_t  ngx_http_fastcgi_temp_path = {
    ngx_string(NGX_HTTP_FASTCGI_TEMP_PATH), { 1, 2, 0 }
};

/*
ngx_http_fastcgi_handler������Ϊnginx��ȡ�����headerͷ����,�ͻ����ngx_http_core_content_phase��һ�����õ�������Կ���upstream��û�е���
��ʵupstream������Щfastcgiģ�����proxyģ��ʹ�õġ�����˵����ʹ�ã�fastcgi����upstream��������صĻص���Ȼ��upstream�������Щ�ص���ɹ���
*/
static ngx_int_t
ngx_http_fastcgi_handler(ngx_http_request_t *r)
{//FCGI�������,ngx_http_core_run_phases���浱��һ�����ݴ���ģ����õġ�(NGX_HTTP_CONTENT_PHASE�׶�ִ��)��ʵ�ʸ�ֵ��:
//ngx_http_core_find_config_phase�����ngx_http_update_location_config���á��������øú����ĵط���ngx_http_core_content_phase->ngx_http_finalize_request(r, r->content_handler(r)); 
    ngx_int_t                      rc;
    ngx_http_upstream_t           *u;
    ngx_http_fastcgi_ctx_t        *f;
    ngx_http_fastcgi_loc_conf_t   *flcf;
#if (NGX_HTTP_CACHE)
    ngx_http_fastcgi_main_conf_t  *fmcf;
#endif

    //����һ��ngx_http_upstream_t�ṹ���ŵ�r->upstream����ȥ��
    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    f = ngx_pcalloc(r->pool, sizeof(ngx_http_fastcgi_ctx_t)); //����fastcgi������upstream������ngx_http_fastcgi_ctx_t
    if (f == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_http_set_ctx(r, f, ngx_http_fastcgi_module); //���������еĲ�����������ctx�⣬������Ҫ�Ĳ���Ҳ����ͨ��r->ctx[]���������ã��Ӷ����Եõ����棬ֻҪ֪��r���Ϳ���ͨ��r->ctx[]��ȡ��

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);//�õ�fcgi�����á�(r)->loc_conf[module.ctx_index]

    if (flcf->fastcgi_lengths) {//������fcgi�б�������ô����Ҫ����һ�±�����
        if (ngx_http_fastcgi_eval(r, flcf) != NGX_OK) { //����fastcgi_pass   127.0.0.1:9000;�����URL�����ݡ�Ҳ������������
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    u = r->upstream; //��������ngx_http_upstream_create�д�����

    ngx_str_set(&u->schema, "fastcgi://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_fastcgi_module;

    u->conf = &flcf->upstream;

#if (NGX_HTTP_CACHE)
    fmcf = ngx_http_get_module_main_conf(r, ngx_http_fastcgi_module);

    u->caches = &fmcf->caches;
    u->create_key = ngx_http_fastcgi_create_key;
#endif

    u->create_request = ngx_http_fastcgi_create_request; //��ngx_http_upstream_init_request��ִ��
    u->reinit_request = ngx_http_fastcgi_reinit_request; //��ngx_http_upstream_reinit��ִ��
    u->process_header = ngx_http_fastcgi_process_header; //��ngx_http_upstream_process_header��ִ��
    u->abort_request = ngx_http_fastcgi_abort_request;  
    u->finalize_request = ngx_http_fastcgi_finalize_request; //��ngx_http_upstream_finalize_request��ִ��
    r->state = 0;

    //��������ݽṹ�Ǹ�event_pipe�õģ�������FCGI�����ݽ���buffering����ġ�
    u->buffering = flcf->upstream.buffering; //Ĭ��Ϊ1
    
    u->pipe = ngx_pcalloc(r->pool, sizeof(ngx_event_pipe_t));
    if (u->pipe == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    //���ö�ȡfcgiЭ���ʽ���ݵĻص��������������\r\n\r\n��ͷ����FCGI���󣬺���İ�������������������д���
    u->pipe->input_filter = ngx_http_fastcgi_input_filter;
    u->pipe->input_ctx = r;

    u->input_filter_init = ngx_http_fastcgi_input_filter_init;
    u->input_filter = ngx_http_fastcgi_non_buffered_filter;
    u->input_filter_ctx = r;

    if (!flcf->upstream.request_buffering
        && flcf->upstream.pass_request_body)
    { //�����Ҫ͸�����Ҳ���Ҫ���ɰ���
        r->request_body_no_buffering = 1;
    }

    //��ȡ�������
    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init); //��ȡ��ͻ��˷������İ����ִ��ngx_http_upstream_init

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}

//����fastcgi_pass   127.0.0.1:9000;�����URL���ݣ����õ�u->resolved����ȥ
static ngx_int_t
ngx_http_fastcgi_eval(ngx_http_request_t *r, ngx_http_fastcgi_loc_conf_t *flcf)
{
    ngx_url_t             url;
    ngx_http_upstream_t  *u;

    ngx_memzero(&url, sizeof(ngx_url_t));
    //����lcodes��codes����Ŀ���ַ��������ݡ�Ŀ���ַ�����������value->data;���棬Ҳ����url.url
    if (ngx_http_script_run(r, &url.url, flcf->fastcgi_lengths->elts, 0,
                            flcf->fastcgi_values->elts)
        == NULL)
    {
        return NGX_ERROR;
    }

    url.no_resolve = 1;

    if (ngx_parse_url(r->pool, &url) != NGX_OK) {//��u���������url,unix,inet6�ȵ�ַ���м�����
         if (url.err) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "%s in upstream \"%V\"", url.err, &url.url);
        }

        return NGX_ERROR;
    }

    u = r->upstream;

    u->resolved = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_resolved_t));
    if (u->resolved == NULL) {
        return NGX_ERROR;
    }

    if (url.addrs && url.addrs[0].sockaddr) {
        u->resolved->sockaddr = url.addrs[0].sockaddr;
        u->resolved->socklen = url.addrs[0].socklen;
        u->resolved->naddrs = 1;
        u->resolved->host = url.addrs[0].name;

    } else {
        u->resolved->host = url.host;
        u->resolved->port = url.port;
        u->resolved->no_port = url.no_port;
    }

    return NGX_OK;
}


#if (NGX_HTTP_CACHE)

//����fastcgi_cache_key xxx ����ֵ��r->cache->keys
static ngx_int_t //ngx_http_upstream_cache��ִ��
ngx_http_fastcgi_create_key(ngx_http_request_t *r)
{//����֮ǰ�ڽ���scgi_cache_keyָ���ʱ���������ĸ��ӱ��ʽ�ṹ�������flcf->cache_key�еģ������cache_key��
    ngx_str_t                    *key;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    key = ngx_array_push(&r->cache->keys);
    if (key == NULL) {
        return NGX_ERROR;
    }

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    //��flcf->cache_key(fastcgi_cache_key������)�н�������Ӧcode��������ر����ַ������浽 r->cache->keys
    if (ngx_http_complex_value(r, &flcf->cache_key, key) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

#endif

/*
2025/03/22 03:55:55[    ngx_http_core_post_access_phase,  2163]  [debug] 2357#2357: *3 post access phase: 8 (NGX_HTTP_POST_ACCESS_PHASE)
2025/03/22 03:55:55[        ngx_http_core_content_phase,  2485]  [debug] 2357#2357: *3 content phase(content_handler): 9 (NGX_HTTP_CONTENT_PHASE)
2025/03/22 03:55:55[             ngx_http_upstream_init,   617]  [debug] 2357#2357: *3 http init upstream, client timer: 0
2025/03/22 03:55:55[                ngx_epoll_add_event,  1398]  [debug] 2357#2357: *3 epoll add event: fd:3 op:3 ev:80002005
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "SCRIPT_FILENAME"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "/var/yyz/www"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "/"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "/test.php"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "SCRIPT_FILENAME: /var/yyz/www//test.php"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "QUERY_STRING" //�ձ�����ûvalue��Ҳ�����ˣ��������if_no_emputy�����Ͳ��ᷢ��
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "QUERY_STRING: "
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "REQUEST_METHOD"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "GET"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "REQUEST_METHOD: GET"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "CONTENT_TYPE"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "CONTENT_TYPE: "
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "CONTENT_LENGTH"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "CONTENT_LENGTH: "
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "SCRIPT_NAME"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "/test.php"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "SCRIPT_NAME: /test.php"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "REQUEST_URI"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "/test.php"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "REQUEST_URI: /test.php"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "DOCUMENT_URI"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "/test.php"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "DOCUMENT_URI: /test.php"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "DOCUMENT_ROOT"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "/var/yyz/www"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "DOCUMENT_ROOT: /var/yyz/www"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "SERVER_PROTOCOL"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "HTTP/1.1"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "SERVER_PROTOCOL: HTTP/1.1"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "REQUEST_SCHEME"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "http"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "REQUEST_SCHEME: http"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: ""
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "GATEWAY_INTERFACE"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "CGI/1.1"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "GATEWAY_INTERFACE: CGI/1.1"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "SERVER_SOFTWARE"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "nginx/"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "1.9.2"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "SERVER_SOFTWARE: nginx/1.9.2"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "REMOTE_ADDR"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "10.2.13.1"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "REMOTE_ADDR: 10.2.13.1"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "REMOTE_PORT"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "52365"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "REMOTE_PORT: 52365"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "SERVER_ADDR"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "10.2.13.167"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "SERVER_ADDR: 10.2.13.167"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "SERVER_PORT"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "80"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "SERVER_PORT: 80"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "SERVER_NAME"
2025/03/22 03:55:55[      ngx_http_script_copy_var_code,   988]  [debug] 2357#2357: *3 http script var: "localhost"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "SERVER_NAME: localhost"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "REDIRECT_STATUS"
2025/03/22 03:55:55[          ngx_http_script_copy_code,   864]  [debug] 2357#2357: *3 http script copy: "200"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1353]  [debug] 2357#2357: *3 fastcgi param: "REDIRECT_STATUS: 200"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1426]  [debug] 2357#2357: *3 fastcgi param: "HTTP_ACCEPT: application/x-ms-application, image/jpeg, application/xaml+xml, image/gif, image/pjpeg, application/x-ms-xbap, application/vnd.ms-excel, application/vnd.ms-powerpoint, application/msword, * / *"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1426]  [debug] 2357#2357: *3 fastcgi param: "HTTP_ACCEPT_LANGUAGE: zh-CN"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1426]  [debug] 2357#2357: *3 fastcgi param: "HTTP_USER_AGENT: Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.1; WOW64; Trident/4.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.3)"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1426]  [debug] 2357#2357: *3 fastcgi param: "HTTP_ACCEPT_ENCODING: gzip, deflate"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1426]  [debug] 2357#2357: *3 fastcgi param: "HTTP_HOST: 10.2.13.167"
2025/03/22 03:55:55[    ngx_http_fastcgi_create_request,  1426]  [debug] 2357#2357: *3 fastcgi param: "HTTP_CONNECTION: Keep-Alive"
2025/03/22 03:55:55[               ngx_http_cleanup_add,  3986]  [debug] 2357#2357: *3 http cleanup add: 080EE06C
2025/03/22 03:55:55[ngx_http_upstream_get_round_robin_peer,   429]  [debug] 2357#2357: *3 get rr peer, try: 1
2025/03/22 03:55:55[             ngx_event_connect_peer,    32]  [debug] 2357#2357: *3 socket 11
2025/03/22 03:55:55[           ngx_epoll_add_connection,  1483]  [debug] 2357#2357: *3 epoll add connection: fd:11 ev:80002005
2025/03/22 03:55:55[             ngx_event_connect_peer,   125]  [debug] 2357#2357: *3 connect to 127.0.0.1:3666, fd:11 #4
2025/03/22 03:55:55[          ngx_http_upstream_connect,  1520]  [debug] 2357#2357: *3 http upstream connect: -2
2025/03/22 03:55:55[                ngx_event_add_timer,    88]  [debug] 2357#2357: *3 <ngx_http_upstream_connect,  1624>  event timer add: 11: 60000:3125260832
���淢���˺ܶ�valueΪ�յı���������if_no_emputy���Ա��ⷢ�Ϳձ���
*/ 
//����FCGI�ĸ�������ʼ������ͷ����HTTP BODY���ݲ��ֵĿ��������������ȡ���������Ϳ��Է���������
//�����u->request_bufs���ӱ����档
static ngx_int_t //ngx_http_fastcgi_create_request��ngx_http_fastcgi_init_params����Ķ�
ngx_http_fastcgi_create_request(ngx_http_request_t *r) //ngx_http_upstream_init_request��ִ�иú���
{
    off_t                         file_pos;
    u_char                        ch, *pos, *lowcase_key;
    size_t                        size, len, key_len, val_len, padding,
                                  allocated;
    ngx_uint_t                    i, n, next, hash, skip_empty, header_params;
    ngx_buf_t                    *b;
    ngx_chain_t                  *cl, *body;
    ngx_list_part_t              *part;
    ngx_table_elt_t              *header, **ignored;
    ngx_http_upstream_t          *u;
    ngx_http_script_code_pt       code;
    ngx_http_script_engine_t      e, le;
    ngx_http_fastcgi_header_t    *h;
    ngx_http_fastcgi_params_t    *params; //
    ngx_http_fastcgi_loc_conf_t  *flcf;
    ngx_http_script_len_code_pt   lcode;

    len = 0;
    header_params = 0;
    ignored = NULL;

    u = r->upstream;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

#if (NGX_HTTP_CACHE)
    params = u->cacheable ? &flcf->params_cache : &flcf->params;
#else
    params = &flcf->params; //fastcgi_params���õı���
#endif

    //��ngx_http_fastcgi_init_params����Ķ� //ngx_http_fastcgi_create_request��ngx_http_fastcgi_init_params����Ķ�
    if (params->lengths) { //��ȡfastcgi_params���õ����б������ȣ�Ҳ�������е�fastcgi_params key value���е�key�ַ������ȣ�����ж�����ã����Ƕ��key֮��
        ngx_memzero(&le, sizeof(ngx_http_script_engine_t));

        ngx_http_script_flush_no_cacheable_variables(r, params->flushes);
        le.flushed = 1;

        le.ip = params->lengths->elts;
        le.request = r;

        while (*(uintptr_t *) le.ip) { //�������е�fastcgi_param���õı�����key��value�ַ���֮��

            ////fastcgi_params���õı���
            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            key_len = lcode(&le);

            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            skip_empty = lcode(&le);


            //Ҳ����ȡ��fastcgi_param  SCRIPT_FILENAME  xxx;���ַ���xxx���ַ�������
            for (val_len = 0; *(uintptr_t *) le.ip; val_len += lcode(&le)) {
                lcode = *(ngx_http_script_len_code_pt *) le.ip;
                //Ϊʲô���������һ��������ֵ����˳�for��?��Ϊ��ngx_http_fastcgi_init_params����value��Ӧ��code���������һ��NULL��ָ�룬Ҳ���������le.ip += sizeof(uintptr_t);
            }
            le.ip += sizeof(uintptr_t);

            //��ngx_http_fastcgi_init_params  ngx_http_upstream_param_set_slot����Ķ�
            if (skip_empty && val_len == 0) { //���fastcgi_param  SCRIPT_FILENAME  xxx  if_not_empty; ���xxx�������ǿյģ���ֱ�������ñ���
                continue;
            }

            //fastcgi_param���õı�����key��value�ַ���֮��
            len += 1 + key_len + ((val_len > 127) ? 4 : 1) + val_len; //((val_len > 127) ? 4 : 1)��ʾ�洢val_len�ֽ�value�ַ���Ҫ���ٸ��ֽ�����ʾ���ַ�����
        }
    }

    if (flcf->upstream.pass_request_headers) { //���� request header �ĳ���

        allocated = 0;
        lowcase_key = NULL;

        if (params->number) { 
            n = 0;
            part = &r->headers_in.headers.part;

            while (part) { //�ͻ�������ͷ��������+fastcgi_param HTTP_XX��������
                n += part->nelts;
                part = part->next;
            }

            ignored = ngx_palloc(r->pool, n * sizeof(void *)); //����һ�� ignored ����
            if (ignored == NULL) {
                return NGX_ERROR;
            }
        }

        part = &r->headers_in.headers.part; //ȡ�� headers �ĵ�һ�� part������Ϣ
        header = part->elts; //ȡ�� headers �ĵ�һ�� part������Ԫ��

        for (i = 0; /* void */; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }

                part = part->next; //��һ������ �����ǰ part ������ϣ����� next part����
                header = part->elts;//��һ�������ͷ��Ԫ��λ��
                i = 0;
            }

            if (params->number) { //���������fastcgi_param  HTTP_  XXX
                if (allocated < header[i].key.len) {
                    allocated = header[i].key.len + 16; //ע�������"host"���ȶ�16
                    lowcase_key = ngx_pnalloc(r->pool, allocated); 
                    //Ϊ��������͹�����ÿһ������ͷ��key����ռ䣬����Ϊhost:www.sina.com�е�"host"�ַ�������ռ�
                    if (lowcase_key == NULL) {
                        return NGX_ERROR;
                    }
                }

                hash = 0;

                
                /* �� key ת��Сд������������һ�� ch �� hash ֵ */
                for (n = 0; n < header[i].key.len; n++) {
                    ch = header[i].key.data[n];

                    if (ch >= 'A' && ch <= 'Z') {
                        ch |= 0x20;

                    } else if (ch == '-') {
                        ch = '_';
                    }

                    hash = ngx_hash(hash, ch);
                    lowcase_key[n] = ch; //����ͷ�����е�keyת��ΪСд����lowcase_key����
                }

                /*
                    ������� header �Ƿ��� ignore ����֮��
                    // yes ������� header����ָ����� ignore �����ڣ��������ã�Ȼ�����������һ��
                    */ //�ͻ�������ͷ���йؼ���key�Ƿ���fastcgi_param  HTTP_xx  XXX���洢HTTP_xx��hash��params->hash���Ƿ��ܲ��ҵ���ͷ����keyһ����
                if (ngx_hash_find(&params->hash, hash, lowcase_key, n)) { 
                    ignored[header_params++] = &header[i]; 
                    //����ͷ�е�key��fastcgi_param HTTP_XX �Ѿ�����HTTP_XX,��Ѹ���������Ϣ��ӵ�ignored������
                    continue;
                }
                
               // n ��ֵ�ļ�����������ʵһ��
               // ���� sizeof ���ټ�һ������Ϊֻ��Ҫ���Ӹ� "HTTP" �� Header ��ȥ������Ҫ "_"
                n += sizeof("HTTP_") - 1;

            } else {
                n = sizeof("HTTP_") - 1 + header[i].key.len; //�������ͷkey����HTTP_�����һ��"HTTP_ͷ����"
            }

            //���� FASTCGI ���ĳ���+����ͷ��key+value���Ⱥ���
            len += ((n > 127) ? 4 : 1) + ((header[i].value.len > 127) ? 4 : 1)
                + n + header[i].value.len;
        }
    }

    //�������Ѿ�������fastcgi_param���õı���key+value������ͷkey+value(HTTP_xx)������Щ�ַ����ĳ��Ⱥ���(�ܳ���len)

    if (len > 65535) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "fastcgi request record is too big: %uz", len);
        return NGX_ERROR;
    }

    //FASTCGI Э��涨�����ݱ��� 8 bit ����
    padding = 8 - len % 8;
    padding = (padding == 8) ? 0 : padding;

    //�����ܵ�����ռ��С
    size = sizeof(ngx_http_fastcgi_header_t) //#1
           + sizeof(ngx_http_fastcgi_begin_request_t) //#2

           + sizeof(ngx_http_fastcgi_header_t) //#3 /* NGX_HTTP_FASTCGI_PARAMS */ //ǰ��������ʵ��������ngx_http_fastcgi_request_start
           + len + padding  //#4
           + sizeof(ngx_http_fastcgi_header_t) //#5 /* NGX_HTTP_FASTCGI_PARAMS */

           + sizeof(ngx_http_fastcgi_header_t);  //#6 /* NGX_HTTP_FASTCGI_STDIN */


    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        return NGX_ERROR;
    }

    cl = ngx_alloc_chain_link(r->pool);// ���� buffer chain���Ѹմ����� buffer ����ȥ
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;

    ngx_http_fastcgi_request_start.br.flags =
        flcf->keep_conn ? NGX_HTTP_FASTCGI_KEEP_CONN : 0;
    //    ǰ���� header ���������Ѿ�����õģ�����򵥸��ƹ���
    ngx_memcpy(b->pos, &ngx_http_fastcgi_request_start,
               sizeof(ngx_http_fastcgi_request_start_t));//ֱ�ӿ���Ĭ�ϵ�FCGIͷ���ֽڣ��Լ��������ֵ�ͷ��
    
    //h ������׼��ngx_http_fastcgi_request_start����ͷ��������������ʼͷ����Ҳ����NGX_HTTP_FASTCGI_PARAMS����
    h = (ngx_http_fastcgi_header_t *)
             (b->pos + sizeof(ngx_http_fastcgi_header_t)
                     + sizeof(ngx_http_fastcgi_begin_request_t)); //���������#3λ��ͷ

    
    //���ݲ������ݣ� ���ʣ��params����ͷ����ʣ��4�ֽ�  ��ngx_http_fastcgi_request_start����Ķ�
    h->content_length_hi = (u_char) ((len >> 8) & 0xff);
    h->content_length_lo = (u_char) (len & 0xff);
    h->padding_length = (u_char) padding;
    h->reserved = 0;

    //��ngx_http_fastcgi_request_start����Ķ�  //����b->lastָ��������ֵĿ�ͷ��������һ������ͷ������Ϊ�������Ѿ����ã����ϡ�
    b->last = b->pos + sizeof(ngx_http_fastcgi_header_t)
                     + sizeof(ngx_http_fastcgi_begin_request_t)
                     + sizeof(ngx_http_fastcgi_header_t); //����#4λ��

    /* ����Ϳ�ʼ���params���� + �ͻ���"HTTP_xx" �������ַ����� */
    
    if (params->lengths) {//����FCGI�Ĳ�����������صĿ���������  
        ngx_memzero(&e, sizeof(ngx_http_script_engine_t));

        e.ip = params->values->elts; //��������ǽ���key-value��Ӧ��key�ַ���ֵ��value�ַ���ֵ
        e.pos = b->last;//FCGI�Ĳ����Ƚ���b����׷��
        e.request = r;
        e.flushed = 1;

        le.ip = params->lengths->elts;
        //��ngx_http_fastcgi_init_params����Ķ� //ngx_http_fastcgi_create_request��ngx_http_fastcgi_init_params����Ķ�
        while (*(uintptr_t *) le.ip) {//��ȡ��Ӧ�ı��������ַ���
            //Ϊngx_http_script_copy_len_code���õ��ű����ȡ� Ҳ����fastcgi_param  SCRIPT_FILENAME  xxx;���ַ���SCRIPT_FILENAME�ַ���
            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            key_len = (u_char) lcode(&le);

            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            skip_empty = lcode(&le);//fastcgi_param  SCRIPT_FILENAME  xxx  if_not_empty;�Ƿ���if_not_empty����������и�ֵΪ1

            for (val_len = 0; *(uintptr_t *) le.ip; val_len += lcode(&le)) { //Ҳ����ȡ��fastcgi_param  SCRIPT_FILENAME  xxx;���ַ���xxx���ַ���
                lcode = *(ngx_http_script_len_code_pt *) le.ip; 
                //Ϊʲô���������һ��������ֵ����˳�for��?��Ϊ��ngx_http_fastcgi_init_params����value��Ӧ��code���������һ��NULL��ָ�룬Ҳ���������le.ip += sizeof(uintptr_t);
            }
            le.ip += sizeof(uintptr_t);

            if (skip_empty && val_len == 0) { //�����������if_not_emputy����������õ�key value�Ͳ��ᷢ�͸����
                e.skip = 1; //ngx_http_script_copy_code��û�����ݣ��ڸú��������追������

                while (*(uintptr_t *) e.ip) {
                    code = *(ngx_http_script_code_pt *) e.ip;
                    code((ngx_http_script_engine_t *) &e);
                }
                e.ip += sizeof(uintptr_t);

                e.skip = 0;

                continue;
            }

            *e.pos++ = (u_char) key_len; //KEY���ȵ�b��

            //VALUE�ַ������ȵ�b�У������4�ֽڱ�ʾ�ĳ��ȣ���һλΪ1������Ϊ0�����ݸ�λ������4�ֽڻ���1�ֽڱ������ݳ���
            if (val_len > 127) {
                *e.pos++ = (u_char) (((val_len >> 24) & 0x7f) | 0x80);
                *e.pos++ = (u_char) ((val_len >> 16) & 0xff);
                *e.pos++ = (u_char) ((val_len >> 8) & 0xff);
                *e.pos++ = (u_char) (val_len & 0xff);

            } else {
                *e.pos++ = (u_char) val_len;
            }

            //�������ngx_http_script_copy_code�´���ű�����������Ķ�Ӧ��value�����е�ֵ��b��
            while (*(uintptr_t *) e.ip) { //ÿ������fastcgi_param  SCRIPT_FILENAME  xxx��value code���涼��һ��NULLָ�룬��������ÿһ��value��Ӧ��code�������˳�
                code = *(ngx_http_script_code_pt *) e.ip;
                code((ngx_http_script_engine_t *) &e);
            }
            e.ip += sizeof(uintptr_t); 

            ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "fastcgi param: \"%*s: %*s\"",
                           key_len, e.pos - (key_len + val_len),
                           val_len, e.pos - val_len);
        }

        b->last = e.pos;
    }
    //���������е�fastcgi_param  xxx   xxx;����

    //��ӿͻ����������е�HTTP_XX�ַ�����Ϣ�������#4��
    if (flcf->upstream.pass_request_headers) {

        part = &r->headers_in.headers.part;
        header = part->elts;

        for (i = 0; /* void */; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }

                part = part->next;
                header = part->elts;
                i = 0;
            }

            for (n = 0; n < header_params; n++) {
                if (&header[i] == ignored[n]) { //������������е�key��fastcgi_param���õĲ���key��ȫһ������Ϊ�����Ѿ�������b���ˣ��������ﲻ��Ҫ�ٿ���
                    goto next;
                }
            }

            key_len = sizeof("HTTP_") - 1 + header[i].key.len; //��ΪҪ����һ��HTTP������ͷkey�У�����host:xxx;���仯HTTPhost���͵���˷�����
            if (key_len > 127) {
                *b->last++ = (u_char) (((key_len >> 24) & 0x7f) | 0x80);
                *b->last++ = (u_char) ((key_len >> 16) & 0xff);
                *b->last++ = (u_char) ((key_len >> 8) & 0xff);
                *b->last++ = (u_char) (key_len & 0xff);

            } else {
                *b->last++ = (u_char) key_len;
            }

            val_len = header[i].value.len;
            if (val_len > 127) {
                *b->last++ = (u_char) (((val_len >> 24) & 0x7f) | 0x80);
                *b->last++ = (u_char) ((val_len >> 16) & 0xff);
                *b->last++ = (u_char) ((val_len >> 8) & 0xff);
                *b->last++ = (u_char) (val_len & 0xff);

            } else {
                *b->last++ = (u_char) val_len;
            }

            b->last = ngx_cpymem(b->last, "HTTP_", sizeof("HTTP_") - 1); //������ͷǰ��Ӹ�HTTP�ַ���

            for (n = 0; n < header[i].key.len; n++) {//��ͷ����keyת�� ��д��Ȼ���Ƶ�b buffer �У�����host:www.sina.com��key��ΪHTTPHOST
                ch = header[i].key.data[n];

                if (ch >= 'a' && ch <= 'z') {
                    ch &= ~0x20;

                } else if (ch == '-') {
                    ch = '_';
                }

                *b->last++ = ch;
            }

            b->last = ngx_copy(b->last, header[i].value.data, val_len); //����host:www.sina.com�е��ַ���www.sina.com��b��

            ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "fastcgi param: \"%*s: %*s\"",
                           key_len, b->last - (key_len + val_len),
                           val_len, b->last - val_len);
        next:

            continue;
        }
    }


    /* �������fastcgi_param�����Ϳͻ�������ͷkey����һ��cl���ͻ��˰�������ռ��һ�����߶��cl������ͨ��next������һ������ǰ�����ӵ�u->request_bufs
        ������Ҫ������˵����ݾ���u->request_bufs���ˣ����͵�ʱ�������ȡ��������*/

    if (padding) { //���ʹ��8�ֽڶ���
        ngx_memzero(b->last, padding);
        b->last += padding;
    }

    //������һ��NGX_HTTP_FASTCGI_PARAMS���������ݳ���Ϊ0��ͷ���У���ʾ���ݽ���
    h = (ngx_http_fastcgi_header_t *) b->last;
    b->last += sizeof(ngx_http_fastcgi_header_t);

    h->version = 1;
    h->type = NGX_HTTP_FASTCGI_PARAMS;
    h->request_id_hi = 0;
    h->request_id_lo = 1;
    h->content_length_hi = 0;//���Ϊ�������ֵ�ͷ�������������Ϊ�գ���ʾ�ǽ�β��
    h->content_length_lo = 0;
    h->padding_length = 0;
    h->reserved = 0;

    /* ������ͻ���ͷ�����Ѿ�������ϣ����Դ�������� */

    if (r->request_body_no_buffering) { //û�л�����壬��ֱ�Ӱ�ͷ���а���fastcgiЭ���ʽ���͵����

        u->request_bufs = cl;

        u->output.output_filter = ngx_http_fastcgi_body_output_filter;
        u->output.filter_ctx = r;

    } else if (flcf->upstream.pass_request_body) {
        //�ͻ������������ʳ��bodyָ�� ngx_http_upstream_init_request��ȡ���Ŀͻ��˰���ṹ
        body = u->request_bufs; //��������������еģ���ngx_http_upstream_init_request��ͷ���õġ�����Ϊ�ͻ��˷��͵�HTTP BODY
        u->request_bufs = cl; //request_bufs����ָ�����渳ֵ�õ�ͷ���к�fastcgi_param�������ݵĿռ�

#if (NGX_SUPPRESS_WARN)
        file_pos = 0;
        pos = NULL;
#endif
        /* �������fastcgi_param�����Ϳͻ�������ͷkey����һ��cl���ͻ��˰�������ռ��һ�����߶��cl������ͨ��next������һ������ǰ�����ӵ�u->request_bufs
                ������Ҫ������˵����ݾ���u->request_bufs���ˣ����͵�ʱ�������ȡ��������*/

        while (body) {

            if (body->buf->in_file) {//������ļ�����
                file_pos = body->buf->file_pos;

            } else {
                pos = body->buf->pos;
            }

            next = 0;

            do {
                b = ngx_alloc_buf(r->pool);//����һ��ngx_buf_sԪ���ݽṹ
                if (b == NULL) {
                    return NGX_ERROR;
                }

                ngx_memcpy(b, body->buf, sizeof(ngx_buf_t));//����Ԫ����

                if (body->buf->in_file) {
                    b->file_pos = file_pos;
                    file_pos += 32 * 1024;//һ��32K�Ĵ�С��

                    if (file_pos >= body->buf->file_last) { //file_pos���ܳ����ļ������ݵ��ܳ���
                        file_pos = body->buf->file_last;
                        next = 1; //˵������һ�ξͿ��Կ����꣬���Ϊ0����ʾ�ļ��л���ı�32K���࣬����Ҫ���ѭ�����ӵ�cl->next��
                    }

                    b->file_last = file_pos;
                    len = (ngx_uint_t) (file_pos - b->file_pos);

                } else {
                    b->pos = pos;
                    b->start = pos;
                    pos += 32 * 1024;

                    if (pos >= body->buf->last) {
                        pos = body->buf->last;
                        next = 1; //
                    }

                    b->last = pos;
                    len = (ngx_uint_t) (pos - b->pos);
                }

                padding = 8 - len % 8;
                padding = (padding == 8) ? 0 : padding;

                h = (ngx_http_fastcgi_header_t *) cl->buf->last;
                cl->buf->last += sizeof(ngx_http_fastcgi_header_t);

                h->version = 1;
                h->type = NGX_HTTP_FASTCGI_STDIN; //����BODY����
                h->request_id_hi = 0;
                h->request_id_lo = 1; //NGINX ��Զֻ����1����
                h->content_length_hi = (u_char) ((len >> 8) & 0xff);//˵��NGINX����BODY��һ��鷢�͵ģ���һ����һ�η��͡�
                h->content_length_lo = (u_char) (len & 0xff);
                h->padding_length = (u_char) padding;
                h->reserved = 0;

                cl->next = ngx_alloc_chain_link(r->pool);//����һ���µ����ӽṹ��������BODY������ɶ�Ĵ���ڵ�һ��BODY������
                if (cl->next == NULL) {
                    return NGX_ERROR;
                }

                cl = cl->next;
                cl->buf = b;//��������µ����ӽṹ������Ϊ�ոյĲ���BODY���ݡ�  ǰ���param����+�ͻ�������������� ����һ��buf���Ǹÿͻ��˰���

                /* �����·�����һ��b�ռ䣬ֻ�洢һ��ͷ����padding�ֶ� */
                b = ngx_create_temp_buf(r->pool,
                                        sizeof(ngx_http_fastcgi_header_t)
                                        + padding);//����һ���µ�ͷ�����壬���ͷ�������ݣ��Լ�����ֽ�
                if (b == NULL) {
                    return NGX_ERROR;
                }

                if (padding) {
                    ngx_memzero(b->last, padding);
                    b->last += padding;
                }

                cl->next = ngx_alloc_chain_link(r->pool);
                if (cl->next == NULL) {
                    return NGX_ERROR;
                }

                cl = cl->next;//�������һ��ͷ���Ļ������������ӱ��ðɣ�������ӱ��㳤���ˡ�
                cl->buf = b;

            } while (!next); //Ϊ0����ʾ�������32K����Ҫ���ѭ���ж�
            //��һ��BODY����
            body = body->next;
        }

    } else {//������÷��������BODY���֡�ֱ��ʹ�øղŵ����ӱ���С����ÿ���BODY��
        u->request_bufs = cl;
    }

    if (!r->request_body_no_buffering) {
        h = (ngx_http_fastcgi_header_t *) cl->buf->last;
        cl->buf->last += sizeof(ngx_http_fastcgi_header_t);

        h->version = 1;
        h->type = NGX_HTTP_FASTCGI_STDIN;//�Ϲ�أ�һ�����ͽ�β��һ��ȫ0��ͷ����
        h->request_id_hi = 0;
        h->request_id_lo = 1;
        h->content_length_hi = 0;
        h->content_length_lo = 0;
        h->padding_length = 0;
        h->reserved = 0;
    }

    cl->next = NULL;//��β�ˡ�

    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_reinit_request(ngx_http_request_t *r)
{
    ngx_http_fastcgi_ctx_t  *f;

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    if (f == NULL) {
        return NGX_OK;
    }

    f->state = ngx_http_fastcgi_st_version;
    f->fastcgi_stdout = 0;
    f->large_stderr = 0;

    if (f->split_parts) {
        f->split_parts->nelts = 0;
    }

    r->state = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_body_output_filter(void *data, ngx_chain_t *in)
{
    ngx_http_request_t  *r = data;

    off_t                       file_pos;
    u_char                     *pos, *start;
    size_t                      len, padding;
    ngx_buf_t                  *b;
    ngx_int_t                   rc;
    ngx_uint_t                  next, last;
    ngx_chain_t                *cl, *tl, *out, **ll;
    ngx_http_fastcgi_ctx_t     *f;
    ngx_http_fastcgi_header_t  *h;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "fastcgi output filter");

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    if (in == NULL) {
        out = in;
        goto out;
    }

    out = NULL;
    ll = &out;

    if (!f->header_sent) {
        /* first buffer contains headers, pass it unmodified */

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "fastcgi output header");

        f->header_sent = 1;

        tl = ngx_alloc_chain_link(r->pool);
        if (tl == NULL) {
            return NGX_ERROR;
        }

        tl->buf = in->buf;
        *ll = tl;
        ll = &tl->next;

        in = in->next;

        if (in == NULL) {
            tl->next = NULL;
            goto out;
        }
    }

    cl = ngx_chain_get_free_buf(r->pool, &f->free);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    b = cl->buf;

    b->tag = (ngx_buf_tag_t) &ngx_http_fastcgi_body_output_filter;
    b->temporary = 1;

    if (b->start == NULL) {
        /* reserve space for maximum possible padding, 7 bytes */

        b->start = ngx_palloc(r->pool,
                              sizeof(ngx_http_fastcgi_header_t) + 7);
        if (b->start == NULL) {
            return NGX_ERROR;
        }

        b->pos = b->start;
        b->last = b->start;

        b->end = b->start + sizeof(ngx_http_fastcgi_header_t) + 7;
    }

    *ll = cl;

    last = 0;
    padding = 0;

#if (NGX_SUPPRESS_WARN)
    file_pos = 0;
    pos = NULL;
#endif

    while (in) {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, r->connection->log, 0,
                       "fastcgi output in  l:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       in->buf->last_buf,
                       in->buf->in_file,
                       in->buf->start, in->buf->pos,
                       in->buf->last - in->buf->pos,
                       in->buf->file_pos,
                       in->buf->file_last - in->buf->file_pos);

        if (in->buf->last_buf) {
            last = 1;
        }

        if (ngx_buf_special(in->buf)) {
            in = in->next;
            continue;
        }

        if (in->buf->in_file) {
            file_pos = in->buf->file_pos;

        } else {
            pos = in->buf->pos;
        }

        next = 0;

        do {
            tl = ngx_chain_get_free_buf(r->pool, &f->free);
            if (tl == NULL) {
                return NGX_ERROR;
            }

            b = tl->buf;
            start = b->start;

            ngx_memcpy(b, in->buf, sizeof(ngx_buf_t));

            /*
             * restore b->start to preserve memory allocated in the buffer,
             * to reuse it later for headers and padding
             */

            b->start = start;

            if (in->buf->in_file) {
                b->file_pos = file_pos;
                file_pos += 32 * 1024;

                if (file_pos >= in->buf->file_last) {
                    file_pos = in->buf->file_last;
                    next = 1;
                }

                b->file_last = file_pos;
                len = (ngx_uint_t) (file_pos - b->file_pos);

            } else {
                b->pos = pos;
                pos += 32 * 1024;

                if (pos >= in->buf->last) {
                    pos = in->buf->last;
                    next = 1;
                }

                b->last = pos;
                len = (ngx_uint_t) (pos - b->pos);
            }

            b->tag = (ngx_buf_tag_t) &ngx_http_fastcgi_body_output_filter;
            b->shadow = in->buf;
            b->last_shadow = next;

            b->last_buf = 0;
            b->last_in_chain = 0;

            padding = 8 - len % 8;
            padding = (padding == 8) ? 0 : padding;

            h = (ngx_http_fastcgi_header_t *) cl->buf->last;
            cl->buf->last += sizeof(ngx_http_fastcgi_header_t);

            h->version = 1;
            h->type = NGX_HTTP_FASTCGI_STDIN;
            h->request_id_hi = 0;
            h->request_id_lo = 1;
            h->content_length_hi = (u_char) ((len >> 8) & 0xff);
            h->content_length_lo = (u_char) (len & 0xff);
            h->padding_length = (u_char) padding;
            h->reserved = 0;

            cl->next = tl;
            cl = tl;

            tl = ngx_chain_get_free_buf(r->pool, &f->free);
            if (tl == NULL) {
                return NGX_ERROR;
            }

            b = tl->buf;

            b->tag = (ngx_buf_tag_t) &ngx_http_fastcgi_body_output_filter;
            b->temporary = 1;

            if (b->start == NULL) {
                /* reserve space for maximum possible padding, 7 bytes */

                b->start = ngx_palloc(r->pool,
                                      sizeof(ngx_http_fastcgi_header_t) + 7);
                if (b->start == NULL) {
                    return NGX_ERROR;
                }

                b->pos = b->start;
                b->last = b->start;

                b->end = b->start + sizeof(ngx_http_fastcgi_header_t) + 7;
            }

            if (padding) {
                ngx_memzero(b->last, padding);
                b->last += padding;
            }

            cl->next = tl;
            cl = tl;

        } while (!next);

        in = in->next;
    }

    if (last) {
        h = (ngx_http_fastcgi_header_t *) cl->buf->last;
        cl->buf->last += sizeof(ngx_http_fastcgi_header_t);

        h->version = 1;
        h->type = NGX_HTTP_FASTCGI_STDIN;
        h->request_id_hi = 0;
        h->request_id_lo = 1;
        h->content_length_hi = 0;
        h->content_length_lo = 0;
        h->padding_length = 0;
        h->reserved = 0;

        cl->buf->last_buf = 1;

    } else if (padding == 0) {
        /* TODO: do not allocate buffers instead */
        cl->buf->temporary = 0;
        cl->buf->sync = 1;
    }

    cl->next = NULL;

out:

#if (NGX_DEBUG)

    for (cl = out; cl; cl = cl->next) {
        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, r->connection->log, 0,
                       "fastcgi output out l:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->last_buf,
                       cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

#endif

    rc = ngx_chain_writer(&r->upstream->writer, out);

    ngx_chain_update_chains(r->pool, &f->free, &f->busy, &out,
                         (ngx_buf_tag_t) &ngx_http_fastcgi_body_output_filter);

    for (cl = f->free; cl; cl = cl->next) {

        /* mark original buffers as sent */

        if (cl->buf->shadow) {
            if (cl->buf->last_shadow) {
                b = cl->buf->shadow;
                b->pos = b->last;
            }

            cl->buf->shadow = NULL;
        }
    }

    return rc;
}

/*
��˷��͹����İ����ʽ
1. ͷ���а���+���ݰ�������fastcgi��ʽ:8�ֽ�fastcgiͷ����+ ����(ͷ������Ϣ+ ���� + ʵ����Ҫ���͵İ�������) + ����ֶ�  
..... �м���ܺ�˰���Ƚϴ������������NGX_HTTP_FASTCGI_STDOUT����fastcgi��ʶ
2. NGX_HTTP_FASTCGI_END_REQUEST����fastcgi��ʽ:��ֻ��8�ֽ�ͷ��

ע��:�������������п�����һ��recv��ȫ�����꣬Ҳ�п�����Ҫ��ȡ���
�ο�<��������nginx> P270
*/
//�����Ӻ�˷�������ȡ����fastcgiͷ����Ϣ��ȥ��8�ֽ�ͷ��ngx_http_fastcgi_header_t�Լ�ͷ�������ݺ��������ֶκ󣬰�ʵ������ͨ��u->bufferָ��   
//��ngx_http_upstream_process_header��ִ�иú���
static ngx_int_t //��ȡfastcgi������ͷ����ngx_http_fastcgi_process_header ��ȡfastcgi������ngx_http_fastcgi_input_filter
ngx_http_fastcgi_process_header(ngx_http_request_t *r)
{//����FCGI�����󷵻ؼ�¼������Ƿ��ر�׼�����������������HTTPͷ�����ص���ͷ�����ݵĻص������ݲ��ֻ�û�н�����
//ngx_http_upstream_process_header��ÿ�ζ�ȡ���ݺ󣬵������
//��ע���������ִ���꣬��һ��������BODY����Ҳ��ȡ����ˣ������ǰ���HTTP HEADER��ĳ��FCGI����ȡ����ˣ�Ȼ����н�����ʱ��
//ngx_http_parse_header_line����������\r\n\r\n���Ƿ���NGX_HTTP_PARSE_HEADER_DONE��Ȼ�󱾺�����ִ����ɡ�

    u_char                         *p, *msg, *start, *last,
                                   *part_start, *part_end;
    size_t                          size;
    ngx_str_t                      *status_line, *pattern;
    ngx_int_t                       rc, status;
    ngx_buf_t                       buf;
    ngx_uint_t                      i;
    ngx_table_elt_t                *h;
    ngx_http_upstream_t            *u;
    ngx_http_fastcgi_ctx_t         *f;
    ngx_http_upstream_header_t     *hh;
    ngx_http_fastcgi_loc_conf_t    *flcf;
    ngx_http_fastcgi_split_part_t  *part;
    ngx_http_upstream_main_conf_t  *umcf;

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    u = r->upstream;

    for ( ;; ) { //����cache������£�ע����ʱ��buf������ʵ�����Ѿ���ngx_http_upstream_process_header�г�ȥ��Ϊ�������ļ���Ԥ����ͷ���ڴ�

        if (f->state < ngx_http_fastcgi_st_data) {//�ϴε�״̬��û�ж���һ��ͷ��,�Ƚ�����Щͷ�������ǲ��������⡣

            f->pos = u->buffer.pos;
            f->last = u->buffer.last;

            rc = ngx_http_fastcgi_process_record(r, f);

            u->buffer.pos = f->pos;
            u->buffer.last = f->last;

            if (rc == NGX_AGAIN) { //˵��ͷ��8�ֽڻ�û���꣬��Ҫ����recv���������ngx_http_fastcgi_process_record����
                return NGX_AGAIN;
            }

            if (rc == NGX_ERROR) {
                return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }

            if (f->type != NGX_HTTP_FASTCGI_STDOUT
                && f->type != NGX_HTTP_FASTCGI_STDERR)  //˵��������NGX_HTTP_FASTCGI_END_REQUEST
                //��ngx_http_fastcgi_process_record����type���Կ���ֻ��Ϊ NGX_HTTP_FASTCGI_STDOUT NGX_HTTP_FASTCGI_STDERR NGX_HTTP_FASTCGI_END_REQUEST
            {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "upstream sent unexpected FastCGI record: %d",
                              f->type);

                return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }

            if (f->type == NGX_HTTP_FASTCGI_STDOUT && f->length == 0) { //���յ�����Я�����ݵ�fastcgi��ʶ����lengthȸΪ0��
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "upstream prematurely closed FastCGI stdout");

                return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }
        }

        if (f->state == ngx_http_fastcgi_st_padding) { //����������ߵ������ʾͷ����fastcgi��ʶ��û�н����������İ��岿�֣������Ҫ�ٴζ�ȡ��������
        //��������if (f->length == 0) { //ͷ���н�����Ϻ�����û�а������ݣ�������������ֶ�
        //ʵ���������padding��ֻ���ں����STDOUT����fastcgi ͷ���н�����Ϻ�(Ҳ��������һ������)������û�а��壬Ҳ����f=>length=0���Ż�ִ�е���������padding�����������ִ�еĵط�

            if (u->buffer.pos + f->padding < u->buffer.last) {  //˵��buffer�е�����Ҳ����padding����ֱ������padding�ֶ�
                f->state = ngx_http_fastcgi_st_version;
                u->buffer.pos += f->padding; //

                continue;  //˵�����fastcgi��ʶ��Ϣ���滹������fastcgi��ʶ��Ϣ
            }

            if (u->buffer.pos + f->padding == u->buffer.last) {
                f->state = ngx_http_fastcgi_st_version;
                u->buffer.pos = u->buffer.last;

                return NGX_AGAIN;
            }

            //˵��buffer��padding�����ֶλ�û�ж��꣬��Ҫ�ٴ�recv���ܶ�ȡ��padding�ֶ�
            f->padding -= u->buffer.last - u->buffer.pos;
            u->buffer.pos = u->buffer.last;

            return NGX_AGAIN;
        }

        //������ֻ����fastcgi��Ϣ��NGX_HTTP_FASTCGI_STDOUT����NGX_HTTP_FASTCGI_STDERR��ʶ��Ϣ

        //�������ʾ��һ��fastcgi��Ϣ��data���ݲ�����
         /* f->state == ngx_http_fastcgi_st_data */
      
        if (f->type == NGX_HTTP_FASTCGI_STDERR) {

            if (f->length) {
                msg = u->buffer.pos; //msgָ�����ݲ���pos��

                if (u->buffer.pos + f->length <= u->buffer.last) { //�����а���������data����
                    u->buffer.pos += f->length; //ֱ������һ��paddingȥ�����Ѱ�����������ֶ�ȥ��
                    f->length = 0;
                    f->state = ngx_http_fastcgi_st_padding;

                } else {
                    f->length -= u->buffer.last - u->buffer.pos; //����ngx_http_fastcgi_st_data�׶ε����ݲ��ֻ�������ֽڣ�Ҳ������Ҫ�ڶ�ȡrecv�����ֽڲ��ܰ�length����
                    u->buffer.pos = u->buffer.last;
                }

                for (p = u->buffer.pos - 1; msg < p; p--) {//�Ӵ�����Ϣ�ĺ�����ǰ��ɨ��ֱ���ҵ�һ����λ\r,\n . �ո� ���ַ�Ϊֹ��Ҳ���ǹ��˺������Щ�ַ��ɡ�
                //�ڴ���STDERR�����ݲ��ִ�β����ǰ���� \r \n . �ո��ַ���λ�ã�����abc.dd\rkkk����pָ��\r�ַ���λ��
                    if (*p != LF && *p != CR && *p != '.' && *p != ' ') {
                        break;
                    }
                }

                p++; //����abc.dd\rkkk����pָ��\r�ַ���λ�õ���һ��λ��k

                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "FastCGI sent in stderr: \"%*s\"", p - msg, msg);//����abc.dd\rkkk����ӡ���Ϊabc.dd

                flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

                if (flcf->catch_stderr) {
                    pattern = flcf->catch_stderr->elts;

                    for (i = 0; i < flcf->catch_stderr->nelts; i++) {
                        if (ngx_strnstr(msg, (char *) pattern[i].data,
                                        p - msg) //fastcgi_catch_stderr "XXX";�е�xxx��p-msg��Ϣ��ƥ�䣬�򷵻�invalid���ú������غ�Ȼ��������һ����˷�����
                            != NULL)
                        {
                            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
                        }
                    }
                }

                if (u->buffer.pos == u->buffer.last) { //˵��û��padding����ֶΣ��պ����ݲ��ֽ����ƶ���pos=last

                    if (!f->fastcgi_stdout) {//��stderr��ʶ��Ϣ֮ǰû���յ���stdout��ʶ��Ϣ

                        /*
                         * the special handling the large number
                         * of the PHP warnings to not allocate memory
                         */

#if (NGX_HTTP_CACHE)
                        if (r->cache) {
                            u->buffer.pos = u->buffer.start
                                                     + r->cache->header_start;
                        } else {
                            u->buffer.pos = u->buffer.start;
                        }
#else
                        u->buffer.pos = u->buffer.start; 
                        //���������fastcgi err��Ϣ�󣬸պð�recv�����ݽ����꣬Ҳ����last=pos,���buffer���Դ���recv�ˣ�Ȼ��ѭ���ڽ���
#endif
                        u->buffer.last = u->buffer.pos;
                        f->large_stderr = 1; 
                    }

                    return NGX_AGAIN; //Ӧ�û�û�н�����fastcgi�Ľ��������Ϣ
                }

            } else { //˵�����滹��padding��Ϣ
                f->state = ngx_http_fastcgi_st_padding;
            }

            continue;
        }


        /* f->type == NGX_HTTP_FASTCGI_STDOUT */ //ͷ���а���

#if (NGX_HTTP_CACHE)

        if (f->large_stderr && r->cache) {
            u_char                     *start;
            ssize_t                     len;
            ngx_http_fastcgi_header_t  *fh;

            start = u->buffer.start + r->cache->header_start;

            len = u->buffer.pos - start - 2 * sizeof(ngx_http_fastcgi_header_t);

            /*
             * A tail of large stderr output before HTTP header is placed
             * in a cache file without a FastCGI record header.
             * To workaround it we put a dummy FastCGI record header at the
             * start of the stderr output or update r->cache_header_start,
             * if there is no enough place for the record header.
             */

            if (len >= 0) {
                fh = (ngx_http_fastcgi_header_t *) start;
                fh->version = 1;
                fh->type = NGX_HTTP_FASTCGI_STDERR;
                fh->request_id_hi = 0;
                fh->request_id_lo = 1;
                fh->content_length_hi = (u_char) ((len >> 8) & 0xff);
                fh->content_length_lo = (u_char) (len & 0xff);
                fh->padding_length = 0;
                fh->reserved = 0;

            } else {
                r->cache->header_start += u->buffer.pos - start
                                           - sizeof(ngx_http_fastcgi_header_t);
            }

            f->large_stderr = 0;
        }

#endif

        f->fastcgi_stdout = 1; //˵�����յ���fastcgi stdout��ʶ��Ϣ

        start = u->buffer.pos;

        if (u->buffer.pos + f->length < u->buffer.last) {

            /*
             * set u->buffer.last to the end of the FastCGI record data
             * for ngx_http_parse_header_line()
             */

            last = u->buffer.last;
            u->buffer.last = u->buffer.pos + f->length; //lastָ�����ݲ��ֵ�ĩβ������Ϊ�����п����д�padding�ȣ����й��˵�padding

        } else {
            last = NULL;
        }

        for ( ;; ) { //STDOUT NGX_HTTP_FASTCGI_STDOUT
            //NGX_HTTP_FASTCGI_STDOUT��ʵ���ݵ�ͷ����β����������padding
            part_start = u->buffer.pos;
            part_end = u->buffer.last;

            rc = ngx_http_parse_header_line(r, &u->buffer, 1); //����fastcgi��˷�������������������

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http fastcgi parser: %d", rc);

            if (rc == NGX_AGAIN) { //һ��������û�н������
                break;
            }

            if (rc == NGX_OK) {//��������һ�������������ˡ� NGX_HTTP_PARSE_HEADER_DONE��ʾ���������н�����ϣ�ͨ������\r\nȷ������ͷ������ϣ�Ҳ���ǳ���һ������

                /* a header line has been parsed successfully */

                h = ngx_list_push(&u->headers_in.headers);
                if (h == NULL) {
                    return NGX_ERROR;
                }

                //���֮ǰ��һ�ζ�ͷ�����ݷ����ģ���������Ҫ�����һ��Ȼ���ٴν�����
                if (f->split_parts && f->split_parts->nelts) {

                    part = f->split_parts->elts;
                    size = u->buffer.pos - part_start;

                    for (i = 0; i < f->split_parts->nelts; i++) {
                        size += part[i].end - part[i].start;
                    }

                    p = ngx_pnalloc(r->pool, size);
                    if (p == NULL) {
                        return NGX_ERROR;
                    }

                    buf.pos = p;

                    for (i = 0; i < f->split_parts->nelts; i++) {
                        p = ngx_cpymem(p, part[i].start,
                                       part[i].end - part[i].start);
                    }

                    p = ngx_cpymem(p, part_start, u->buffer.pos - part_start);

                    buf.last = p;

                    f->split_parts->nelts = 0;

                    rc = ngx_http_parse_header_line(r, &buf, 1);

                    if (rc != NGX_OK) {
                        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                                      "invalid header after joining "
                                      "FastCGI records");
                        return NGX_ERROR;
                    }

                    h->key.len = r->header_name_end - r->header_name_start;
                    h->key.data = r->header_name_start;
                    h->key.data[h->key.len] = '\0';

                    h->value.len = r->header_end - r->header_start;
                    h->value.data = r->header_start;
                    h->value.data[h->value.len] = '\0';

                    h->lowcase_key = ngx_pnalloc(r->pool, h->key.len);
                    if (h->lowcase_key == NULL) {
                        return NGX_ERROR;
                    }

                } else {
                    //���������е�key:value���浽u->headers_in.headers�е������Ա��
                    h->key.len = r->header_name_end - r->header_name_start;
                    h->value.len = r->header_end - r->header_start;

                    h->key.data = ngx_pnalloc(r->pool,
                                              h->key.len + 1 + h->value.len + 1
                                              + h->key.len);
                    if (h->key.data == NULL) {
                        return NGX_ERROR;
                    }

                    //���濪�ٵĿռ�洢����:key.data + '\0' + value.data + '\0' + lowcase_key.data
                    h->value.data = h->key.data + h->key.len + 1; //value.dataΪkey.data��ĩβ��һ��'\0'�ַ��ĺ���һ���ַ�
                    h->lowcase_key = h->key.data + h->key.len + 1
                                     + h->value.len + 1;

                    ngx_memcpy(h->key.data, r->header_name_start, h->key.len); //����key�ַ�����key.data
                    h->key.data[h->key.len] = '\0';
                    ngx_memcpy(h->value.data, r->header_start, h->value.len); //����value�ַ�����value.data
                    h->value.data[h->value.len] = '\0';
                }

                h->hash = r->header_hash;

                if (h->key.len == r->lowcase_index) { 
                    ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);

                } else {
                    ngx_strlow(h->lowcase_key, h->key.data, h->key.len); //��key.dataת��ΪСд�ַ��浽lowcase_key
                }

                hh = ngx_hash_find(&umcf->headers_in_hash, h->hash,
                                   h->lowcase_key, h->key.len); //ͨ��lowcase_key�ؼ��ֲ���ngx_http_upstream_headers_in�ж�Ӧ�ĳ�Ա

                //�������ж�Ӧ��key���ַ���Ϊ"Status"��Ӧ��valueΪ"ttt"����r->upstream->headers_in.statas.data = "ttt";
                //ͨ�������forѭ���͸�handler���������Ի�ȡ�����а�������ݣ�����r->upstream->headers_in�е���س�Աָ��
                if (hh && hh->handler(r, h, hh->offset) != NGX_OK) { //ִ��ngx_http_upstream_headers_in�еĸ�����Ա��handler����
                    return NGX_ERROR;
                }

                ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "http fastcgi header: \"%V: %V\"",
                               &h->key, &h->value);

                if (u->buffer.pos < u->buffer.last) {
                    continue;
                }

                /* the end of the FastCGI record */

                break;
            }

            if (rc == NGX_HTTP_PARSE_HEADER_DONE) { //���е������н�����ϣ�����ֻ���������body�����ˡ�

                /* a whole header has been parsed successfully */

                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "http fastcgi header done");

                if (u->headers_in.status) {
                    status_line = &u->headers_in.status->value;

                    status = ngx_atoi(status_line->data, 3);

                    if (status == NGX_ERROR) {
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                      "upstream sent invalid status \"%V\"",
                                      status_line);
                        return NGX_HTTP_UPSTREAM_INVALID_HEADER;
                    }

                    u->headers_in.status_n = status;
                    u->headers_in.status_line = *status_line;

                } else if (u->headers_in.location) { //˵�������з���"location"��Ҫ�ض���
                    u->headers_in.status_n = 302;
                    ngx_str_set(&u->headers_in.status_line,
                                "302 Moved Temporarily");

                } else {
                    u->headers_in.status_n = 200; //ֱ�ӷ��سɹ�
                    ngx_str_set(&u->headers_in.status_line, "200 OK");
                }

                if (u->state && u->state->status == 0) {
                    u->state->status = u->headers_in.status_n;
                }

                break;
            }

            /* there was error while a header line parsing */

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "upstream sent invalid header");

            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }

        if (last) {
            u->buffer.last = last;
        }

        f->length -= u->buffer.pos - start; //�������ͷ���а��峤��ȥ����ʣ�µ�Ӧ�þ��� �������� + padding �����

        if (f->length == 0) { //ͷ���н�����Ϻ�����û�а������ݣ�������������ֶ�
            f->state = ngx_http_fastcgi_st_padding;
        }

        if (rc == NGX_HTTP_PARSE_HEADER_DONE) { //ͷ���н������
            return NGX_OK;//�����ˣ�����ͷ��ȫ����ɡ���fastcgi STDOUT����ͷ���а���ȫ���������
        }

        if (rc == NGX_OK) {
            continue;
        }

        /* rc == NGX_AGAIN */

        //˵��һ��fastcgi�������и�ʽ���廹û�н�����ϣ��ں˻��������Ѿ�û�������ˣ���Ҫ��ʣ����ֽ��ٴζ�ȡ�����½��н��� �����Ҫ��ס�ϴν�����λ�õ�
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "upstream split a header line in FastCGI records");

        if (f->split_parts == NULL) {
            f->split_parts = ngx_array_create(r->pool, 1,
                                        sizeof(ngx_http_fastcgi_split_part_t));
            if (f->split_parts == NULL) {
                return NGX_ERROR;
            }
        }

        part = ngx_array_push(f->split_parts);
        if (part == NULL) {
            return NGX_ERROR;
        }

        part->start = part_start;//��¼��ʼ����ǰ��ͷ���а����posλ��
        part->end = part_end; //��¼��ʼ����ǰ��ͷ���а����lastλ��

        if (u->buffer.pos < u->buffer.last) {
            continue;
        }

        return NGX_AGAIN;
    }
}


static ngx_int_t
ngx_http_fastcgi_input_filter_init(void *data)
{
    ngx_http_request_t           *r = data;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    r->upstream->pipe->length = flcf->keep_conn ?
                                (off_t) sizeof(ngx_http_fastcgi_header_t) : -1;

    return NGX_OK;
}

/*
    buffering��ʽ��������ǰ���ȿ���һ���ռ䣬��ngx_event_pipe_read_upstream->ngx_readv_chain�п���һ��ngx_buf_t(buf1)�ṹָ����������ݣ�
Ȼ���ڶ�ȡ���ݵ�in�����ʱ����ngx_http_fastcgi_input_filter�����´���һ��ngx_buf_t(buf1)������������buf1->shadow=buf2->shadow
buf2->shadow=buf1->shadow��ͬʱ��buf2��ӵ�p->in�С���ͨ��ngx_http_write_filter�������ݵ�ʱ����p->in�е�������ӵ�p->out��Ȼ���ͣ�
���һ��û�з�����ɣ������ڵ����ݻ�����p->out�С�������ͨ��p->output_filter(p->output_ctx, out)���ͺ�buf2�ᱻ��ӵ�p->free�У�
buf1�ᱻ��ӵ�free_raw_bufs�У���ngx_event_pipe_write_to_downstream
*/

//buffering��ʽ��Ϊngx_http_fastcgi_input_filter  ��buffering��ʽΪngx_http_fastcgi_non_buffered_filter    
//��ȡfastcgi������ͷ����ngx_http_fastcgi_process_header ��ȡfastcgi������ngx_http_fastcgi_input_filter
static ngx_int_t //��ngx_event_pipe_read_upstream���øú���
ngx_http_fastcgi_input_filter(ngx_event_pipe_t *p, ngx_buf_t *buf) 
//��Ҫ���ܾ��ǽ���fastcgi��ʽ���壬����������󣬰Ѷ�Ӧ��buf���뵽p->in
{//�����������\r\n\r\n��ͷ����FCGI���󣬺���İ�������������������д���
    u_char                       *m, *msg;
    ngx_int_t                     rc;
    ngx_buf_t                    *b, **prev;
    ngx_chain_t                  *cl;
    ngx_http_request_t           *r;
    ngx_http_fastcgi_ctx_t       *f;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    if (buf->pos == buf->last) {
        return NGX_OK;
    }

    //ngx_http_get_module_ctx�洢���й����еĸ���״̬(�����ȡ������ݣ�������Ҫ��ζ�ȡ)  ngx_http_get_module_loc_conf��ȡ��ģ����local{}�е�������Ϣ
    r = p->input_ctx;
    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module); //��ȡfastcgiģ�����������ݹ����еĸ���״̬��Ϣ����Ϊ����epoll�����ü��ζ��������
    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    b = NULL;
    prev = &buf->shadow;

    f->pos = buf->pos;
    f->last = buf->last;

    for ( ;; ) {
        if (f->state < ngx_http_fastcgi_st_data) {//С��ngx_http_fastcgi_st_data״̬�ıȽϺô������������ɡ������ֻ��data,padding 2��״̬�ˡ�

            rc = ngx_http_fastcgi_process_record(r, f);//����򵥴���һ��FCGI��ͷ��������Ϣ��ֵ��f��type,length,padding��Ա�ϡ�

            if (rc == NGX_AGAIN) {
                break;//û�����ˣ��ȴ���ȡ
            }

            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }

            if (f->type == NGX_HTTP_FASTCGI_STDOUT && f->length == 0) {//���Э��ͷ��ʾ�Ǳ�׼��������ҳ���Ϊ0���Ǿ���˵��û������
                f->state = ngx_http_fastcgi_st_padding; //�ִ���һ����ͷ��ʼ��Ҳ���ǰ汾�š�

                if (!flcf->keep_conn) {
                    p->upstream_done = 1;
                }

                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, p->log, 0,
                               "http fastcgi closed stdout");

                continue;
            }

            if (f->type == NGX_HTTP_FASTCGI_END_REQUEST) {//FCGI�����˹ر����ӵ�����

                if (!flcf->keep_conn) {
                    p->upstream_done = 1;
                    break;
                }

                ngx_log_debug2(NGX_LOG_DEBUG_HTTP, p->log, 0,
                               "http fastcgi sent end request, flcf->keep_conn:%d, p->upstream_done:%d", 
                                flcf->keep_conn, p->upstream_done);
                
                continue;
            }
        }


        if (f->state == ngx_http_fastcgi_st_padding) { //�����Ƕ�ȡpadding�Ľ׶Σ�

            if (f->type == NGX_HTTP_FASTCGI_END_REQUEST) {

                if (f->pos + f->padding < f->last) {//�����õ�ǰ�������������㹻��padding���ȣ��Ǿ�ֱ��������Ȼ���ǵ���һ��״̬�����������
                    p->upstream_done = 1;
                    break;
                }

                if (f->pos + f->padding == f->last) {//�պý������Ǿ��˳�ѭ�������һ�����ݵĽ�����
                    p->upstream_done = 1;
                    r->upstream->keepalive = 1;
                    break;
                }

                f->padding -= f->last - f->pos;

                break;
            }

            if (f->pos + f->padding < f->last) {
                f->state = ngx_http_fastcgi_st_version;
                f->pos += f->padding;

                continue;
            }

            if (f->pos + f->padding == f->last) {
                f->state = ngx_http_fastcgi_st_version;

                break;
            }

            f->padding -= f->last - f->pos;

            break;
        }

        //�������ֻ�ж�ȡ���ݲ����ˡ�

        /* f->state == ngx_http_fastcgi_st_data */

        if (f->type == NGX_HTTP_FASTCGI_STDERR) {//���Ǳ�׼���������nginx����ô�����أ���ӡһ����־�����ˡ�

            if (f->length) {//�������ݳ���

                if (f->pos == f->last) {//����û�����ˣ�����Ҫ�´��ٶ�ȡһ�����ݲ��ܼ�����
                    break;
                }

                msg = f->pos;

                if (f->pos + f->length <= f->last) {//������Ϣ�Ѿ�ȫ����ȡ���ˣ�
                    f->pos += f->length;
                    f->length = 0;
                    f->state = ngx_http_fastcgi_st_padding;//��һ��ȥ����padding

                } else {
                    f->length -= f->last - f->pos;
                    f->pos = f->last;
                }

                for (m = f->pos - 1; msg < m; m--) {//�Ӵ�����Ϣ�ĺ�����ǰ��ɨ��ֱ���ҵ�һ����λ\r,\n . �ո� ���ַ�Ϊֹ��Ҳ���ǹ��˺������Щ�ַ��ɡ�
                    if (*m != LF && *m != CR && *m != '.' && *m != ' ') {
                        break;
                    }
                }

                ngx_log_error(NGX_LOG_ERR, p->log, 0,
                              "FastCGI sent in stderr: \"%*s\"",
                              m + 1 - msg, msg);

            } else {
                f->state = ngx_http_fastcgi_st_padding;
            }

            continue;
        }

        if (f->type == NGX_HTTP_FASTCGI_END_REQUEST) {

            if (f->pos + f->length <= f->last) {
                f->state = ngx_http_fastcgi_st_padding;
                f->pos += f->length;

                continue;
            }

            f->length -= f->last - f->pos;

            break;
        }


        /* f->type == NGX_HTTP_FASTCGI_STDOUT */

        if (f->pos == f->last) {
            break;
        }

        cl = ngx_chain_get_free_buf(p->pool, &p->free);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        b = cl->buf;
        //������µĻ��������ṹ��ָ��buf����ڴ�����ı�׼������ݲ��֣�ע�����ﲢû�п������ݣ�������bָ����f->posҲ����buf��ĳ�����ݵط���
        ngx_memzero(b, sizeof(ngx_buf_t));

        b->pos = f->pos; //��pos��end  b�е�ָ���buf�е�ָ��ָ����ͬ���ڴ�ռ�
        
        b->start = buf->start; //b ��buf����һ��ͻ��˷��͹��������ݡ������shadow�ĵط��� ����Ӱ��?
        b->end = buf->end; //b ��buf����һ��ͻ��˷��͹��������ݡ������shadow�ĵط��� ����Ӱ��?
        b->tag = p->tag;
        b->temporary = 1;
        /* 
        ����Ϊ��Ҫ���յı�־�������ڷ�������ʱ���ῼ�ǻ�������ڴ�ġ�ΪʲôҪ����Ϊ1�أ���buffer�������ں�����ʼ����
        prev = &buf->shadow;�������buf->shadowָ��������·����b�����ṹ����ʵ�����Ƿֿ��ģ�ֻ��2�������ṹָ��ͬһ��buffer 
        */
        b->recycled = 1;

        //ע��:�ں���Ҳ����b->shadow = buf; Ҳ����b��buf��Ӱ��
        *prev = b; //ע������ǰ��������:prev = &buf->shadow; Ҳ����buf->shadow=b
        /* 
          �������ʼ��buf��Ҳ���ǿͻ��˽��յ����ݵ��ǿ�����buf��shadow��Ա���γ�һ����������ÿ��Ԫ�ض���FCGI��һ������data�������ݡ�
          */
        prev = &b->shadow; //����о�û��????û�κ�����      

        //���潫��ǰ�����õ���FCGI����data���ַ���p->in����������(���뵽����ĩβ��)��
        if (p->in) {
            *p->last_in = cl;
        } else {
            p->in = cl;
        }
        p->last_in = &cl->next;//��ס���һ��

        //ͬ��������һ�����ݿ���š���������ע�⣬buf���ܰ����ü���FCGIЭ�����ݿ飬
		//�ǾͿ��ܴ��ڶ��in�����b->num����һ����ͬ��buf->num.
        /* STUB */ b->num = buf->num;

        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "input buf #%d %p", b->num, b->pos);

        if (f->pos + f->length <= f->last) {//��������㹻�������޸�һ��f->pos����f->state�Ӷ�������һ�����ݰ��Ĵ��������Ѿ�������p->in�˵ġ�
            f->state = ngx_http_fastcgi_st_padding;
            f->pos += f->length;
            b->last = f->pos; //�ƶ�last

            continue;//����������ݣ�������һ��
        }

        //�������ʾ��ǰ��ȡ�������ݻ����ˣ�����һ���������ģ��Ǿ�������һ�㣬Ȼ�󷵻أ�
		//�ȴ��´�event_pipe��ʱ���ٴ�read_upstream����ȡһЩ�����ٴ����ˡ�
        f->length -= f->last - f->pos;

        b->last = f->last;//�ƶ�b->last

        break;

    }

    if (flcf->keep_conn) {

        /* set p->length, minimal amount of data we want to see */

        if (f->state < ngx_http_fastcgi_st_data) {
            p->length = 1;

        } else if (f->state == ngx_http_fastcgi_st_padding) {
            p->length = f->padding;

        } else {
            /* ngx_http_fastcgi_st_data */

            p->length = f->length;
        }
    }

    int upstream_done = p->upstream_done;
    if(upstream_done)
        ngx_log_debugall(p->log, 0, "fastcgi input filter upstream_done:%d", upstream_done);

    if (b) { //�ղ��Ѿ������������ݲ��֡�
        b->shadow = buf; //buf��b��Ӱ�ӣ�ǰ��������buf->shadow=b
        b->last_shadow = 1;

        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "input buf %p %z", b->pos, b->last - b->pos); //��ʱ���b->last - b->pos�Ѿ�ȥ����8�ֽ�ͷ��

        return NGX_OK;
    }

    //�ߵ�����һ��������з���buf�ռ䣬����ȴ����buf��û�ж�ȡ��ʵ�ʵ���ҳ�������ݣ������Ҫ�Ѹ�bufָ���ڴ����free_raw_bufs�����У��Ա����´�
    //��ȡ��˰����ʱ��ֱ�Ӵ�����ȡ

    /* there is no data record in the buf, add it to free chain */
    //��buf����free_raw_bufsͷ�����ߵڶ���λ�ã������һ��λ�������ݵĻ��� 
    //
    if (ngx_event_pipe_add_free_buf(p, buf) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

/*
��˷��͹����İ����ʽ
1. ͷ���а���+���ݰ�������fastcgi��ʽ:8�ֽ�fastcgiͷ����+ ����(ͷ������Ϣ+ ���� + ʵ����Ҫ���͵İ�������) + ����ֶ�  
..... �м���ܺ�˰���Ƚϴ������������NGX_HTTP_FASTCGI_STDOUT����fastcgi��ʶ
2. NGX_HTTP_FASTCGI_END_REQUEST����fastcgi��ʽ:��ֻ��8�ֽ�ͷ��

ע��:�������������п�����һ��recv��ȫ�����꣬Ҳ�п�����Ҫ��ȡ���
�ο�<��������nginx> P270
*/
//dataʵ�����ǿͻ��˵�����ngx_http_request_t *r  
//buffering��ʽ��Ϊngx_http_fastcgi_input_filter  ��buffering��ʽΪngx_http_fastcgi_non_buffered_filter    
static ngx_int_t //ngx_http_upstream_send_response��ִ��
ngx_http_fastcgi_non_buffered_filter(void *data, ssize_t bytes) //�Ѻ�˷��صİ�����Ϣ��ӵ�u->out_bufsĩβ
{
    u_char                  *m, *msg;
    ngx_int_t                rc;
    ngx_buf_t               *b, *buf;
    ngx_chain_t             *cl, **ll;
    ngx_http_request_t      *r;
    ngx_http_upstream_t     *u;
    ngx_http_fastcgi_ctx_t  *f;

    r = data;
    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    u = r->upstream;
    buf = &u->buffer;

    //ִ����ʵ�İ��岿��
    buf->pos = buf->last;
    buf->last += bytes;

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next; //u->out_bufsָ��ĩβ��
    }

    f->pos = buf->pos;
    f->last = buf->last;

    for ( ;; ) {
        //�ڽ���������+�������ݵ�ʱ���п���NGX_HTTP_FASTCGI_END_REQUEST����fastcgi��ʽҲ���յ��������Ҫ����
        if (f->state < ngx_http_fastcgi_st_data) {

            rc = ngx_http_fastcgi_process_record(r, f);

            if (rc == NGX_AGAIN) {
                break;
            }

            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }
        
            if (f->type == NGX_HTTP_FASTCGI_STDOUT && f->length == 0) {
                f->state = ngx_http_fastcgi_st_padding;

                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "http fastcgi closed stdout");

                continue;
            }
        }

        if (f->state == ngx_http_fastcgi_st_padding) { //�п��ܴӺ����if (f->pos + f->length <= f->last) �ߵ�����

            if (f->type == NGX_HTTP_FASTCGI_END_REQUEST) {

                if (f->pos + f->padding < f->last) {
                    u->length = 0;
                    break;
                }

                if (f->pos + f->padding == f->last) {
                    u->length = 0;
                    u->keepalive = 1;
                    break;
                }

                f->padding -= f->last - f->pos;

                break;
            }

            if (f->pos + f->padding < f->last) { //˵��padding���滹�������µ�fastcgi��ʶ������Ҫ����
                f->state = ngx_http_fastcgi_st_version;
                f->pos += f->padding;

                continue;
            }

            if (f->pos + f->padding == f->last) {
                f->state = ngx_http_fastcgi_st_version;

                break;
            }

            f->padding -= f->last - f->pos;

            break;
        }


        /* f->state == ngx_http_fastcgi_st_data */

        if (f->type == NGX_HTTP_FASTCGI_STDERR) {

            if (f->length) {

                if (f->pos == f->last) {
                    break;
                }

                msg = f->pos;

                if (f->pos + f->length <= f->last) {
                    f->pos += f->length;
                    f->length = 0;
                    f->state = ngx_http_fastcgi_st_padding;

                } else {
                    f->length -= f->last - f->pos;
                    f->pos = f->last;
                }

                for (m = f->pos - 1; msg < m; m--) { //�Ӵ�����Ϣ�ĺ�����ǰ��ɨ��ֱ���ҵ�һ����λ\r,\n . �ո� ���ַ�Ϊֹ��Ҳ���ǹ��˺������Щ�ַ��ɡ�
                    if (*m != LF && *m != CR && *m != '.' && *m != ' ') {
                        break;
                    }
                }
                //��������ӡ����־��û�����ġ�
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "FastCGI sent in stderr: \"%*s\"",
                              m + 1 - msg, msg);

            } else {
                f->state = ngx_http_fastcgi_st_padding;
            }

            continue;
        }

        if (f->type == NGX_HTTP_FASTCGI_END_REQUEST) {

            if (f->pos + f->length <= f->last) { //˵��data + padding���ݺ��滹���µ�fastcgi��ʽ����
                f->state = ngx_http_fastcgi_st_padding;
                f->pos += f->length;

                continue;
            }

            f->length -= f->last - f->pos;

            break;
        }


        /* f->type == NGX_HTTP_FASTCGI_STDOUT */
        //��������Ǳ�׼���������Ҳ������ҳ���ݡ�

        
        if (f->pos == f->last) {
            break;//����û�����ݣ�����
        }

        cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs); //��free����ngx_buf_t�ṹ��ȡһ��
        if (cl == NULL) {
            return NGX_ERROR;
        }

        //��cl�ڵ���ӵ�u->out_bufs��β��
        *ll = cl;
        ll = &cl->next;

        b = cl->buf; //ͨ�����渳ֵ�Ӷ�ָ��ʵ��u->buffer�еİ��岿��

        b->flush = 1;
        b->memory = 1;

        b->pos = f->pos;
        b->tag = u->output.tag;

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http fastcgi output buf %p", b->pos);

        if (f->pos + f->length <= f->last) { //�Ѱ��岿��ȡ��������bָ��
            f->state = ngx_http_fastcgi_st_padding;
            f->pos += f->length; //f�ƹ�����
            b->last = f->pos; //�����ĩβ

            continue;
        }

        f->length -= f->last - f->pos;
        b->last = f->last;

        break;
    }

    /* provide continuous buffer for subrequests in memory */

    if (r->subrequest_in_memory) {

        cl = u->out_bufs;

        if (cl) {
            buf->pos = cl->buf->pos;
        }

        buf->last = buf->pos;

        for (cl = u->out_bufs; cl; cl = cl->next) {
            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http fastcgi in memory %p-%p %uz",
                           cl->buf->pos, cl->buf->last, ngx_buf_size(cl->buf));

            if (buf->last == cl->buf->pos) {
                buf->last = cl->buf->last;
                continue;
            }

            buf->last = ngx_movemem(buf->last, cl->buf->pos,
                                    cl->buf->last - cl->buf->pos);

            cl->buf->pos = buf->last - (cl->buf->last - cl->buf->pos);
            cl->buf->last = buf->last;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_process_record(ngx_http_request_t *r,
    ngx_http_fastcgi_ctx_t *f)
{
    u_char                     ch, *p;
    ngx_http_fastcgi_state_e   state;

    state = f->state;

    for (p = f->pos; p < f->last; p++) {

        ch = *p;

        //�����ǰ�8�ֽ�fastcgiЭ��ͷ����ӡ����
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http fastcgi record byte: %02Xd", ch);

        switch (state) {

        case ngx_http_fastcgi_st_version:
            if (ch != 1) { //��һ���ֽڱ�����1
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "upstream sent unsupported FastCGI "
                              "protocol version: %d", ch);
                return NGX_ERROR;
            }
            state = ngx_http_fastcgi_st_type;
            break;

        case ngx_http_fastcgi_st_type:
            switch (ch) {
            case NGX_HTTP_FASTCGI_STDOUT:
            case NGX_HTTP_FASTCGI_STDERR:
            case NGX_HTTP_FASTCGI_END_REQUEST:
                 f->type = (ngx_uint_t) ch;
                 break;
            default:
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "upstream sent invalid FastCGI "
                              "record type: %d", ch);
                return NGX_ERROR;

            }
            state = ngx_http_fastcgi_st_request_id_hi;
            break;

        /* we support the single request per connection */

        case ngx_http_fastcgi_st_request_id_hi:
            if (ch != 0) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "upstream sent unexpected FastCGI "
                              "request id high byte: %d", ch);
                return NGX_ERROR;
            }
            state = ngx_http_fastcgi_st_request_id_lo;
            break;

        case ngx_http_fastcgi_st_request_id_lo:
            if (ch != 1) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "upstream sent unexpected FastCGI "
                              "request id low byte: %d", ch);
                return NGX_ERROR;
            }
            state = ngx_http_fastcgi_st_content_length_hi;
            break;

        case ngx_http_fastcgi_st_content_length_hi:
            f->length = ch << 8;
            state = ngx_http_fastcgi_st_content_length_lo;
            break;

        case ngx_http_fastcgi_st_content_length_lo:
            f->length |= (size_t) ch;
            state = ngx_http_fastcgi_st_padding_length;
            break;

        case ngx_http_fastcgi_st_padding_length:
            f->padding = (size_t) ch;
            state = ngx_http_fastcgi_st_reserved;
            break;

        case ngx_http_fastcgi_st_reserved: //8�ֽ�ͷ�����
            state = ngx_http_fastcgi_st_data;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http fastcgi record length: %z", f->length); //fastcgi��ʽ�������ݳ���()

            f->pos = p + 1;
            f->state = state;

            return NGX_OK;

        /* suppress warning */
        case ngx_http_fastcgi_st_data:
        case ngx_http_fastcgi_st_padding:
            break;
        }
    }

    f->state = state;

    return NGX_AGAIN;
}


static void
ngx_http_fastcgi_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http fastcgi request");

    return;
}


static void
ngx_http_fastcgi_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http fastcgi request");

    return;
}


static ngx_int_t
ngx_http_fastcgi_add_variables(ngx_conf_t *cf)
{
   ngx_http_variable_t  *var, *v;

    for (v = ngx_http_fastcgi_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_fastcgi_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_fastcgi_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_fastcgi_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

#if (NGX_HTTP_CACHE)
    if (ngx_array_init(&conf->caches, cf->pool, 4,
                       sizeof(ngx_http_file_cache_t *))
        != NGX_OK)
    {
        return NULL;
    }
#endif

    return conf;
}


static void *
ngx_http_fastcgi_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_fastcgi_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_fastcgi_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.ignore_headers = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.cache_zone = NULL;
     *     conf->upstream.cache_use_stale = 0;
     *     conf->upstream.cache_methods = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.hide_headers_hash = { NULL, 0 };
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     *     conf->upstream.store_lengths = NULL;
     *     conf->upstream.store_values = NULL;
     *
     *     conf->index.len = { 0, NULL };
     */

    conf->upstream.store = NGX_CONF_UNSET;
    conf->upstream.store_access = NGX_CONF_UNSET_UINT;
    conf->upstream.next_upstream_tries = NGX_CONF_UNSET_UINT;
    conf->upstream.buffering = NGX_CONF_UNSET;
    conf->upstream.request_buffering = NGX_CONF_UNSET;
    conf->upstream.ignore_client_abort = NGX_CONF_UNSET;
    conf->upstream.force_ranges = NGX_CONF_UNSET;

    conf->upstream.local = NGX_CONF_UNSET_PTR;

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.next_upstream_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.send_lowat = NGX_CONF_UNSET_SIZE;
    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;
    conf->upstream.limit_rate = NGX_CONF_UNSET_SIZE;

    conf->upstream.busy_buffers_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.max_temp_file_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.temp_file_write_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream.pass_request_headers = NGX_CONF_UNSET;
    conf->upstream.pass_request_body = NGX_CONF_UNSET;

#if (NGX_HTTP_CACHE)
    conf->upstream.cache = NGX_CONF_UNSET;
    conf->upstream.cache_min_uses = NGX_CONF_UNSET_UINT;
    conf->upstream.cache_bypass = NGX_CONF_UNSET_PTR;
    conf->upstream.no_cache = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_valid = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_lock = NGX_CONF_UNSET;
    conf->upstream.cache_lock_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.cache_lock_age = NGX_CONF_UNSET_MSEC;
    conf->upstream.cache_revalidate = NGX_CONF_UNSET;
#endif

    conf->upstream.hide_headers = NGX_CONF_UNSET_PTR;
    conf->upstream.pass_headers = NGX_CONF_UNSET_PTR;

    conf->upstream.intercept_errors = NGX_CONF_UNSET;

    /* "fastcgi_cyclic_temp_file" is disabled */
    conf->upstream.cyclic_temp_file = 0;

    conf->upstream.change_buffering = 1;

    conf->catch_stderr = NGX_CONF_UNSET_PTR;

    conf->keep_conn = NGX_CONF_UNSET;

    ngx_str_set(&conf->upstream.module, "fastcgi");

    return conf;
}


static char *
ngx_http_fastcgi_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_fastcgi_loc_conf_t *prev = parent;
    ngx_http_fastcgi_loc_conf_t *conf = child;

    size_t                        size;
    ngx_int_t                     rc;
    ngx_hash_init_t               hash;
    ngx_http_core_loc_conf_t     *clcf;

#if (NGX_HTTP_CACHE)

    if (conf->upstream.store > 0) {
        conf->upstream.cache = 0;
    }

    if (conf->upstream.cache > 0) {
        conf->upstream.store = 0;
    }

#endif

    if (conf->upstream.store == NGX_CONF_UNSET) {
        ngx_conf_merge_value(conf->upstream.store,
                              prev->upstream.store, 0);

        conf->upstream.store_lengths = prev->upstream.store_lengths;
        conf->upstream.store_values = prev->upstream.store_values;
    }

    ngx_conf_merge_uint_value(conf->upstream.store_access,
                              prev->upstream.store_access, 0600);

    ngx_conf_merge_uint_value(conf->upstream.next_upstream_tries,
                              prev->upstream.next_upstream_tries, 0);

    ngx_conf_merge_value(conf->upstream.buffering,
                              prev->upstream.buffering, 1);

    ngx_conf_merge_value(conf->upstream.request_buffering,
                              prev->upstream.request_buffering, 1);

    ngx_conf_merge_value(conf->upstream.ignore_client_abort,
                              prev->upstream.ignore_client_abort, 0);

    ngx_conf_merge_value(conf->upstream.force_ranges,
                              prev->upstream.force_ranges, 0);

    ngx_conf_merge_ptr_value(conf->upstream.local,
                              prev->upstream.local, NULL);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.next_upstream_timeout,
                              prev->upstream.next_upstream_timeout, 0);

    ngx_conf_merge_size_value(conf->upstream.send_lowat,
                              prev->upstream.send_lowat, 0);
                              
    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_size_value(conf->upstream.limit_rate,
                              prev->upstream.limit_rate, 0);


    ngx_conf_merge_bufs_value(conf->upstream.bufs, prev->upstream.bufs,
                              8, ngx_pagesize);

    if (conf->upstream.bufs.num < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "there must be at least 2 \"fastcgi_buffers\"");
        return NGX_CONF_ERROR;
    }


    size = conf->upstream.buffer_size;
    if (size < conf->upstream.bufs.size) {
        size = conf->upstream.bufs.size;
    }


    ngx_conf_merge_size_value(conf->upstream.busy_buffers_size_conf,
                              prev->upstream.busy_buffers_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.busy_buffers_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.busy_buffers_size = 2 * size;
    } else {
        conf->upstream.busy_buffers_size =
                                         conf->upstream.busy_buffers_size_conf;
    }

    if (conf->upstream.busy_buffers_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_busy_buffers_size\" must be equal to or greater than "
             "the maximum of the value of \"fastcgi_buffer_size\" and "
             "one of the \"fastcgi_buffers\"");

        return NGX_CONF_ERROR;
    }

    if (conf->upstream.busy_buffers_size
        > (conf->upstream.bufs.num - 1) * conf->upstream.bufs.size)
    {
        size_t buf1 = (size_t)((conf->upstream.bufs.num - 1) * conf->upstream.bufs.size);
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_busy_buffers_size:%z\" must be less than "
             "the size of all \"fastcgi_buffers\" :%z minus one buffer", 
             conf->upstream.busy_buffers_size, 
             buf1);

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream.temp_file_write_size_conf,
                              prev->upstream.temp_file_write_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.temp_file_write_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.temp_file_write_size = 2 * size;
    } else {
        conf->upstream.temp_file_write_size =
                                      conf->upstream.temp_file_write_size_conf;
    }

    if (conf->upstream.temp_file_write_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_temp_file_write_size\" must be equal to or greater "
             "than the maximum of the value of \"fastcgi_buffer_size\" and "
             "one of the \"fastcgi_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_size_value(conf->upstream.max_temp_file_size_conf,
                              prev->upstream.max_temp_file_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.max_temp_file_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.max_temp_file_size = 1024 * 1024 * 1024;
    } else {
        conf->upstream.max_temp_file_size =
                                        conf->upstream.max_temp_file_size_conf;
    }

    if (conf->upstream.max_temp_file_size != 0
        && conf->upstream.max_temp_file_size < size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"fastcgi_max_temp_file_size\" must be equal to zero to disable "
             "temporary files usage or must be equal to or greater than "
             "the maximum of the value of \"fastcgi_buffer_size\" and "
             "one of the \"fastcgi_buffers\"");

        return NGX_CONF_ERROR;
    }


    ngx_conf_merge_bitmask_value(conf->upstream.ignore_headers,
                              prev->upstream.ignore_headers,
                              NGX_CONF_BITMASK_SET);


    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (ngx_conf_merge_path_value(cf, &conf->upstream.temp_path,
                              prev->upstream.temp_path,
                              &ngx_http_fastcgi_temp_path)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

#if (NGX_HTTP_CACHE)

    if (conf->upstream.cache == NGX_CONF_UNSET) {
        ngx_conf_merge_value(conf->upstream.cache,
                              prev->upstream.cache, 0);

        conf->upstream.cache_zone = prev->upstream.cache_zone;
        conf->upstream.cache_value = prev->upstream.cache_value;
    }

    if (conf->upstream.cache_zone && conf->upstream.cache_zone->data == NULL) {
        ngx_shm_zone_t  *shm_zone;

        shm_zone = conf->upstream.cache_zone;

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"fastcgi_cache\" zone \"%V\" is unknown",
                           &shm_zone->shm.name);

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_uint_value(conf->upstream.cache_min_uses,
                              prev->upstream.cache_min_uses, 1);

    ngx_conf_merge_bitmask_value(conf->upstream.cache_use_stale,
                              prev->upstream.cache_use_stale,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_OFF));

    if (conf->upstream.cache_use_stale & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.cache_use_stale = NGX_CONF_BITMASK_SET
                                         |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.cache_use_stale & NGX_HTTP_UPSTREAM_FT_ERROR) {
        conf->upstream.cache_use_stale |= NGX_HTTP_UPSTREAM_FT_NOLIVE;
    }

    if (conf->upstream.cache_methods == 0) {
        conf->upstream.cache_methods = prev->upstream.cache_methods;
    }

    conf->upstream.cache_methods |= NGX_HTTP_GET|NGX_HTTP_HEAD;

    ngx_conf_merge_ptr_value(conf->upstream.cache_bypass,
                             prev->upstream.cache_bypass, NULL);

    ngx_conf_merge_ptr_value(conf->upstream.no_cache,
                             prev->upstream.no_cache, NULL);

    ngx_conf_merge_ptr_value(conf->upstream.cache_valid,
                             prev->upstream.cache_valid, NULL);

    if (conf->cache_key.value.data == NULL) {
        conf->cache_key = prev->cache_key;
    }

    if (conf->upstream.cache && conf->cache_key.value.data == NULL) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "no \"fastcgi_cache_key\" for \"fastcgi_cache\"");
    }

    ngx_conf_merge_value(conf->upstream.cache_lock,
                              prev->upstream.cache_lock, 0);

    ngx_conf_merge_msec_value(conf->upstream.cache_lock_timeout,
                              prev->upstream.cache_lock_timeout, 5000);

    ngx_conf_merge_msec_value(conf->upstream.cache_lock_age,
                              prev->upstream.cache_lock_age, 5000);

    ngx_conf_merge_value(conf->upstream.cache_revalidate,
                              prev->upstream.cache_revalidate, 0);

#endif

    ngx_conf_merge_value(conf->upstream.pass_request_headers,
                              prev->upstream.pass_request_headers, 1);
    ngx_conf_merge_value(conf->upstream.pass_request_body,
                              prev->upstream.pass_request_body, 1);

    ngx_conf_merge_value(conf->upstream.intercept_errors,
                              prev->upstream.intercept_errors, 0);

    ngx_conf_merge_ptr_value(conf->catch_stderr, prev->catch_stderr, NULL);

    ngx_conf_merge_value(conf->keep_conn, prev->keep_conn, 0);


    ngx_conf_merge_str_value(conf->index, prev->index, "");

    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "fastcgi_hide_headers_hash";

    if (ngx_http_upstream_hide_headers_hash(cf, &conf->upstream,
             &prev->upstream, ngx_http_fastcgi_hide_headers, &hash)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    if (clcf->noname
        && conf->upstream.upstream == NULL && conf->fastcgi_lengths == NULL)
    {
        conf->upstream.upstream = prev->upstream.upstream;
        conf->fastcgi_lengths = prev->fastcgi_lengths;
        conf->fastcgi_values = prev->fastcgi_values;
    }

    if (clcf->lmt_excpt && clcf->handler == NULL
        && (conf->upstream.upstream || conf->fastcgi_lengths))
    {
        clcf->handler = ngx_http_fastcgi_handler;
    }

#if (NGX_PCRE)
    if (conf->split_regex == NULL) {
        conf->split_regex = prev->split_regex;
        conf->split_name = prev->split_name;
    }
#endif

    if (conf->params_source == NULL) {
        conf->params = prev->params;
#if (NGX_HTTP_CACHE)
        conf->params_cache = prev->params_cache;
#endif
        conf->params_source = prev->params_source;
    }

    rc = ngx_http_fastcgi_init_params(cf, conf, &conf->params, NULL);
    if (rc != NGX_OK) {
        return NGX_CONF_ERROR;
    }

#if (NGX_HTTP_CACHE)

    if (conf->upstream.cache) {
        rc = ngx_http_fastcgi_init_params(cf, conf, &conf->params_cache,
                                          ngx_http_fastcgi_cache_headers);
        if (rc != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

#endif

    return NGX_CONF_OK;
}

//ngx_http_fastcgi_create_request��ngx_http_fastcgi_init_params����Ķ�
static ngx_int_t
ngx_http_fastcgi_init_params(ngx_conf_t *cf, ngx_http_fastcgi_loc_conf_t *conf,
    ngx_http_fastcgi_params_t *params, ngx_keyval_t *default_params)
{
    u_char                       *p;
    size_t                        size;
    uintptr_t                    *code;
    ngx_uint_t                    i, nsrc;
    ngx_array_t                   headers_names, params_merged;
    ngx_keyval_t                 *h;
    ngx_hash_key_t               *hk;
    ngx_hash_init_t               hash;
    ngx_http_upstream_param_t    *src, *s;
    ngx_http_script_compile_t     sc;
    ngx_http_script_copy_code_t  *copy;

    if (params->hash.buckets) {
        return NGX_OK;
    }

    if (conf->params_source == NULL && default_params == NULL) {
        params->hash.buckets = (void *) 1;
        return NGX_OK;
    }

    params->lengths = ngx_array_create(cf->pool, 64, 1);
    if (params->lengths == NULL) {
        return NGX_ERROR;
    }

    params->values = ngx_array_create(cf->pool, 512, 1);
    if (params->values == NULL) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&headers_names, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (conf->params_source) {
        src = conf->params_source->elts;
        nsrc = conf->params_source->nelts;

    } else {
        src = NULL;
        nsrc = 0;
    }

    if (default_params) {
        if (ngx_array_init(&params_merged, cf->temp_pool, 4,
                           sizeof(ngx_http_upstream_param_t))
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        for (i = 0; i < nsrc; i++) {

            s = ngx_array_push(&params_merged);
            if (s == NULL) {
                return NGX_ERROR;
            }

            *s = src[i];
        }

        h = default_params;

        while (h->key.len) {

            src = params_merged.elts;
            nsrc = params_merged.nelts;

            for (i = 0; i < nsrc; i++) {
                if (ngx_strcasecmp(h->key.data, src[i].key.data) == 0) {
                    goto next;
                }
            }

            s = ngx_array_push(&params_merged);
            if (s == NULL) {
                return NGX_ERROR;
            }

            s->key = h->key;
            s->value = h->value;
            s->skip_empty = 1;

        next:

            h++;
        }

        src = params_merged.elts;
        nsrc = params_merged.nelts;
    }

    for (i = 0; i < nsrc; i++) {

        if (src[i].key.len > sizeof("HTTP_") - 1
            && ngx_strncmp(src[i].key.data, "HTTP_", sizeof("HTTP_") - 1) == 0)
        {
            hk = ngx_array_push(&headers_names);
            if (hk == NULL) {
                return NGX_ERROR;
            }

            hk->key.len = src[i].key.len - 5;
            hk->key.data = src[i].key.data + 5; //��ͷ����HTTP_��5���ַ�ȥ����Ȼ�󿽱���key->data
            hk->key_hash = ngx_hash_key_lc(hk->key.data, hk->key.len);
            hk->value = (void *) 1;

            if (src[i].value.len == 0) {
                continue;
            }
        }

        ////fastcgi_param  SCRIPT_FILENAME  aaa�б�����SCRIPT_FILENAME���ַ������ȳ���code
        copy = ngx_array_push_n(params->lengths,
                                sizeof(ngx_http_script_copy_code_t));
        if (copy == NULL) {
            return NGX_ERROR;
        }

        copy->code = (ngx_http_script_code_pt) ngx_http_script_copy_len_code;
        copy->len = src[i].key.len;


        ////fastcgi_param  SCRIPT_FILENAME  aaa  if_not_empty����ʶ��fastcgi_param���õı���SCRIPT_FILENAME�Ƿ��д�if_not_empty������������Ӧ�ĳ���code��
        copy = ngx_array_push_n(params->lengths,
                                sizeof(ngx_http_script_copy_code_t));
        if (copy == NULL) {
            return NGX_ERROR;
        }

        copy->code = (ngx_http_script_code_pt) ngx_http_script_copy_len_code;
        copy->len = src[i].skip_empty; //��1�ֽڱ�ʾ�Ƿ�������ʱ����"if_not_empty"


        //fastcgi_param  SCRIPT_FILENAME  aaa�ַ���SCRIPT_FILENAME(key)��Ӧ��SCRIPT_FILENAME�ַ���code
        size = (sizeof(ngx_http_script_copy_code_t)
                + src[i].key.len + sizeof(uintptr_t) - 1)
               & ~(sizeof(uintptr_t) - 1);

        copy = ngx_array_push_n(params->values, size);
        if (copy == NULL) {
            return NGX_ERROR;
        }

        copy->code = ngx_http_script_copy_code;
        copy->len = src[i].key.len;

        p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
        ngx_memcpy(p, src[i].key.data, src[i].key.len);


        //fastcgi_param  SCRIPT_FILENAME  aaa�����б�����Ӧ��aaaֵ�����ֵ�Ǳ�����ɣ�����/home/www/scripts/php$fastcgi_script_name
        //����Ҫʹ�ýű��������ֱ�������Щô����fastcgi_param  SCRIPT_FILENAME  aaa���ַ���aaa��Ӧ���ַ�������
        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = &src[i].value;
        sc.flushes = &params->flushes;
        sc.lengths = &params->lengths;
        sc.values = &params->values;

        //�������conf->params_source[]�еĸ�����Աsrc��Ӧ��code��Ϣ��ӵ�params->lengths[]  params->values[]��
        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_ERROR;
        }

        code = ngx_array_push_n(params->lengths, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;


        code = ngx_array_push_n(params->values, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;
    }

    code = ngx_array_push_n(params->lengths, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_ERROR;
    }

    *code = (uintptr_t) NULL;

    params->number = headers_names.nelts;

    hash.hash = &params->hash;//fastcgi_param  HTTP_  XXX;������ͨ��fastcgi_param���õ�HTTP_xx����ͨ��hash����浽��hash����
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = 64;
    hash.name = "fastcgi_params_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    return ngx_hash_init(&hash, headers_names.elts, headers_names.nelts);
}


static ngx_int_t
ngx_http_fastcgi_script_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                       *p;
    ngx_http_fastcgi_ctx_t       *f;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    f = ngx_http_fastcgi_split(r, flcf);

    if (f == NULL) {
        return NGX_ERROR;
    }

    if (f->script_name.len == 0
        || f->script_name.data[f->script_name.len - 1] != '/')
    {
        v->len = f->script_name.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = f->script_name.data;

        return NGX_OK;
    }

    v->len = f->script_name.len + flcf->index.len;

    v->data = ngx_pnalloc(r->pool, v->len);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    p = ngx_copy(v->data, f->script_name.data, f->script_name.len);
    ngx_memcpy(p, flcf->index.data, flcf->index.len);

    return NGX_OK;
}


static ngx_int_t
ngx_http_fastcgi_path_info_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_fastcgi_ctx_t       *f;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    flcf = ngx_http_get_module_loc_conf(r, ngx_http_fastcgi_module);

    f = ngx_http_fastcgi_split(r, flcf);

    if (f == NULL) {
        return NGX_ERROR;
    }

    v->len = f->path_info.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = f->path_info.data;

    return NGX_OK;
}


static ngx_http_fastcgi_ctx_t *
ngx_http_fastcgi_split(ngx_http_request_t *r, ngx_http_fastcgi_loc_conf_t *flcf)
{
    ngx_http_fastcgi_ctx_t       *f;
#if (NGX_PCRE)
    ngx_int_t                     n;
    int                           captures[(1 + 2) * 3];

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    if (f == NULL) {
        f = ngx_pcalloc(r->pool, sizeof(ngx_http_fastcgi_ctx_t));
        if (f == NULL) {
            return NULL;
        }

        ngx_http_set_ctx(r, f, ngx_http_fastcgi_module);
    }

    if (f->script_name.len) {
        return f;
    }

    if (flcf->split_regex == NULL) {
        f->script_name = r->uri;
        return f;
    }

    n = ngx_regex_exec(flcf->split_regex, &r->uri, captures, (1 + 2) * 3);

    if (n >= 0) { /* match */
        f->script_name.len = captures[3] - captures[2];
        f->script_name.data = r->uri.data + captures[2];

        f->path_info.len = captures[5] - captures[4];
        f->path_info.data = r->uri.data + captures[4];

        return f;
    }

    if (n == NGX_REGEX_NO_MATCHED) {
        f->script_name = r->uri;
        return f;
    }

    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                  ngx_regex_exec_n " failed: %i on \"%V\" using \"%V\"",
                  n, &r->uri, &flcf->split_name);
    return NULL;

#else

    f = ngx_http_get_module_ctx(r, ngx_http_fastcgi_module);

    if (f == NULL) {
        f = ngx_pcalloc(r->pool, sizeof(ngx_http_fastcgi_ctx_t));
        if (f == NULL) {
            return NULL;
        }

        ngx_http_set_ctx(r, f, ngx_http_fastcgi_module);
    }

    f->script_name = r->uri;

    return f;

#endif
}

/*
���ngx_http_fastcgi_handler����nginx �������õ�ʱ�򣬽�������ngx_string(��fastcgi_pass��),ָ���ʱ������ngx_http_fastcgi_pass��������ָ�����
*/
static char *
ngx_http_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_url_t                   u;
    ngx_str_t                  *value, *url;
    ngx_uint_t                  n;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_script_compile_t   sc;

    if (flcf->upstream.upstream || flcf->fastcgi_lengths) {
        return "is duplicate";
    }
    
    //��ȡ��ǰ��location�������ĸ�location���õ�"fastcgi_pass"ָ��  
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    /* ���þ��������ngx_http_update_location_config��������Ϊcontent_handle�ģ��Ӷ���content phase�б�����  
        
     //����loc��handler�����clcf->handler����ngx_http_update_location_config()���渳��r->content_handler����
     ����NGX_HTTP_CONTENT_PHASE����������handler����ngx_http_fastcgi_handler��  
     */
    clcf->handler = ngx_http_fastcgi_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    value = cf->args->elts;

    url = &value[1];

    n = ngx_http_script_variables_count(url);//��'$'��ͷ�ı����ж���

    if (n) {

        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = url;
        sc.lengths = &flcf->fastcgi_lengths;
        sc.values = &flcf->fastcgi_values;
        sc.variables = n;
        sc.complete_lengths = 1;
        sc.complete_values = 1;

        //�漰�������Ĳ���ͨ���ú����ѳ���code��value code��ӵ�flcf->fastcgi_lengths��flcf->fastcgi_values��
        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.no_resolve = 1;

    //��������server��upstream���뵽upstream����,�� upstream {}����
    flcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0); 
    if (flcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_fastcgi_split_path_info(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_PCRE)
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t            *value;
    ngx_regex_compile_t   rc;
    u_char                errstr[NGX_MAX_CONF_ERRSTR];

    value = cf->args->elts;

    flcf->split_name = value[1];

    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

    rc.pattern = value[1];
    rc.pool = cf->pool;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    if (ngx_regex_compile(&rc) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V", &rc.err);
        return NGX_CONF_ERROR;
    }

    if (rc.captures != 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "pattern \"%V\" must have 2 captures", &value[1]);
        return NGX_CONF_ERROR;
    }

    flcf->split_regex = rc.regex;

    return NGX_CONF_OK;

#else

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "\"%V\" requires PCRE library", &cmd->name);
    return NGX_CONF_ERROR;

#endif
}


static char *
ngx_http_fastcgi_store(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t                  *value;
    ngx_http_script_compile_t   sc;

    if (flcf->upstream.store != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        flcf->upstream.store = 0;
        return NGX_CONF_OK;
    }

#if (NGX_HTTP_CACHE)
    if (flcf->upstream.cache > 0) {
        return "is incompatible with \"fastcgi_cache\"";
    }
#endif

    flcf->upstream.store = 1;

    if (ngx_strcmp(value[1].data, "on") == 0) {
        return NGX_CONF_OK;
    }

    /* include the terminating '\0' into script */
    value[1].len++;

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = &value[1];
    sc.lengths = &flcf->upstream.store_lengths;
    sc.values = &flcf->upstream.store_values;
    sc.variables = ngx_http_script_variables_count(&value[1]);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


#if (NGX_HTTP_CACHE)

static char *
ngx_http_fastcgi_cache(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t                         *value;
    ngx_http_complex_value_t           cv;
    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (flcf->upstream.cache != NGX_CONF_UNSET) { //˵���Ѿ����ù�fastcgi_cache xx��,�����⵽������fastcgi_cache�������ظ�
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
        flcf->upstream.cache = 0;
        return NGX_CONF_OK;
    }

    if (flcf->upstream.store > 0) {
        return "is incompatible with \"fastcgi_store\"";
    }

    flcf->upstream.cache = 1;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &cv;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (cv.lengths != NULL) {

        flcf->upstream.cache_value = ngx_palloc(cf->pool,
                                             sizeof(ngx_http_complex_value_t));
        if (flcf->upstream.cache_value == NULL) {
            return NGX_CONF_ERROR;
        }

        *flcf->upstream.cache_value = cv;

        return NGX_CONF_OK;
    }

    flcf->upstream.cache_zone = ngx_shared_memory_add(cf, &value[1], 0,
                                                      &ngx_http_fastcgi_module);
    if (flcf->upstream.cache_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

//fastcgi_cache_key
static char *
ngx_http_fastcgi_cache_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_fastcgi_loc_conf_t *flcf = conf;

    ngx_str_t                         *value;
    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (flcf->cache_key.value.data) {
        return "is duplicate";
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &flcf->cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

#endif


static char *
ngx_http_fastcgi_lowat_check(ngx_conf_t *cf, void *post, void *data)
{
#if (NGX_FREEBSD)
    ssize_t *np = data;

    if ((u_long) *np >= ngx_freebsd_net_inet_tcp_sendspace) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"fastcgi_send_lowat\" must be less than %d "
                           "(sysctl net.inet.tcp.sendspace)",
                           ngx_freebsd_net_inet_tcp_sendspace);

        return NGX_CONF_ERROR;
    }

#elif !(NGX_HAVE_SO_SNDLOWAT)
    ssize_t *np = data;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"fastcgi_send_lowat\" is not supported, ignored");

    *np = 0;

#endif

    return NGX_CONF_OK;
}
