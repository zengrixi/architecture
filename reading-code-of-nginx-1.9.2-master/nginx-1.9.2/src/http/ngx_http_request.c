
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void ngx_http_wait_request_handler(ngx_event_t *ev);
static void ngx_http_process_request_line(ngx_event_t *rev);
static void ngx_http_process_request_headers(ngx_event_t *rev);
static ssize_t ngx_http_read_request_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_alloc_large_header_buffer(ngx_http_request_t *r,
    ngx_uint_t request_line);

static ngx_int_t ngx_http_process_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_unique_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_multi_header_lines(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_host(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_connection(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_process_user_agent(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);

static ngx_int_t ngx_http_validate_host(ngx_str_t *host, ngx_pool_t *pool,
    ngx_uint_t alloc);
static ngx_int_t ngx_http_set_virtual_server(ngx_http_request_t *r,
    ngx_str_t *host);
static ngx_int_t ngx_http_find_virtual_server(ngx_connection_t *c,
    ngx_http_virtual_names_t *virtual_names, ngx_str_t *host,
    ngx_http_request_t *r, ngx_http_core_srv_conf_t **cscfp);

static void ngx_http_request_handler(ngx_event_t *ev);
static void ngx_http_terminate_request(ngx_http_request_t *r, ngx_int_t rc);
static void ngx_http_terminate_handler(ngx_http_request_t *r);
static void ngx_http_finalize_connection(ngx_http_request_t *r);
static ngx_int_t ngx_http_set_write_handler(ngx_http_request_t *r);
static void ngx_http_writer(ngx_http_request_t *r);
static void ngx_http_request_finalizer(ngx_http_request_t *r);

static void ngx_http_set_keepalive(ngx_http_request_t *r);
static void ngx_http_keepalive_handler(ngx_event_t *ev);
static void ngx_http_set_lingering_close(ngx_http_request_t *r);
static void ngx_http_lingering_close_handler(ngx_event_t *ev);
static ngx_int_t ngx_http_post_action(ngx_http_request_t *r);
static void ngx_http_close_request(ngx_http_request_t *r, ngx_int_t error);
static void ngx_http_log_request(ngx_http_request_t *r);

static u_char *ngx_http_log_error(ngx_log_t *log, u_char *buf, size_t len);
static u_char *ngx_http_log_error_handler(ngx_http_request_t *r,
    ngx_http_request_t *sr, u_char *buf, size_t len);

#if (NGX_HTTP_SSL)
static void ngx_http_ssl_handshake(ngx_event_t *rev);
static void ngx_http_ssl_handshake_handler(ngx_connection_t *c);
#endif


static char *ngx_http_client_errors[] = {

    /* NGX_HTTP_PARSE_INVALID_METHOD */
    "client sent invalid method",

    /* NGX_HTTP_PARSE_INVALID_REQUEST */
    "client sent invalid request",

    /* NGX_HTTP_PARSE_INVALID_09_METHOD */
    "client sent invalid method in HTTP/0.9 request"
};

//把ngx_http_headers_in中的所有成员做hash运算，然后存放到cmcf->headers_in_hash中，见ngx_http_init_headers_in_hash
ngx_http_header_t  ngx_http_headers_in[] = {  
//通过ngx_http_variable_header函数获取ngx_http_core_variables中相关变量的值，这些值的来源就是ngx_http_headers_in中的hander解析的客户端请求头部

    { ngx_string("Host"), offsetof(ngx_http_headers_in_t, host),
                 ngx_http_process_host }, //ngx_http_process_request_headers中执行handler

    { ngx_string("Connection"), offsetof(ngx_http_headers_in_t, connection),
                 ngx_http_process_connection },

    { ngx_string("If-Modified-Since"),
                 offsetof(ngx_http_headers_in_t, if_modified_since),
                 ngx_http_process_unique_header_line },

    { ngx_string("If-Unmodified-Since"),
                 offsetof(ngx_http_headers_in_t, if_unmodified_since),
                 ngx_http_process_unique_header_line },

    { ngx_string("If-Match"),
                 offsetof(ngx_http_headers_in_t, if_match),
                 ngx_http_process_unique_header_line },

    { ngx_string("If-None-Match"),
                 offsetof(ngx_http_headers_in_t, if_none_match),
                 ngx_http_process_unique_header_line },

    { ngx_string("User-Agent"), offsetof(ngx_http_headers_in_t, user_agent),
                 ngx_http_process_user_agent },

    { ngx_string("Referer"), offsetof(ngx_http_headers_in_t, referer),
                 ngx_http_process_header_line },

    { ngx_string("Content-Length"),
                 offsetof(ngx_http_headers_in_t, content_length),
                 ngx_http_process_unique_header_line },

    { ngx_string("Content-Type"),
                 offsetof(ngx_http_headers_in_t, content_type),
                 ngx_http_process_header_line },

/*
实现断点续传功能的下载 

一. 两个必要响应头Accept-Ranges、ETag
        客户端每次提交下载请求时，服务端都要添加这两个响应头，以保证客户端和服务端将此下载识别为可以断点续传的下载：
Accept-Ranges：告知下载客户端这是一个可以恢复续传的下载，存放本次下载的开始字节位置、文件的字节大小；
ETag：保存文件的唯一标识（我在用的文件名+文件最后修改时间，以便续传请求时对文件进行验证）；
参考http://www.cnblogs.com/diyunpeng/archive/2011/12/29/2305702.html
*/
    { ngx_string("Range"), offsetof(ngx_http_headers_in_t, range),
                 ngx_http_process_header_line },

    { ngx_string("If-Range"),
                 offsetof(ngx_http_headers_in_t, if_range),
                 ngx_http_process_unique_header_line },

    { ngx_string("Transfer-Encoding"),
                 offsetof(ngx_http_headers_in_t, transfer_encoding),
                 ngx_http_process_header_line },

    { ngx_string("Expect"),
                 offsetof(ngx_http_headers_in_t, expect),
                 ngx_http_process_unique_header_line },

    { ngx_string("Upgrade"),
                 offsetof(ngx_http_headers_in_t, upgrade),
                 ngx_http_process_header_line },

#if (NGX_HTTP_GZIP)
    { ngx_string("Accept-Encoding"),
                 offsetof(ngx_http_headers_in_t, accept_encoding),
                 ngx_http_process_header_line },

    { ngx_string("Via"), offsetof(ngx_http_headers_in_t, via),
                 ngx_http_process_header_line },
#endif

    { ngx_string("Authorization"),
                 offsetof(ngx_http_headers_in_t, authorization),
                 ngx_http_process_unique_header_line },

    { ngx_string("Keep-Alive"), offsetof(ngx_http_headers_in_t, keep_alive),
                 ngx_http_process_header_line },

#if (NGX_HTTP_X_FORWARDED_FOR)
    { ngx_string("X-Forwarded-For"),
                 offsetof(ngx_http_headers_in_t, x_forwarded_for),
                 ngx_http_process_multi_header_lines },
#endif

#if (NGX_HTTP_REALIP)
    { ngx_string("X-Real-IP"),
                 offsetof(ngx_http_headers_in_t, x_real_ip),
                 ngx_http_process_header_line },
#endif

#if (NGX_HTTP_HEADERS)
    { ngx_string("Accept"), offsetof(ngx_http_headers_in_t, accept),
                 ngx_http_process_header_line },

    { ngx_string("Accept-Language"),
                 offsetof(ngx_http_headers_in_t, accept_language),
                 ngx_http_process_header_line },
#endif

#if (NGX_HTTP_DAV)
    { ngx_string("Depth"), offsetof(ngx_http_headers_in_t, depth),
                 ngx_http_process_header_line },

    { ngx_string("Destination"), offsetof(ngx_http_headers_in_t, destination),
                 ngx_http_process_header_line },

    { ngx_string("Overwrite"), offsetof(ngx_http_headers_in_t, overwrite),
                 ngx_http_process_header_line },

    { ngx_string("Date"), offsetof(ngx_http_headers_in_t, date),
                 ngx_http_process_header_line },
#endif

    { ngx_string("Cookie"), offsetof(ngx_http_headers_in_t, cookies),
                 ngx_http_process_multi_header_lines },

    { ngx_null_string, 0, NULL }
};

//设置ngx_listening_t的handler，这个handler会在监听到客户端连接时被调用，具体就是在ngx_event_accept函数中，ngx_http_init_connection函数顾名思义，就是初始化这个新建的连接
void
ngx_http_init_connection(ngx_connection_t *c) 
//当建立连接后开辟ngx_http_connection_t结构，这里面存储该服务器端ip:port所在server{}上下文配置信息，和server_name信息等，然后让
//ngx_connection_t->data指向该结构，这样就可以通过ngx_connection_t->data获取到服务器端的serv loc 等配置信息以及该server{}中的server_name信息

{
    ngx_uint_t              i;
    ngx_event_t            *rev;
    struct sockaddr_in     *sin;
    ngx_http_port_t        *port;
    ngx_http_in_addr_t     *addr;
    ngx_http_log_ctx_t     *ctx;
    ngx_http_connection_t  *hc;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6    *sin6;
    ngx_http_in6_addr_t    *addr6;
#endif

    //注意ngx_connection_t和ngx_http_connection_t的区别，前者是建立连接accept前使用的结构，后者是连接成功后使用的结构
    hc = ngx_pcalloc(c->pool, sizeof(ngx_http_connection_t));
    if (hc == NULL) {
        ngx_http_close_connection(c);
        return;
    }

    //在服务器端accept客户端连接成功(ngx_event_accept)后，会通过ngx_get_connection从连接池获取一个ngx_connection_t结构，也就是每个客户端连接对于一个ngx_connection_t结构，
    //并且为其分配一个ngx_http_connection_t结构，ngx_connection_t->data = ngx_http_connection_t，见ngx_http_init_connection
    c->data = hc;

    /* find the server configuration for the address:port */

    port = c->listening->servers;  

    if (port->naddrs > 1) {  
    
        /*
         * there are several addresses on this port and one of them
         * is an "*:port" wildcard so getsockname() in ngx_http_server_addr()
         * is required to determine a server address
         */
        //说明listen ip:port存在几条没有bind选项，并且存在通配符配置，如listen *:port,那么就需要通过ngx_connection_local_sockaddr来确定
    //究竟客户端是和那个本地ip地址建立的连接
        if (ngx_connection_local_sockaddr(c, NULL, 0) != NGX_OK) { //
            ngx_http_close_connection(c);
            return;
        }

        switch (c->local_sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            sin6 = (struct sockaddr_in6 *) c->local_sockaddr;

            addr6 = port->addrs;

            /* the last address is "*" */

            for (i = 0; i < port->naddrs - 1; i++) {
                if (ngx_memcmp(&addr6[i].addr6, &sin6->sin6_addr, 16) == 0) {
                    break;
                }
            }

            hc->addr_conf = &addr6[i].conf;

            break;
#endif

        default: /* AF_INET */
            sin = (struct sockaddr_in *) c->local_sockaddr;

            addr = port->addrs; 

            /* the last address is "*" */
            //根据上面的ngx_connection_local_sockaddr函数获取到客户端连接到本地，本地IP地址获取到后，遍历ngx_http_port_t找到对应
            //的IP地址和端口，然后赋值给ngx_http_connection_t->addr_conf，这里面存储有server_name配置信息以及该ip:port对应的上下文信息
            for (i = 0; i < port->naddrs - 1; i++) {
                if (addr[i].addr == sin->sin_addr.s_addr) {
                    break;
                }
            }

          /*
                这里也体现了在ngx_http_init_connection中获取http{}上下文ctx，如果客户端请求中带有host参数，则会继续在ngx_http_set_virtual_server
                中重新获取对应的server{}和location{}，如果客户端请求不带host头部行，则使用默认的server{},见 ngx_http_init_connection  
            */
            hc->addr_conf = &addr[i].conf;

            break;
        }

    } else {

        switch (c->local_sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            addr6 = port->addrs;
            hc->addr_conf = &addr6[0].conf;
            break;
#endif

        default: /* AF_INET */
            addr = port->addrs;
            hc->addr_conf = &addr[0].conf;
            break;
        }
    }

    /* the default server configuration for the address:port */
    //listen add:port对于的 server{}配置块的上下文ctx
    hc->conf_ctx = hc->addr_conf->default_server->ctx;

    ctx = ngx_palloc(c->pool, sizeof(ngx_http_log_ctx_t));
    if (ctx == NULL) {
        ngx_http_close_connection(c);
        return;
    }

    ctx->connection = c;
    ctx->request = NULL;
    ctx->current_request = NULL;

    c->log->connection = c->number;
    c->log->handler = ngx_http_log_error;
    c->log->data = ctx;
    c->log->action = "waiting for request";

    c->log_error = NGX_ERROR_INFO;

    rev = c->read;
    rev->handler = ngx_http_wait_request_handler;
    c->write->handler = ngx_http_empty_handler;

#if (NGX_HTTP_V2) 
    /* 这里放在SSL的前面是，如果没有配置SSL，则直接不用进行SSL协商而进行HTTP2处理ngx_http_v2_init */
    if (hc->addr_conf->http2) {
        rev->handler = ngx_http_v2_init;
    }
#endif

#if (NGX_HTTP_SSL)
    {
    ngx_http_ssl_srv_conf_t  *sscf;

    sscf = ngx_http_get_module_srv_conf(hc->conf_ctx, ngx_http_ssl_module);

    if (sscf->enable || hc->addr_conf->ssl) {

        c->log->action = "SSL handshaking";

        if (hc->addr_conf->ssl && sscf->ssl.ctx == NULL) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                          "no \"ssl_certificate\" is defined "
                          "in server listening on SSL port");
            ngx_http_close_connection(c);
            return;
        }

        hc->ssl = 1;

        rev->handler = ngx_http_ssl_handshake;
    }
    }
#endif

    if (hc->addr_conf->proxy_protocol) {
        hc->proxy_protocol = 1;
        c->log->action = "reading PROXY protocol";
    }

    /*
     如果新连接的读事件ngx_event_t结构体中的标志位ready为1，实际上表示这个连接对应的套接字缓存上已经有用户发来的数据，
     这时就可调用上面说过的ngx_http_init_request方法处理请求。
     */
    //这里只可能是当listen的时候添加了defered参数并且内核支持，在ngx_event_accept的时候才会置1，才可能执行下面的if里面的内容，否则不会只需if里面的内容
    if (rev->ready) {
        /* the deferred accept(), iocp */
        if (ngx_use_accept_mutex) { //如果是配置了accept_mutex，则把该rev->handler延后处理，
        //实际上执行的地方为ngx_process_events_and_timers中的ngx_event_process_posted
            ngx_post_event(rev, &ngx_posted_events);
            return;
        }

        rev->handler(rev); //ngx_http_wait_request_handler
        return;
    }

/*
在有些情况下，当TCP连接建立成功时同时也出现了可读事件（例如，在套接字listen配置时设置了deferred选项时，内核仅在套接字上确实收到请求时才会通知epoll
调度事件的回调方法。当然，在大部分情况下，ngx_http_init_request方法和
ngx_http_init_connection方法都是由两个事件（TCP连接建立成功事件和连接上的可读事件）触发调用的
*/

/*
调用ngx_add_timer方法把读事件添加到定时器中，设置的超时时间则是nginx.conf中client_header_timeout配置项指定的参数。
也就是说，如果经过client_header_timeout时间后这个连接上还没有用户数据到达，则会由定时器触发调用读事件的ngx_http_init_request处理方法。
 */
    ngx_add_timer(rev, c->listening->post_accept_timeout, NGX_FUNC_LINE); //把接收事件添加到定时器中，当post_accept_timeout秒还没有客户端数据到来，就关闭连接
    ngx_reusable_connection(c, 1);
    
    if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) { //当下次有数据从客户端发送过来的时候，会在ngx_epoll_process_events把对应的ready置1。
        ngx_http_close_connection(c);
        return;
    }
}

//客户端建立连接后，只有第一次读取客户端数据到数据的时候，执行的handler指向该函数，因此当客户端连接建立成功后，只有第一次读取
//客户端数据才会走该函数，如果在保活期内又收到客户端请求，则不会再走该函数，而是执行ngx_http_process_request_line，因为该函数
//把handler指向了ngx_http_process_request_line
static void
ngx_http_wait_request_handler(ngx_event_t *rev)
{
    u_char                    *p;
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_connection_t          *c;
    ngx_http_connection_t     *hc;
    ngx_http_core_srv_conf_t  *cscf;

    c = rev->data;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http wait request handler");

    if (rev->timedout) { //如果tcp连接建立后，等了client_header_timeout秒一直没有收到客户端的数据包过来，则关闭连接
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        ngx_http_close_connection(c);
        return;
    }

    if (c->close) {
        ngx_http_close_connection(c);
        return;
    }

    hc = c->data;
    cscf = ngx_http_get_module_srv_conf(hc->conf_ctx, ngx_http_core_module);

    size = cscf->client_header_buffer_size; //默认1024

    b = c->buffer;

    if (b == NULL) {
        b = ngx_create_temp_buf(c->pool, size);
        if (b == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        c->buffer = b;

    } else if (b->start == NULL) {

        b->start = ngx_palloc(c->pool, size);
        if (b->start == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        b->pos = b->start;
        b->last = b->start;
        b->end = b->last + size;
    }

    //这里如果一次没有把所有客户端的数据读取完，则在ngx_http_process_request_line中会继续读取
    //与ngx_http_read_request_header配合读
    n = c->recv(c, b->last, size);  //读取客户端来的数据    执行ngx_unix_recv

    if (n == NGX_AGAIN) {
        if (!rev->timer_set) {
            ngx_add_timer(rev, c->listening->post_accept_timeout, NGX_FUNC_LINE);
            ngx_reusable_connection(c, 1);
        }

        if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) {
            ngx_http_close_connection(c);
            return;
        }

        /*
         * We are trying to not hold c->buffer's memory for an idle connection.
         */

        if (ngx_pfree(c->pool, b->start) == NGX_OK) {
            b->start = NULL;
        }

        return;
    }

    if (n == NGX_ERROR) {
        ngx_http_close_connection(c);
        return;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client closed connection");
        ngx_http_close_connection(c);
        return;
    }

    b->last += n;

    if (hc->proxy_protocol) {
        hc->proxy_protocol = 0;

        p = ngx_proxy_protocol_read(c, b->pos, b->last);

        if (p == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        b->pos = p;

        if (b->pos == b->last) {
            c->log->action = "waiting for request";
            b->pos = b->start;
            b->last = b->start;
            ngx_post_event(rev, &ngx_posted_events);
            return;
        }
    }

    c->log->action = "reading client request line";

    ngx_reusable_connection(c, 0);
    //从新让c->data指向新开辟的ngx_http_request_t
    c->data = ngx_http_create_request(c);
    if (c->data == NULL) {
        ngx_http_close_connection(c);
        return;
    }

    rev->handler = ngx_http_process_request_line;
    ngx_http_process_request_line(rev);
}

//只有在连接建立并接受到客户端第一次请求的时候才会创建ngx_connection_t，该结构一直持续到连接关闭才释放
ngx_http_request_t *
ngx_http_create_request(ngx_connection_t *c)
{
    ngx_pool_t                 *pool;
    ngx_time_t                 *tp;
    ngx_http_request_t         *r;
    ngx_http_log_ctx_t         *ctx;
    ngx_http_connection_t      *hc;
    ngx_http_core_srv_conf_t   *cscf;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_core_main_conf_t  *cmcf;

    c->requests++;

    hc = c->data;
    //在ngx_http_wait_request_handler的时候，data指向ngx_http_connection_t,该函数返回后 c->data重新指向新开盘的ngx_http_request_t
    //之前c->data指向的ngx_http_connection_t用r->http_connection保存

    cscf = ngx_http_get_module_srv_conf(hc->conf_ctx, ngx_http_core_module);

    pool = ngx_create_pool(cscf->request_pool_size, c->log);
    if (pool == NULL) {
        return NULL;
    }

    r = ngx_pcalloc(pool, sizeof(ngx_http_request_t));
    if (r == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    r->pool = pool; 

    //当连接建立成功后，当收到客户端的第一个请求的时候会通过ngx_http_wait_request_handler->ngx_http_create_request创建ngx_http_request_t
    //同时把r->http_connection指向accept客户端连接成功时候创建的ngx_http_connection_t，这里面有存储server{}上下文ctx和server_name等信息
    //该ngx_http_request_t会一直有效，除非关闭连接。因此该函数只会调用一次，也就是第一个客户端请求报文过来的时候创建，一直持续到连接关闭
    r->http_connection = hc; //重新把c->data赋值给r->http_connection，这样r->http_connection就保存了ngx_http_connection_t信息
    r->signature = NGX_HTTP_MODULE;
    r->connection = c;

    r->main_conf = hc->conf_ctx->main_conf;
    r->srv_conf = hc->conf_ctx->srv_conf;
    r->loc_conf = hc->conf_ctx->loc_conf;

    r->read_event_handler = ngx_http_block_reading;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ngx_set_connection_log(r->connection, clcf->error_log);

    r->header_in = hc->nbusy ? hc->busy[0] : c->buffer;

    if (ngx_list_init(&r->headers_out.headers, r->pool, 20,
                      sizeof(ngx_table_elt_t))
        != NGX_OK)
    {
        ngx_destroy_pool(r->pool);
        return NULL;
    }

    r->ctx = ngx_pcalloc(r->pool, sizeof(void *) * ngx_http_max_module);
    if (r->ctx == NULL) {
        ngx_destroy_pool(r->pool);
        return NULL;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    r->variables = ngx_pcalloc(r->pool, cmcf->variables.nelts
                                        * sizeof(ngx_http_variable_value_t));
    if (r->variables == NULL) {
        ngx_destroy_pool(r->pool);
        return NULL;
    }

#if (NGX_HTTP_SSL)
    if (c->ssl) {
        r->main_filter_need_in_memory = 1;
    }
#endif

    r->main = r;
    r->count = 1;

    tp = ngx_timeofday();
    //ngx_http_request_t结构体中有两个成员表示这个请求的开始处理时间：start_sec成员和start_msec成员
    r->start_sec = tp->sec;
    r->start_msec = tp->msec;

    r->method = NGX_HTTP_UNKNOWN;
    r->http_version = NGX_HTTP_VERSION_10;

    r->headers_in.content_length_n = -1;
    r->headers_in.keep_alive_n = -1;
    r->headers_out.content_length_n = -1;
    r->headers_out.last_modified_time = -1;

    r->uri_changes = NGX_HTTP_MAX_URI_CHANGES + 1;
    r->subrequests = NGX_HTTP_MAX_SUBREQUESTS + 1;

    r->http_state = NGX_HTTP_READING_REQUEST_STATE;

    ctx = c->log->data;
    ctx->request = r;
    ctx->current_request = r;
    r->log_handler = ngx_http_log_error_handler;

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_reading, 1);
    r->stat_reading = 1;
    (void) ngx_atomic_fetch_add(ngx_stat_requests, 1);
#endif

    return r;
}


#if (NGX_HTTP_SSL)
/* 如果配置了需要支持ssl协议，则连接建立后会调用该handler处理后续ssl协商过程 */
static void
ngx_http_ssl_handshake(ngx_event_t *rev)
{
    u_char                   *p, buf[NGX_PROXY_PROTOCOL_MAX_HEADER + 1];
    size_t                    size;
    ssize_t                   n;
    ngx_err_t                 err;
    ngx_int_t                 rc;
    ngx_connection_t         *c;
    ngx_http_connection_t    *hc;
    ngx_http_ssl_srv_conf_t  *sscf;

    c = rev->data;
    hc = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0,
                   "http check ssl handshake");

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        ngx_http_close_connection(c);
        return;
    }

    if (c->close) {
        ngx_http_close_connection(c);
        return;
    }

    size = hc->proxy_protocol ? sizeof(buf) : 1; //如果不是做proxy，则读1字节出来，看是什么协议

    /*
    MSG_PEEK标志会将套接字接收队列中的可读的数据拷贝到缓冲区，但不会使套接子接收队列中的数据减少，
    常见的是：例如调用recv或read后，导致套接字接收队列中的数据被读取后而减少，而指定了MSG_PEEK标志，
    可通过返回值获得可读数据长度，并且不会减少套接字接收缓冲区中的数据，所以可以供程序的其他部分继续读取。
    */
    n = recv(c->fd, (char *) buf, size, MSG_PEEK); 

    err = ngx_socket_errno;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http recv(): %d", n);

    if (n == -1) {
        if (err == NGX_EAGAIN) {
            rev->ready = 0;

            if (!rev->timer_set) {
                ngx_add_timer(rev, c->listening->post_accept_timeout, NGX_FUNC_LINE);
                ngx_reusable_connection(c, 1);
            }

            if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) {
                ngx_http_close_connection(c);
            }

            return;
        }

        ngx_connection_error(c, err, "recv() failed");
        ngx_http_close_connection(c);

        return;
    }

    if (hc->proxy_protocol) {
        hc->proxy_protocol = 0;

        p = ngx_proxy_protocol_read(c, buf, buf + n);

        if (p == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        size = p - buf;

        if (c->recv(c, buf, size) != (ssize_t) size) {
            ngx_http_close_connection(c);
            return;
        }

        c->log->action = "SSL handshaking";

        if (n == (ssize_t) size) {
            ngx_post_event(rev, &ngx_posted_events);
            return;
        }

        n = 1;
        buf[0] = *p;
    }

    if (n == 1) {
        if (buf[0] & 0x80 /* SSLv2 */ || buf[0] == 0x16 /* SSLv3/TLSv1 */) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, rev->log, 0,
                           "https ssl handshake: 0x%02Xd", buf[0]);

            sscf = ngx_http_get_module_srv_conf(hc->conf_ctx,
                                                ngx_http_ssl_module);

            if (ngx_ssl_create_connection(&sscf->ssl, c, NGX_SSL_BUFFER)
                != NGX_OK)
            {
                ngx_http_close_connection(c);
                return;
            }

            rc = ngx_ssl_handshake(c);

            if (rc == NGX_AGAIN) {

                if (!rev->timer_set) {
                    ngx_add_timer(rev, c->listening->post_accept_timeout, NGX_FUNC_LINE);
                }

                ngx_reusable_connection(c, 0);

                //ssl单向认证四次握手完成后执行该handler
                c->ssl->handler = ngx_http_ssl_handshake_handler;
                return;
            }

            ngx_http_ssl_handshake_handler(c);

            return;
        }

        //http 平台的请求，如果是http平台的请求，就走一般流程返回错我信息
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "plain http");

        c->log->action = "waiting for request";

        rev->handler = ngx_http_wait_request_handler;
        ngx_http_wait_request_handler(rev);

        return;
    }

    ngx_log_error(NGX_LOG_INFO, c->log, 0, "client closed connection");
    ngx_http_close_connection(c);
}

//SSL或者TLS协商成功后，开始读取客户端包体了
static void
ngx_http_ssl_handshake_handler(ngx_connection_t *c)
{
    if (c->ssl->handshaked) {

        /*
         * The majority of browsers do not send the "close notify" alert.
         * Among them are MSIE, old Mozilla, Netscape 4, Konqueror,
         * and Links.  And what is more, MSIE ignores the server's alert.
         *
         * Opera and recent Mozilla send the alert.
         */

        c->ssl->no_wait_shutdown = 1;

#if (NGX_HTTP_V2                                                              \
     && (defined TLSEXT_TYPE_application_layer_protocol_negotiation           \
         || defined TLSEXT_TYPE_next_proto_neg))
        {
        unsigned int          len;
        const unsigned char  *data;

#ifdef TLSEXT_TYPE_application_layer_protocol_negotiation
        SSL_get0_alpn_selected(c->ssl->connection, &data, &len);

#ifdef TLSEXT_TYPE_next_proto_neg
        if (len == 0) {
            SSL_get0_next_proto_negotiated(c->ssl->connection, &data, &len);
        }
#endif

#else /* TLSEXT_TYPE_next_proto_neg */
        SSL_get0_next_proto_negotiated(c->ssl->connection, &data, &len);
#endif

        if (len == 2 && data[0] == 'h' && data[1] == '2') {
            ngx_http_v2_init(c->read);
            return;
        }
        }
#endif

        c->log->action = "waiting for request";

        c->read->handler = ngx_http_wait_request_handler;
        /* STUB: epoll edge */ c->write->handler = ngx_http_empty_handler;

        ngx_reusable_connection(c, 1);

        ngx_http_wait_request_handler(c->read);

        return;
    }

    if (c->read->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
    }

    ngx_http_close_connection(c);
}

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME

int
ngx_http_ssl_servername(ngx_ssl_conn_t *ssl_conn, int *ad, void *arg)
{
    ngx_str_t                  host;
    const char                *servername;
    ngx_connection_t          *c;
    ngx_http_connection_t     *hc;
    ngx_http_ssl_srv_conf_t   *sscf;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;

    servername = SSL_get_servername(ssl_conn, TLSEXT_NAMETYPE_host_name);

    if (servername == NULL) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    c = ngx_ssl_get_connection(ssl_conn);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "SSL server name: \"%s\"", servername);

    host.len = ngx_strlen(servername);

    if (host.len == 0) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    host.data = (u_char *) servername;

    if (ngx_http_validate_host(&host, c->pool, 1) != NGX_OK) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    hc = c->data;

    if (ngx_http_find_virtual_server(c, hc->addr_conf->virtual_names, &host,
                                     NULL, &cscf)
        != NGX_OK)
    {
        return SSL_TLSEXT_ERR_NOACK;
    }

    hc->ssl_servername = ngx_palloc(c->pool, sizeof(ngx_str_t));
    if (hc->ssl_servername == NULL) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    *hc->ssl_servername = host;

    hc->conf_ctx = cscf->ctx;

    clcf = ngx_http_get_module_loc_conf(hc->conf_ctx, ngx_http_core_module);

    ngx_set_connection_log(c, clcf->error_log);

    sscf = ngx_http_get_module_srv_conf(hc->conf_ctx, ngx_http_ssl_module);

    if (sscf->ssl.ctx) {
        SSL_set_SSL_CTX(ssl_conn, sscf->ssl.ctx);

        /*
         * SSL_set_SSL_CTX() only changes certs as of 1.0.0d
         * adjust other things we care about
         */

        SSL_set_verify(ssl_conn, SSL_CTX_get_verify_mode(sscf->ssl.ctx),
                       SSL_CTX_get_verify_callback(sscf->ssl.ctx));

        SSL_set_verify_depth(ssl_conn, SSL_CTX_get_verify_depth(sscf->ssl.ctx));

#ifdef SSL_CTRL_CLEAR_OPTIONS
        /* only in 0.9.8m+ */
        SSL_clear_options(ssl_conn, SSL_get_options(ssl_conn) &
                                    ~SSL_CTX_get_options(sscf->ssl.ctx));
#endif

        SSL_set_options(ssl_conn, SSL_CTX_get_options(sscf->ssl.ctx));
    }

    return SSL_TLSEXT_ERR_OK;
}

#endif

#endif

/*
这样的请求行长度是不定的，它与URI长度相关，这意味着在读事件被触发时，内核套接字缓冲区的大小未必足够接收到全部的HTTP请求行，由此可以得出结论：
调用一次ngx_http_process_request_line方法不一定能够做完这项工作。所以，ngx_http_process_request_line方法也会作为读事件的回调方法，它可能会被
epoll这个事件驱动机制多次调度，反复地接收TCP流并使用状态机解析它们，直到确认接收到了完整的HTTP请求行，这个阶段才算完成，才会进入下一个阶段接收HTTP头部。
*/
/*
在接收完HTTP头部，第一次在业务上处理HTTP请求时，HTTP框架提供的处理方法是ngx_http_process_request。但如果该方法无法一次处
理完该请求的全部业务，在归还控制权到epoll事件模块后，该请求再次被回调时，将通过ngx_http_request_handler方法来处理
*/
static void
ngx_http_process_request_line(ngx_event_t *rev) //ngx_http_process_request_line方法来接收HTTP请求行
{
    ssize_t              n;
    ngx_int_t            rc, rv;
    ngx_str_t            host;
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

    c = rev->data;
    r = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0,
                   "http process request line");
    /*
     检查这个读事件是否已经超时，超时时间仍然是nginx.conf配置文件中指定的client_header_timeout。如果ngx_event_t事件的timeout标志为1，
     则认为接收HTTP请求已经超时，调用ngx_http_close_request方法关闭请求，同时由ngx_http_process_request_line方法中返回。
     */
    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        c->timedout = 1;
        ngx_http_close_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    rc = NGX_AGAIN;

//读取一行数据，分析出请求行中包含的method、uri、http_version信息。然后再一行一行处理请求头，并根据请求method与请求头的信息来决定
//是否有请求体以及请求体的长度，然后再去读取请求体
    for ( ;; ) {

        if (rc == NGX_AGAIN) {
            n = ngx_http_read_request_header(r);

            if (n == NGX_AGAIN || n == NGX_ERROR) { 
            //如果内核中的数据已经读完，但这时候头部字段还没有解析完毕，则把控制器交还给HTTP，当数据到来的时候触发
            //ngx_http_process_request_line，因为该函数外面rev->handler = ngx_http_process_request_line;
                return;
            }
        }
    
        rc = ngx_http_parse_request_line(r, r->header_in);

        if (rc == NGX_OK) { //请求行解析成功

            /* the request line has been parsed successfully */
            //请求行内容及长度    //GET /sample.jsp HTTP/1.1整行
            r->request_line.len = r->request_end - r->request_start;
            r->request_line.data = r->request_start;
            r->request_length = r->header_in->pos - r->request_start;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http request line: \"%V\"", &r->request_line);

            //请求方法 GET  POST等    //GET /sample.jsp HTTP/1.1  中的GET
            r->method_name.len = r->method_end - r->request_start + 1;
            r->method_name.data = r->request_line.data;

            //GET /sample.jsp HTTP/1.1  中的HTTP/1.1
            if (r->http_protocol.data) {
                r->http_protocol.len = r->request_end - r->http_protocol.data;
            }

            if (ngx_http_process_request_uri(r) != NGX_OK) {
                return;
            }

            if (r->host_start && r->host_end) {

                host.len = r->host_end - r->host_start;
                host.data = r->host_start;

                rc = ngx_http_validate_host(&host, r->pool, 0);

                if (rc == NGX_DECLINED) {
                    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                                  "client sent invalid host in request line");
                    ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
                    return;
                }

                if (rc == NGX_ERROR) {
                    ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return;
                }

                if (ngx_http_set_virtual_server(r, &host) == NGX_ERROR) {
                    return;
                }

                r->headers_in.server = host;
            }

            if (r->http_version < NGX_HTTP_VERSION_10) { //1.0以下版本没有请求头部字段，
                /*
                    用户请求的HTTP版本小于1.0（如HTTP 0.9版本），其处理过程将与HTTP l.0和HTTP l.1的完全不同，它不会有接收HTTP
                    头部这一步骤。这时将会调用ngx_http_find_virtual_server方法寻找到相应的虚拟主机?
                    */
                if (r->headers_in.server.len == 0
                    && ngx_http_set_virtual_server(r, &r->headers_in.server) //http0.9应该是从请求行获取虚拟主机?
                       == NGX_ERROR)
                {
                    return;
                }

                ngx_http_process_request(r);
                return;
            }

            //初始化用于存放http头部行的空间，用来存放http头部行
            if (ngx_list_init(&r->headers_in.headers, r->pool, 20,
                              sizeof(ngx_table_elt_t))
                != NGX_OK)
            {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }

            c->log->action = "reading client request headers";

            rev->handler = ngx_http_process_request_headers;
            ngx_http_process_request_headers(rev);//开始解析http头部行

            return;
        }

        if (rc != NGX_AGAIN) {//读取完毕内核该套接字上面的数据，头部行不全，则说明头部行不全关闭连接

            /* there was error while a request line parsing */

            ngx_log_error(NGX_LOG_INFO, c->log, 0,
                          ngx_http_client_errors[rc - NGX_HTTP_CLIENT_ERROR]);
            ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
            return;
        }

        //表示该行内容不够，例如recv读取的时候，没有把整行数据读取出来，返回后继续recv，然后接着上次解析的位置继续解析直到请求行解析完毕
        /* NGX_AGAIN: a request line parsing is still incomplete */
        /*
             如果ngx_http_parse_request_line方法返回NGX_AGAIN，则表示需要接收更多的字符流，这时需要对header_in缓冲区做判断，检查
         是否还有空闲的内存，如果还有未使用的内存可以继续接收字符流，检查缓冲区是否有未解析的字符流，否则调用
         ngx_http_alloc_large_header_buffer方法分配更大的接收缓冲区。到底分配多大呢？这由nginx.conf文件中的large_client_header_buffers配置项指定。
          */
        if (r->header_in->pos == r->header_in->end) {

            rv = ngx_http_alloc_large_header_buffer(r, 1);

            if (rv == NGX_ERROR) {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }

            if (rv == NGX_DECLINED) {
                r->request_line.len = r->header_in->end - r->request_start;
                r->request_line.data = r->request_start;

                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                              "client sent too long URI");
                ngx_http_finalize_request(r, NGX_HTTP_REQUEST_URI_TOO_LARGE);
                return;
            }
        }
        //表示头部行没有解析完成，继续读数据解析
    }
}


ngx_int_t
ngx_http_process_request_uri(ngx_http_request_t *r)
{
    ngx_http_core_srv_conf_t  *cscf;

    if (r->args_start) {
        r->uri.len = r->args_start - 1 - r->uri_start;
    } else {
        r->uri.len = r->uri_end - r->uri_start;
    }

    if (r->complex_uri || r->quoted_uri) {

        r->uri.data = ngx_pnalloc(r->pool, r->uri.len + 1);
        if (r->uri.data == NULL) {
            ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return NGX_ERROR;
        }

        cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

        if (ngx_http_parse_complex_uri(r, cscf->merge_slashes) != NGX_OK) {
            r->uri.len = 0;

            ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "client sent invalid request");
            ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
            return NGX_ERROR;
        }

    } else {
        r->uri.data = r->uri_start;
    }

    r->unparsed_uri.len = r->uri_end - r->uri_start;
    r->unparsed_uri.data = r->uri_start;

    r->valid_unparsed_uri = r->space_in_uri ? 0 : 1;

    if (r->uri_ext) {
        if (r->args_start) {
            r->exten.len = r->args_start - 1 - r->uri_ext;
        } else {
            r->exten.len = r->uri_end - r->uri_ext;
        }

        r->exten.data = r->uri_ext;
    }

    if (r->args_start && r->uri_end > r->args_start) {
        r->args.len = r->uri_end - r->args_start;
        r->args.data = r->args_start;
    }

#if (NGX_WIN32)
    {
    u_char  *p, *last;

    p = r->uri.data;
    last = r->uri.data + r->uri.len;

    while (p < last) {

        if (*p++ == ':') {

            /*
             * this check covers "::$data", "::$index_allocation" and
             * ":$i30:$index_allocation"
             */

            if (p < last && *p == '$') {
                ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                              "client sent unsafe win32 URI");
                ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
                return NGX_ERROR;
            }
        }
    }

    p = r->uri.data + r->uri.len - 1;

    while (p > r->uri.data) {

        if (*p == ' ') {
            p--;
            continue;
        }

        if (*p == '.') {
            p--;
            continue;
        }

        break;
    }

    if (p != r->uri.data + r->uri.len - 1) {
        r->uri.len = p + 1 - r->uri.data;
        ngx_http_set_exten(r);
    }

    }
#endif

/*
2016/01/07 12:38:01[      ngx_http_process_request_line,  1002]  [debug] 20090#20090: *14 http request line: "GET /download/nginx-1.9.2.rar?st=xhWL03HbtjrojpEAfiD6Mw&e=1452139931 HTTP/1.1"
2016/01/07 12:38:01[       ngx_http_process_request_uri,  1223]  [debug] 20090#20090: *14 http uri: "/download/nginx-1.9.2.rar"
2016/01/07 12:38:01[       ngx_http_process_request_uri,  1226]  [debug] 20090#20090: *14 http args: "st=xhWL03HbtjrojpEAfiD6Mw&e=1452139931"
2016/01/07 12:38:01[       ngx_http_process_request_uri,  1229]  [debug] 20090#20090: *14 http exten: "rar"
*/
    //http://10.135.10.167/aaaaaaaa?bbbb  http uri: "/aaaaaaaa"  http args: "bbbb"  同时"GET /aaaaaaaa?bbbb.txt HTTP/1.1"中的/aaaaaaa?bbbb.txt和uri中的一样
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http uri: \"%V\"", &r->uri);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http args: \"%V\"", &r->args);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http exten: \"%V\"", &r->exten);

    return NGX_OK;
}

/*
GET /sample.jsp HTTP/1.1

 

Accept:image/gif.image/jpeg,**

Accept-Language:zh-cn

Connection:Keep-Alive

Host:localhost

User-Agent:Mozila/4.0(compatible:MSIE5.01:Windows NT5.0)

Accept-Encoding:gzip,deflate.

*/ //解析上面的GET /sample.jsp HTTP/1.1以外的配置

//ngx_http_parse_request_line解析请求行， ngx_http_process_request_headers(ngx_http_parse_header_line)解析头部行(请求头部) 接收包体ngx_http_read_client_request_body
static void
ngx_http_process_request_headers(ngx_event_t *rev)
{
    u_char                     *p;
    size_t                      len;
    ssize_t                     n;
    ngx_int_t                   rc, rv;
    ngx_table_elt_t            *h;
    ngx_connection_t           *c;
    ngx_http_header_t          *hh;
    ngx_http_request_t         *r;
    ngx_http_core_srv_conf_t   *cscf;
    ngx_http_core_main_conf_t  *cmcf;

    c = rev->data;
    r = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0,
                   "http process request header line");

    if (rev->timedout) {//如果tcp连接建立后，等了client_header_timeout秒一直没有收到客户端的数据包过来，则关闭连接
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT, "client timed out");
        c->timedout = 1;
        ngx_http_close_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    rc = NGX_AGAIN;

    for ( ;; ) {

        if (rc == NGX_AGAIN) {

            if (r->header_in->pos == r->header_in->end) {//说明header_in空间已经用完了，无法继续存储recv的数据，则重新分配大空间

                rv = ngx_http_alloc_large_header_buffer(r, 0);

                if (rv == NGX_ERROR) {
                    ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return;
                }

                if (rv == NGX_DECLINED) {
                    p = r->header_name_start;

                    r->lingering_close = 1;

                    if (p == NULL) {
                        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                                      "client sent too large request");
                        ngx_http_finalize_request(r,
                                            NGX_HTTP_REQUEST_HEADER_TOO_LARGE);
                        return;
                    }

                    len = r->header_in->end - p;

                    if (len > NGX_MAX_ERROR_STR - 300) {
                        len = NGX_MAX_ERROR_STR - 300;
                    }

                    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                                "client sent too long header line: \"%*s...\"",
                                len, r->header_name_start);

                    ngx_http_finalize_request(r,
                                            NGX_HTTP_REQUEST_HEADER_TOO_LARGE);
                    return;
                }
            }

            //这里至少读一次
            n = ngx_http_read_request_header(r); //说明内存中的数据还没有读完，需要读完后继续解析

            if (n == NGX_AGAIN || n == NGX_ERROR) {  
                return;
            }
        }

        /* the host header could change the server configuration context */
        cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

        rc = ngx_http_parse_header_line(r, r->header_in,
                                        cscf->underscores_in_headers);


        if (rc == NGX_OK) {

            r->request_length += r->header_in->pos - r->header_name_start; //request_length增加一行字符长度

            if (r->invalid_header && cscf->ignore_invalid_headers) {

                /* there was error while a header line parsing */

                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                              "client sent invalid header line: \"%*s\"",
                              r->header_end - r->header_name_start,
                              r->header_name_start);
                continue;
            }

            /* a header line has been parsed successfully */
            /* 解析的头不会KEY:VALUE存入到headers_in中 */
            h = ngx_list_push(&r->headers_in.headers);
            if (h == NULL) {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }

            h->hash = r->header_hash;
            //拷贝name:value中的name到key中,name后面的冒号被用\0替换了
            h->key.len = r->header_name_end - r->header_name_start;
            h->key.data = r->header_name_start;
            h->key.data[h->key.len] = '\0';
            
            //拷贝name:value中的value到value中，value后的换行符被用\0替换了
            h->value.len = r->header_end - r->header_start;
            h->value.data = r->header_start;
            h->value.data[h->value.len] = '\0';

            h->lowcase_key = ngx_pnalloc(r->pool, h->key.len);
            if (h->lowcase_key == NULL) {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }

             //拷贝name:value中的name到key中,包括name后面的冒号
            if (h->key.len == r->lowcase_index) {
                ngx_memcpy(h->lowcase_key, r->lowcase_header, h->key.len);

            } else {
                ngx_strlow(h->lowcase_key, h->key.data, h->key.len);
            }

            //见ngx_http_headers_in  //检查解析到的头部key:value中的对应ngx_http_header_t结构，即ngx_http_headers_in中的成员
            hh = ngx_hash_find(&cmcf->headers_in_hash, h->hash,
                               h->lowcase_key, h->key.len); 
            

            //从ngx_http_headers_in找到与name匹配的字符串，然后只需对应的handler,一般就是把解析出的name:value中的value存放到headers_in成员的headers链表中
            
            //参考ngx_http_headers_in，通过该数组中的回调hander来存储解析到的请求行name:value中的value到headers_in的响应成员中，见ngx_http_process_request_headers
            if (hh && hh->handler(r, h, hh->offset) != NGX_OK) {
                return;
            }

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http header: \"%V: %V\"",
                           &h->key, &h->value); //如果打开调试开关，则name:value可以存到error.log中

            continue; //继续解析下一行
        }

        if (rc == NGX_HTTP_PARSE_HEADER_DONE) {//头部行解析完毕

            /* a whole header has been parsed successfully */

            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http header done");
            /*
                请求行
                头部行
                空行(这里是\r\n)
                内容
               */
            r->request_length += r->header_in->pos - r->header_name_start;//把空行的\r\n加上

            r->http_state = NGX_HTTP_PROCESS_REQUEST_STATE;

            rc = ngx_http_process_request_header(r);

            if (rc != NGX_OK) {
                return;
            }

            ngx_http_process_request(r);

            return;
        }

        if (rc == NGX_AGAIN) {//返回NGX_AGAIN时，表示还需要接收到更多的字符流才能继续解析

            /* a header line parsing is still not complete */

            continue;
        }

        /* rc == NGX_HTTP_PARSE_INVALID_HEADER: "\r" is not followed by "\n" */
        /*
            当调用ngx_http_parse_header_line方法解析字符串构成的HTTP时，是有可能遇到非法的或者Nginx当前版本不支持的HTTP头部的，
            这时该方法会返回错误，于是调用ngx_http_finalize_request方法，向客户端发送NGX_ HTTP BAD_REQUEST宏对应的400错误码响应。
          */
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client sent invalid header line: \"%*s\\r...\"",
                      r->header_end - r->header_name_start,
                      r->header_name_start);
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return;
    }
}


static ssize_t
ngx_http_read_request_header(ngx_http_request_t *r)
{
    ssize_t                    n;
    ngx_event_t               *rev;
    ngx_connection_t          *c;
    ngx_http_core_srv_conf_t  *cscf;

    c = r->connection;
    rev = c->read;

    //它的pos成员和last成员指向的地址之间的内存就是接收到的还未解析的字符流
    n = r->header_in->last - r->header_in->pos; //header_in指针指向ngx_connection_t->buffer

    //第一次调用ngx_http_process_request_line方法时缓冲区里必然是空的，这时会调用封装的recv方法把Linux内核套接字缓冲区中的TCP流复制到header_in缓冲区中
    if (n > 0) {//在ngx_http_wait_request_handler中会首先读取一次，这里一般是大于0的
        return n;
    }

    if (rev->ready) { //如果来自对端的数据还没有读取获取还没有读完，则继续读
        n = c->recv(c, r->header_in->last,
                    r->header_in->end - r->header_in->last); //ngx_unix_recv
    } else {
        n = NGX_AGAIN;
    }

    //每次读取完客户端过来的请求数据后，都会执行到这里，一般情况是第一次在ngx_http_wait_request_handler读取完所有数据，然后会在
    //ngx_http_process_request_line中再次调用本ngx_http_read_request_header函数，第二次的时候已经没数据了，会走到这里,例如客户端发送过来的
    //头部行不全，单客户端一直不发生剩余的部分
    if (n == NGX_AGAIN) { //如果recv返回NGX_AGAIN,则需要重新add read event,这样下次有数据来可以继续读,
        //当一次处理客户端请求结束后，会把ngx_http_process_request_line添加到定时器中，如果等client_header_timeout还没有信的请求数据过来，
        //则会走到ngx_http_read_request_header中的ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);从而关闭连接
        if (!rev->timer_set) {
            cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
            //注意，在解析到完整的头部行和请求行后，会在ngx_http_process_request中会把读事件超时定时器删除
            //在请求处理完发送给客户端数据后，rev->handler = ngx_http_keepalive_handler
            ngx_add_timer(rev, cscf->client_header_timeout, NGX_FUNC_LINE);
        }

        if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) {
            ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return NGX_ERROR;
        }

        return NGX_AGAIN;
    }

    if (n == 0) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "client prematurely closed connection");
    }

    if (n == 0 || n == NGX_ERROR) { //TCP连接出错
        c->error = 1;
        c->log->action = "reading client request headers";

        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return NGX_ERROR;
    }

    r->header_in->last += n;

    return n;
}

/*
//client_header_buffer_size为读取客户端数据时默认分配的空间，如果该空间不够存储http头部行和请求行，则会调用large_client_header_buffers
//从新分配空间，并把之前的空间内容拷贝到新空间中，所以，这意味着可变长度的HTTP请求行加上HTTP头部的长度总和不能超过large_client_ header_
//buffers指定的字节数，否则Nginx将会报错。
*/
//ngx_http_alloc_large_header_buffer方法分配更大的接收缓冲区。到底分配多大呢？这由nginx.conf文件中的large_client_header_buffers配置项指定。
static ngx_int_t
ngx_http_alloc_large_header_buffer(ngx_http_request_t *r,
    ngx_uint_t request_line)
{
    u_char                    *old, *new;
    ngx_buf_t                 *b;
    ngx_http_connection_t     *hc;
    ngx_http_core_srv_conf_t  *cscf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http alloc large header buffer");

    if (request_line && r->state == 0) {

        /* the client fills up the buffer with "\r\n" */

        r->header_in->pos = r->header_in->start;
        r->header_in->last = r->header_in->start;

        return NGX_OK;
    }

    old = request_line ? r->request_start : r->header_name_start;

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

    if (r->state != 0
        && (size_t) (r->header_in->pos - old)
                                     >= cscf->large_client_header_buffers.size)
    {
        return NGX_DECLINED;
    }

    hc = r->http_connection;

    if (hc->nfree) {
        b = hc->free[--hc->nfree];

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http large header free: %p %uz",
                       b->pos, b->end - b->last);

    } else if (hc->nbusy < cscf->large_client_header_buffers.num) {

        if (hc->busy == NULL) {
            hc->busy = ngx_palloc(r->connection->pool,
                  cscf->large_client_header_buffers.num * sizeof(ngx_buf_t *));
            if (hc->busy == NULL) {
                return NGX_ERROR;
            }
        }

        b = ngx_create_temp_buf(r->connection->pool,
                                cscf->large_client_header_buffers.size);
        if (b == NULL) {
            return NGX_ERROR;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http large header alloc: %p %uz",
                       b->pos, b->end - b->last);

    } else {
        return NGX_DECLINED;
    }

    hc->busy[hc->nbusy++] = b;

    if (r->state == 0) {
        /*
         * r->state == 0 means that a header line was parsed successfully
         * and we do not need to copy incomplete header line and
         * to relocate the parser header pointers
         */

        r->header_in = b;

        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http large header copy: %d", r->header_in->pos - old);

    new = b->start;

    ngx_memcpy(new, old, r->header_in->pos - old);

    b->pos = new + (r->header_in->pos - old);
    b->last = new + (r->header_in->pos - old);

    if (request_line) {
        r->request_start = new;

        if (r->request_end) {
            r->request_end = new + (r->request_end - old);
        }

        r->method_end = new + (r->method_end - old);

        r->uri_start = new + (r->uri_start - old);
        r->uri_end = new + (r->uri_end - old);

        if (r->schema_start) {
            r->schema_start = new + (r->schema_start - old);
            r->schema_end = new + (r->schema_end - old);
        }

        if (r->host_start) {
            r->host_start = new + (r->host_start - old);
            if (r->host_end) {
                r->host_end = new + (r->host_end - old);
            }
        }

        if (r->port_start) {
            r->port_start = new + (r->port_start - old);
            r->port_end = new + (r->port_end - old);
        }

        if (r->uri_ext) {
            r->uri_ext = new + (r->uri_ext - old);
        }

        if (r->args_start) {
            r->args_start = new + (r->args_start - old);
        }

        if (r->http_protocol.data) {
            r->http_protocol.data = new + (r->http_protocol.data - old);
        }

    } else {
        r->header_name_start = new;
        r->header_name_end = new + (r->header_name_end - old);
        r->header_start = new + (r->header_start - old);
        r->header_end = new + (r->header_end - old);
    }

    r->header_in = b;

    return NGX_OK;
}


static ngx_int_t
ngx_http_process_header_line(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  **ph;

    ph = (ngx_table_elt_t **) ((char *) &r->headers_in + offset);

    if (*ph == NULL) {
        *ph = h;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_process_unique_header_line(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  **ph;

    ph = (ngx_table_elt_t **) ((char *) &r->headers_in + offset);

    if (*ph == NULL) {
        *ph = h;
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "client sent duplicate header line: \"%V: %V\", "
                  "previous value: \"%V: %V\"",
                  &h->key, &h->value, &(*ph)->key, &(*ph)->value);

    ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);

    return NGX_ERROR;
}

//ngx_http_process_request_headers->ngx_http_process_host->ngx_http_set_virtual_server
/*
handler的三个参数分别为(r, h, hh->offset):r为对应的连接请求，h存储为头部行key:value(如:Content-Type: text/html)值，hh->offset即
ngx_http_headers_in中成员的对应offset(如请求行带有host，则offset=offsetof(ngx_http_headers_in_t, host))
*/
static ngx_int_t
ngx_http_process_host(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset) //根据请求中的host来进行server查找和定位
{
    ngx_int_t  rc;
    ngx_str_t  host;

    if (r->headers_in.host == NULL) {
        r->headers_in.host = h;
    }

    host = h->value;

    rc = ngx_http_validate_host(&host, r->pool, 0);

    if (rc == NGX_DECLINED) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "client sent invalid host header");
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return NGX_ERROR;
    }

    if (rc == NGX_ERROR) {
        ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_ERROR;
    }

    if (r->headers_in.server.len) { //来自同一个TCP连接的客户端，他们的server是一样的
        return NGX_OK;
    }

    if (ngx_http_set_virtual_server(r, &host) == NGX_ERROR) {
        return NGX_ERROR;
    }

    r->headers_in.server = host;

    return NGX_OK;
}


static ngx_int_t
ngx_http_process_connection(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    if (ngx_strcasestrn(h->value.data, "close", 5 - 1)) { 
        r->headers_in.connection_type = NGX_HTTP_CONNECTION_CLOSE;

    } else if (ngx_strcasestrn(h->value.data, "keep-alive", 10 - 1)) {//长连接
        r->headers_in.connection_type = NGX_HTTP_CONNECTION_KEEP_ALIVE;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_process_user_agent(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char  *user_agent, *msie;

    if (r->headers_in.user_agent) {
        return NGX_OK;
    }

    r->headers_in.user_agent = h;

    /* check some widespread browsers while the header is in CPU cache */

    user_agent = h->value.data;

    msie = ngx_strstrn(user_agent, "MSIE ", 5 - 1);

    if (msie && msie + 7 < user_agent + h->value.len) {

        r->headers_in.msie = 1;

        if (msie[6] == '.') {

            switch (msie[5]) {
            case '4':
            case '5':
                r->headers_in.msie6 = 1;
                break;
            case '6':
                if (ngx_strstrn(msie + 8, "SV1", 3 - 1) == NULL) {
                    r->headers_in.msie6 = 1;
                }
                break;
            }
        }

#if 0
        /* MSIE ignores the SSL "close notify" alert */
        if (c->ssl) {
            c->ssl->no_send_shutdown = 1;
        }
#endif
    }

    if (ngx_strstrn(user_agent, "Opera", 5 - 1)) {
        r->headers_in.opera = 1;
        r->headers_in.msie = 0;
        r->headers_in.msie6 = 0;
    }

    if (!r->headers_in.msie && !r->headers_in.opera) {

        if (ngx_strstrn(user_agent, "Gecko/", 6 - 1)) {
            r->headers_in.gecko = 1;

        } else if (ngx_strstrn(user_agent, "Chrome/", 7 - 1)) {
            r->headers_in.chrome = 1;

        } else if (ngx_strstrn(user_agent, "Safari/", 7 - 1)
                   && ngx_strstrn(user_agent, "Mac OS X", 8 - 1))
        {
            r->headers_in.safari = 1;

        } else if (ngx_strstrn(user_agent, "Konqueror", 9 - 1)) {
            r->headers_in.konqueror = 1;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_process_multi_header_lines(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_array_t       *headers;
    ngx_table_elt_t  **ph;

    headers = (ngx_array_t *) ((char *) &r->headers_in + offset);

    if (headers->elts == NULL) {
        if (ngx_array_init(headers, r->pool, 1, sizeof(ngx_table_elt_t *))
            != NGX_OK)
        {
            ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return NGX_ERROR;
        }
    }

    ph = ngx_array_push(headers);
    if (ph == NULL) {
        ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_ERROR;
    }

    *ph = h;
    return NGX_OK;
}

//ngx_http_process_request_headers头部行解析完毕后调用函数ngx_http_process_request_header
//解析到头部行的host:  时会执行该函数
ngx_int_t
ngx_http_process_request_header(ngx_http_request_t *r)
{
    if (r->headers_in.server.len == 0
        && ngx_http_set_virtual_server(r, &r->headers_in.server)
           == NGX_ERROR)  
    {
        return NGX_ERROR;
    }

    if (r->headers_in.host == NULL && r->http_version > NGX_HTTP_VERSION_10) { //1.0以上版本必须携带host
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                   "client sent HTTP/1.1 request without \"Host\" header");
        ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
        return NGX_ERROR;
    }

    if (r->headers_in.content_length) {
        r->headers_in.content_length_n =
                            ngx_atoof(r->headers_in.content_length->value.data,
                                      r->headers_in.content_length->value.len);

        if (r->headers_in.content_length_n == NGX_ERROR) {
            ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "client sent invalid \"Content-Length\" header");
            ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
            return NGX_ERROR;
        }
    }

    if (r->method & NGX_HTTP_TRACE) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "client sent TRACE method");
        ngx_http_finalize_request(r, NGX_HTTP_NOT_ALLOWED);
        return NGX_ERROR;
    }

    if (r->headers_in.transfer_encoding) {
        if (r->headers_in.transfer_encoding->value.len == 7
            && ngx_strncasecmp(r->headers_in.transfer_encoding->value.data,
                               (u_char *) "chunked", 7) == 0) //Transfer-Encoding:chunked
        {
            r->headers_in.content_length = NULL;
            r->headers_in.content_length_n = -1;
            r->headers_in.chunked = 1;

        } else if (r->headers_in.transfer_encoding->value.len != 8
            || ngx_strncasecmp(r->headers_in.transfer_encoding->value.data,
                               (u_char *) "identity", 8) != 0)
        {
            ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "client sent unknown \"Transfer-Encoding\": \"%V\"",
                          &r->headers_in.transfer_encoding->value);
            ngx_http_finalize_request(r, NGX_HTTP_NOT_IMPLEMENTED);
            return NGX_ERROR;
        }
    }

    if (r->headers_in.connection_type == NGX_HTTP_CONNECTION_KEEP_ALIVE) {
        if (r->headers_in.keep_alive) {//Connection=keep-alive时才有效
            r->headers_in.keep_alive_n =
                            ngx_atotm(r->headers_in.keep_alive->value.data,
                                      r->headers_in.keep_alive->value.len);
        }
    }

    return NGX_OK;
}

/*
ngx_http_process_request方法负责在接收完HTTP头部后，第一次与各个HTTP模块共同按阶段处理请求，而对于ngx_http_request_handler方法，
如果ngx_http_process_request没能处理完请求，这个请求上的事件再次被触发，那就将由此方法继续处理了。
*/
//ngx_http_process_request_headers头部行解析完毕后调用函数ngx_http_process_request_header
void
ngx_http_process_request(ngx_http_request_t *r) 
{
    ngx_connection_t  *c;

    c = r->connection;

#if (NGX_HTTP_SSL)

    if (r->http_connection->ssl) {
        long                      rc;
        X509                     *cert;
        ngx_http_ssl_srv_conf_t  *sscf;

        if (c->ssl == NULL) {
            ngx_log_error(NGX_LOG_INFO, c->log, 0,
                          "client sent plain HTTP request to HTTPS port");
            ngx_http_finalize_request(r, NGX_HTTP_TO_HTTPS);
            return;
        }

        sscf = ngx_http_get_module_srv_conf(r, ngx_http_ssl_module);

        if (sscf->verify) {
            rc = SSL_get_verify_result(c->ssl->connection);

            if (rc != X509_V_OK
                && (sscf->verify != 3 || !ngx_ssl_verify_error_optional(rc)))
            {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                              "client SSL certificate verify error: (%l:%s)",
                              rc, X509_verify_cert_error_string(rc));

                ngx_ssl_remove_cached_session(sscf->ssl.ctx,
                                       (SSL_get0_session(c->ssl->connection)));

                ngx_http_finalize_request(r, NGX_HTTPS_CERT_ERROR);
                return;
            }

            if (sscf->verify == 1) {
                cert = SSL_get_peer_certificate(c->ssl->connection);

                if (cert == NULL) {
                    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                                  "client sent no required SSL certificate");

                    ngx_ssl_remove_cached_session(sscf->ssl.ctx,
                                       (SSL_get0_session(c->ssl->connection)));

                    ngx_http_finalize_request(r, NGX_HTTPS_NO_CERT);
                    return;
                }

                X509_free(cert);
            }
        }
    }

#endif
    /*
    由于现在已经开始准备调用各HTTP模块处理请求了，因此不再存在接收HTTP请求头部超时的问题，那就需要从定时器中把当前连接的读事件移除了。
    检查读事件对应的timer_set标志位，力1时表示读事件已经添加到定时器中了，这时需要调用ngx_del_timer从定时器中移除读事件；
     */
    if (c->read->timer_set) {//ngx_http_read_request_header中读取不到数据的时候返回NGX_AGIN，会添加定时器和读事件表示继续等待客户端数据到来
        ngx_del_timer(c->read, NGX_FUNC_LINE);
    }

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_reading, -1);
    r->stat_reading = 0;
    (void) ngx_atomic_fetch_add(ngx_stat_writing, 1);
    r->stat_writing = 1;
#endif

/*
从现在开始不会再需要接收HTTP请求行或者头部，所以需要重新设置当前连接读/写事件的回调方法。在这一步骤中，将同时把读事件、写事件的回调
方法都设置为ngx_http_request_handler方法，请求的后续处理都是通过ngx_http_request_handler方法进行的。
 */
    c->read->handler = ngx_http_request_handler; //由读写事件触发ngx_http_request_handler  //由epoll读事件在ngx_epoll_process_events触发
    c->write->handler = ngx_http_request_handler;   //由epoll写事件在ngx_epoll_process_events触发

/*
设置ngx_http_request_t结构体的read_event_handler方法gx_http_block_reading。当再次有读事件到来时，将会调用ngx_http_block_reading方法
处理请求。而这里将它设置为ngx_http_block_reading方法，这个方法可认为不做任何事，它的意义在于，目前已经开始处理HTTP请求，除非某个HTTP模块重新
设置了read_event_handler方法，否则任何读事件都将得不到处理，也可似认为读事件被阻 塞了。
*/
    r->read_event_handler = ngx_http_block_reading; //表示暂时不要读取客户端请求    

    /* ngx_http_process_request和ngx_http_request_handler这两个方法的共通之处在于，它们都会先按阶段调用各个HTTP模块处理请求，再处理post请求 */
    ngx_http_handler(r); //这里面会执行ngx_http_core_run_phases,执行11个阶段

/*
HTTP框架无论是调用ngx_http_process_request方法（首次从业务上处理请求）还是ngx_http_request_handler方法（TCP连接上后续的事件触发时）处理
请求，最后都有一个步骤，就是调用ngx_http_run_posted_requests方法处理post请求

11个阶段执行完毕后，调用ngx_http_run_posted_requests方法执行post请求，这里一般都是对subrequest进行处理
*/
    ngx_http_run_posted_requests(c); /*  */
}

//检测头部行host:后面的参数是否正确
static ngx_int_t
ngx_http_validate_host(ngx_str_t *host, ngx_pool_t *pool, ngx_uint_t alloc)
{
    u_char  *h, ch;
    size_t   i, dot_pos, host_len;

    enum {
        sw_usual = 0,
        sw_literal,
        sw_rest
    } state;

    dot_pos = host->len;
    host_len = host->len;

    h = host->data;

    state = sw_usual;

    for (i = 0; i < host->len; i++) {
        ch = h[i];

        switch (ch) {

        case '.':
            if (dot_pos == i - 1) {
                return NGX_DECLINED;
            }
            dot_pos = i;
            break;

        case ':':
            if (state == sw_usual) {
                host_len = i;
                state = sw_rest;
            }
            break;

        case '[':
            if (i == 0) {
                state = sw_literal;
            }
            break;

        case ']':
            if (state == sw_literal) {
                host_len = i + 1;
                state = sw_rest;
            }
            break;

        case '\0':
            return NGX_DECLINED;

        default:

            if (ngx_path_separator(ch)) {
                return NGX_DECLINED;
            }

            if (ch >= 'A' && ch <= 'Z') {
                alloc = 1;
            }

            break;
        }
    }

    if (dot_pos == host_len - 1) {
        host_len--;
    }

    if (host_len == 0) {
        return NGX_DECLINED;
    }

    if (alloc) {
        host->data = ngx_pnalloc(pool, host_len);
        if (host->data == NULL) {
            return NGX_ERROR;
        }

        ngx_strlow(host->data, h, host_len);
    }

    host->len = host_len;

    return NGX_OK;
}

/*
当客户端建立连接后，并发送请求数据过来后，在ngx_http_create_request中从ngx_http_connection_t->conf_ctx获取这三个值，也就是根据客户端连接
本端所处IP:port所对应的默认server{}块上下文，如果是以下情况:ip:port相同，单在不同的server{}块中，那么有可能客户端请求过来的时候携带的host
头部项的server_name不在默认的server{}中，而在另外的server{}中，所以需要通过ngx_http_set_virtual_server重新获取server{}和location{}上下文配置
例如:
    server {  #1
        listen 1.1.1.1:80;
        server_name aaa
    }

    server {   #2
        listen 1.1.1.1:80;
        server_name bbb
    }
    这两个server{}占用同一个ngx_http_conf_addr_t，但他们拥有两个不同的ngx_http_core_srv_conf_t(存在于ngx_http_conf_addr_t->servers),
    这个配置在ngx_http_init_connection中获取这个ngx_http_port_t(1个ngx_http_port_t对应一个ngx_http_conf_addr_t)把ngx_http_connection_t->conf_ctx
    指向ngx_http_addr_conf_s->default_server,也就是指向#1,然后ngx_http_create_request中把main_conf srv_conf  loc_conf 指向#1,
    但如果请求行的头部的host:bbb，那么需要重新获取对应的server{} #2,见ngx_http_set_virtual_server->ngx_http_find_virtual_server
 */

//在解析到http头部的host字段后，会通过//ngx_http_process_request_headers->ngx_http_process_host->ngx_http_set_virtual_server
//获取server_name配置和host字符串完全一样的server{}上下文配置ngx_http_core_srv_conf_t和ngx_http_core_loc_conf_t
static ngx_int_t
ngx_http_set_virtual_server(ngx_http_request_t *r, ngx_str_t *host)
{
    ngx_int_t                  rc;
    ngx_http_connection_t     *hc;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;

#if (NGX_SUPPRESS_WARN)
    cscf = NULL;
#endif

    hc = r->http_connection;

#if (NGX_HTTP_SSL && defined SSL_CTRL_SET_TLSEXT_HOSTNAME)

    if (hc->ssl_servername) {
        if (hc->ssl_servername->len == host->len
            && ngx_strncmp(hc->ssl_servername->data,
                           host->data, host->len) == 0)
        {
#if (NGX_PCRE)
            if (hc->ssl_servername_regex
                && ngx_http_regex_exec(r, hc->ssl_servername_regex,
                                          hc->ssl_servername) != NGX_OK)
            {
                ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return NGX_ERROR;
            }
#endif
            return NGX_OK;
        }
    }

#endif
    //查找host字符串和server_name配置相同的server{}上下文
    rc = ngx_http_find_virtual_server(r->connection,
                                      hc->addr_conf->virtual_names,
                                      host, r, &cscf);

    if (rc == NGX_ERROR) {
        ngx_http_close_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return NGX_ERROR;
    }

#if (NGX_HTTP_SSL && defined SSL_CTRL_SET_TLSEXT_HOSTNAME)

    if (hc->ssl_servername) {
        ngx_http_ssl_srv_conf_t  *sscf;

        if (rc == NGX_DECLINED) {
            cscf = hc->addr_conf->default_server;
            rc = NGX_OK;
        }

        sscf = ngx_http_get_module_srv_conf(cscf->ctx, ngx_http_ssl_module);

        if (sscf->verify) {
            ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "client attempted to request the server name "
                          "different from that one was negotiated");
            ngx_http_finalize_request(r, NGX_HTTP_BAD_REQUEST);
            return NGX_ERROR;
        }
    }

#endif

    if (rc == NGX_DECLINED) {
        return NGX_OK;
    }

    /*
当客户端建立连接后，并发送请求数据过来后，在ngx_http_create_request中从ngx_http_connection_t->conf_ctx获取这三个值，也就是根据客户端连接
本端所处IP:port所对应的默认server{}块上下文，如果是以下情况:ip:port相同，单在不同的server{}块中，那么有可能客户端请求过来的时候携带的host
头部项的server_name不在默认的server{}中，而在另外的server{}中，所以需要通过ngx_http_set_virtual_server重新获取server{}和location{}上下文配置
例如:
    server {  #1
        listen 1.1.1.1:80;
        server_name aaa
    }

    server {   #2
        listen 1.1.1.1:80;
        server_name bbb
    }
    这个配置在ngx_http_init_connection中把ngx_http_connection_t->conf_ctx指向ngx_http_addr_conf_s->default_server,也就是指向#1,然后
    ngx_http_create_request中把main_conf srv_conf  loc_conf 指向#1,
    但如果请求行的头部的host:bbb，那么需要重新获取对应的server{} #2,见ngx_http_set_virtual_server
 */
    //把 srv_conf  loc_conf指向host与server_name配置相同的server{]上下文配置srv_conf  loc_conf

    /*
        这里也体现了在ngx_http_init_connection中获取http{}上下文ctx，如果客户端请求中带有host参数，则会继续在ngx_http_set_virtual_server中重新获取
        对应的server{}和location{}，如果客户端请求不带host头部行，则使用默认的server{},见 ngx_http_init_connection
    */
    r->srv_conf = cscf->ctx->srv_conf;
    r->loc_conf = cscf->ctx->loc_conf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ngx_set_connection_log(r->connection, clcf->error_log);

    return NGX_OK;
}

//从virtual_names->names  hash表中查找host所处字符串作为key，其在hash中对应节点的value值,也就是查找host字符串sever_name配置相同的server{}上下文。
/*
当客户端建立连接后，并发送请求数据过来后，在ngx_http_create_request中从ngx_http_connection_t->conf_ctx获取这三个值，也就是根据客户端连接
本端所处IP:port所对应的默认server{}块上下文，如果是以下情况:ip:port相同，单在不同的server{}块中，那么有可能客户端请求过来的时候携带的host
头部项的server_name不在默认的server{}中，而在另外的server{}中，所以需要通过ngx_http_set_virtual_server重新获取server{}和location{}上下文配置
例如:
    server {  #1
        listen 1.1.1.1:80;
        server_name aaa
    }

    server {   #2
        listen 1.1.1.1:80;
        server_name bbb
    }
    这两个server{}占用同一个ngx_http_conf_addr_t，但他们拥有两个不同的ngx_http_core_srv_conf_t(存在于ngx_http_conf_addr_t->servers),
    这个配置在ngx_http_init_connection中获取这个ngx_http_port_t(1个ngx_http_port_t对应一个ngx_http_conf_addr_t)把ngx_http_connection_t->conf_ctx
    指向ngx_http_addr_conf_s->default_server,也就是指向#1,然后ngx_http_create_request中把main_conf srv_conf  loc_conf 指向#1,
    但如果请求行的头部的host:bbb，那么需要重新获取对应的server{} #2,见ngx_http_set_virtual_server->ngx_http_find_virtual_server
 */static ngx_int_t
ngx_http_find_virtual_server(ngx_connection_t *c,
    ngx_http_virtual_names_t *virtual_names, ngx_str_t *host,
    ngx_http_request_t *r, ngx_http_core_srv_conf_t **cscfp) //获取host字符串对应的server_name所对处的server{}配置块信息
{
    ngx_http_core_srv_conf_t  *cscf;

    if (virtual_names == NULL) {
        return NGX_DECLINED;
    }

    //virtual_names->names hash中的key为字符串server_name xxx中的xxx,value为该server_name xxx所在的server{}块
    cscf = ngx_hash_find_combined(&virtual_names->names,
                                  ngx_hash_key(host->data, host->len),
                                  host->data, host->len);

    if (cscf) {
        *cscfp = cscf;
        return NGX_OK;
    }

#if (NGX_PCRE)

    if (host->len && virtual_names->nregex) {
        ngx_int_t                n;
        ngx_uint_t               i;
        ngx_http_server_name_t  *sn;

        sn = virtual_names->regex;

#if (NGX_HTTP_SSL && defined SSL_CTRL_SET_TLSEXT_HOSTNAME)

        if (r == NULL) {
            ngx_http_connection_t  *hc;

            for (i = 0; i < virtual_names->nregex; i++) {

                n = ngx_regex_exec(sn[i].regex->regex, host, NULL, 0);

                if (n == NGX_REGEX_NO_MATCHED) {
                    continue;
                }

                if (n >= 0) {
                    hc = c->data;
                    hc->ssl_servername_regex = sn[i].regex;

                    *cscfp = sn[i].server;
                    return NGX_OK;
                }

                ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                              ngx_regex_exec_n " failed: %i "
                              "on \"%V\" using \"%V\"",
                              n, host, &sn[i].regex->name);

                return NGX_ERROR;
            }

            return NGX_DECLINED;
        }

#endif /* NGX_HTTP_SSL && defined SSL_CTRL_SET_TLSEXT_HOSTNAME */

        for (i = 0; i < virtual_names->nregex; i++) {

            n = ngx_http_regex_exec(r, sn[i].regex, host);

            if (n == NGX_DECLINED) {
                continue;
            }

            if (n == NGX_OK) {
                *cscfp = sn[i].server;
                return NGX_OK;
            }

            return NGX_ERROR;
        }
    }

#endif /* NGX_PCRE */

    return NGX_DECLINED;
}

/*
ngx_http_process_request方法负责在接收完HTTP头部后，第一次与各个HTTP模块共同按阶段处理请求，而对于ngx_http_request_handler方法，
如果ngx_http_process_request没能处理完请求，这个请求上的事件再次被触发，那就将由此方法继续处理了。

当解析到完整的头部行和请求行后，不会再需要接收HTTP请求行或者头部，所以需要重新设置当前连接读/写事件的回调方法。将同时把读事件、写事件的回调
方法都设置为ngx_http_request_handler方法，请求的后续处理都是通过ngx_http_request_handler方法进行的。

HTTP框架无论是调用ngx_http_process_request方法（首次从业务上处理请求）还是ngx_http_request_handler方法（TCP连接上后续的事件触发时）处理
请求，最后都有一个步骤，就是调用ngx_http_run_posted_requests方法处理post请求
*/ 
//客户端事件处理handler一般(write(read)->handler)一般为ngx_http_request_handler， 和后端的handler一般(write(read)->handler)一般为ngx_http_upstream_handler
static void
ngx_http_request_handler(ngx_event_t *ev)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;

/*
ngx_http_request_handler是HTTP请求上读/写事件的回调方法。在ngx_event_t结构体表示的事件中，data成员指向了这个事件对应的ngx_connection_t连接，
在HTTP框架的ngx_connection_t结构体中的data成员则指向了ngx_http_request_t结构体
*/
    c = ev->data;
    r = c->data;

    ngx_http_set_log_request(c->log, r);

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http run request(ev->write:%d): \"%V?%V\"", ev->write, &r->uri, &r->args);

/*
 检查这个事件的write可写标志，如果write标志为l，则调用ngx_http_request_t结构体中的write event- handler方法。注意，我们在ngx_http_handler
 方法中已经将write_event_handler设置为ngx_http_core_run_phases方法，而一般我们开发的不太复杂的HTTP模块是不会重新设置write_event_handler方
 法的，因此，一旦有可写事件时，就会继续按照流程执行ngx_http_core_run_phases方法，并继续按阶段调用各个HTTP模块实现的方法处理请求。

如果一个事件的读写标志同时为1时，仅write_event_handler方法会被调用，即可写事件的处理优先于可读事件（这正是Nginx高性能设计的体现，
优先处理可写事件可以尽快释放内存，尽量保持各HTTP模块少使用内存以提高并发能力）。因为服务器发送给客户端的报文长度一般比请求报文大很多
 */
   //当ev为ngx_connection_t->write 默认write为1；当ev为ngx_connection_t->read 默认write为0
    if (ev->write) { //说明ev是ngx_connection_t->write
        r->write_event_handler(r); //ngx_http_core_run_phases

    } else {//说明ev是ngx_connection_t->read事件 
        r->read_event_handler(r);
    }

/*
HTTP框架无论是调用ngx_http_process_request方法（首次从业务上处理请求）还是ngx_http_request_handler方法（TCP连接上后续的事件触发时）处理
请求，最后都有一个步骤，就是调用ngx_http_run_posted_requests方法处理post请求
*/
/* ngx_http_process_request和ngx_http_request_handler这两个方法的共通之处在于，它们都会先按阶段调用各个HTTP模块处理请求，再处理post请求 */
    ngx_http_run_posted_requests(c);
}



/*
    sub1_r和sub2_r都是同一个父请求，就是root_r请求，sub1_r和sub2_r就是ngx_http_postponed_request_s->request成员
    它们由ngx_http_postponed_request_s->next连接在一起，参考ngx_http_subrequest
    
        
                          -----root_r(主请求)     
                          |postponed
                          |                next
            -------------sub1_r(data1)--------------sub2_r(data1)
            |                                       |postponed                    
            |postponed                              |
            |                                     sub21_r-----sub22
            |
            |               next
          sub11_r(data11)-----------sub12_r(data12)

    图中的root节点即为主请求，它的postponed链表从左至右挂载了3个节点，SUB1是它的第一个子请求，DATA1是它产生的一段数据，SUB2是它的第2个子请求，
而且这2个子请求分别有它们自己的子请求及数据。ngx_connection_t中的data字段保存的是当前可以往out chain发送数据的请求，文章开头说到发到客户端
的数据必须按照子请求创建的顺序发送，这里即是按后续遍历的方法（SUB11->DATA11->SUB12->DATA12->(SUB1)->DATA1->SUB21->SUB22->(SUB2)->(ROOT)），
上图中当前能够往客户端（out chain）发送数据的请求显然就是SUB11，如果SUB12提前执行完成，并产生数据DATA121，只要前面它还有节点未发送完毕，
DATA121只能先挂载在SUB12的postponed链表下。这里还要注意一下的是c->data的设置，当SUB11执行完并且发送完数据之后，下一个将要发送的节点应该是
DATA11，但是该节点实际上保存的是数据，而不是子请求，所以c->data这时应该指向的是拥有改数据节点的SUB1请求。

发送数据到客户端优先级:
1.子请求优先级比父请求高
2.同级(一个r产生多个子请求)请求，从左到右优先级由高到低(因为先创建的子请求先发送数据到客户端)
发送数据到客户端顺序控制见ngx_http_postpone_filter   nginx通过子请求发送数据到后端见ngx_http_run_posted_requests
*/

//subrequest注意ngx_http_run_posted_requests与ngx_http_subrequest ngx_http_postpone_filter ngx_http_finalize_request配合阅读

/*
HTTP框架无论是调用ngx_http_process_request方法（首次从业务上处理请求）还是ngx_http_request_handler方法（TCP连接上后续的事件触发时）处理
请求，最后都有一个步骤，就是调用ngx_http_run_posted_requests方法处理post请求

ngx_http_run_posted_requests函数又是在什么时候调用？它实际上是在某个请求的读（写）事件的handler中，执行完该请求相关的处理后被调用，
比如主请求在走完一遍PHASE的时候会调用ngx_http_run_posted_requests，这时子请求得以运行。

11个阶段执行完毕后，调用ngx_http_run_posted_requests方法执行post请求，这里一般都是对subrequest进行处理
*/
/* 
ngx_http_upstream_handler和ngx_http_process_request都会执行该函数
*/
void //ngx_http_post_request将该子请求挂载在主请求的posted_requests链表队尾，在ngx_http_run_posted_requests中执行
ngx_http_run_posted_requests(ngx_connection_t *c) //执行r->main->posted_requests链表中所有节点的->write_event_handler()
{ //subrequest注意ngx_http_run_posted_requests与ngx_http_postpone_filter ngx_http_finalize_request配合阅读
    ngx_http_request_t         *r;
    ngx_http_posted_request_t  *pr;

    /*
    如果优先级低的子请求的数据先到达，则先通过ngx_http_postpone_filter->ngx_http_postpone_filter_add缓存到r->postpone，
    然后r添加到pr->request->posted_requests,最后在高优先级请求后端数据到来后，会把之前缓存起来的低优先级请求的数据也一
    起在ngx_http_run_posted_requests中触发发送，从而保证真正发送到客户端数据时按照子请求优先级顺序发送的
    */
    for ( ;; ) {/* 遍历该客户端请求r所对应的所有subrequest，然后把这些subrequest请求做重定向处理 */ 

        /* 首先检查连接是否已销毁，如果连接被销毁，就结束ngx_http_run_posted_requests方法 */
        if (c->destroyed) {
            return;
        }

        r = c->data;
        pr = r->main->posted_requests;

        /*
        根据ngx_http_request_t结构体中的main成员找到原始客户端请求，这个原始请求的posted_requests成员指向待处理的post请求组成的单链表，
        如果posted_requests指向NULL空指针，则结束ngx_http_run_posted_requests方法，否则取出链表中首个指向post请求的指针
          */
        if (pr == NULL) { //请求r没有子请求
            return;
        }

       /*
        将原始请求的posted_requests指针指向链表中下一个post请求（通过第1个post请求的next指针可以获得），当然，下一个post请求有可能不存在，
        这在下一次循环中就会检测到。
        */
        r->main->posted_requests = pr->next;

        r = pr->request; /* 获取子请求的r */

        ngx_http_set_log_request(c->log, r);

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                       "http posted request: \"%V?%V\"", &r->uri, &r->args);

          /*
          调用这个post请求ngx_http_request_t结构体中的write event handler方法。为什么不是执行read_ event_ handler方法呢？原因
          很简单，子请求不是被网络事件驱动的，因此，执行post请求时就相当于有可写事件，由Nginx主动做出动作。

          一般子请求的write_event_handler在ngx_http_set_write_handler中设置为ngx_http_writer
          */ 

        /* r已经是子请求的r了，也就是ngx_http_subrequest中创建的r,执行子请求的write_event_handler，ngx_http_handler，这
        里面会对子请求r做重定向 */
        r->write_event_handler(r); 
    }
}

//把pr添加到r->main->posted_requests尾部
ngx_int_t //ngx_http_post_request将该子请求挂载在主请求的posted_requests链表队尾，在ngx_http_run_posted_requests中执行
ngx_http_post_request(ngx_http_request_t *r, ngx_http_posted_request_t *pr)
{ //注意是把新创建的ngx_http_posted_request_t添加到最上层r的posted_requests中(例如现在是第四层r，则最上层是第一次r，而不是第三层r)
    ngx_http_posted_request_t  **p;

    if (pr == NULL) {
        pr = ngx_palloc(r->pool, sizeof(ngx_http_posted_request_t));
        if (pr == NULL) {
            return NGX_ERROR;
        }
    }

    pr->request = r;
    pr->next = NULL;

    for (p = &r->main->posted_requests; *p; p = &(*p)->next) { /* void */ }

    *p = pr;

    return NGX_OK;
}


/*
    对于事件驱动的架构来说，结束请求是一项复杂的工作。因为一个请求可能会被许多个事件触发，这使得Nginx框架调度到某个请求的回调方法
时，在当前业务内似乎需要结束HTTP请求，但如果真的结束了请求，销毁了与请求相关的内存，多半会造成重大错误，因为这个请求可能还有其
他事件在定时器或者epoll中。当这些事件被回调时，请求却已经不存在了，这就是严重的内存访问越界错误！如果尝试在属于某个HTTP模块的
回调方法中试图结束请求，先要把这个请求相关的所有事件（有些事件可能属于其他HTTP模块）都从定时器和epoll中取出并调用其handler方法，
这又太复杂了，另外，不同HTTP模块上的代码耦合太紧密将会难以维护。
    那HTTP框架又是怎样解决这个问题的呢？HTTP框架把一个请求分为多种动作，如果HTTP框架提供的方法会导致Nginx再次调度到请求（例如，
在这个方法中产生了新的事件，或者重新将已有事件添加到epoll或者定时器中），那么可以认为这一步调用是一种独立的动作。例如，接收HTTP
请求的包体、调用upstream机制提供的方法访问第三方服务、派生出subrequest子请求等。这些所谓独立的动作，都是在告诉Nginx，如果机会合
适就再次调用它们处理请求，因为这个动作并不是Nginx调用一次它们的方法就可以处理完毕的。因此，每一种动作对于整个请求来说都是独立的，
HTTP框架希望每个动作结束时仅维护自己的业务，不用去关心这个请求是否还做了其他动作。这种设计大大降低了复杂度。
    这种设计具体又是怎么实现的呢？每个HTTP请求都有一个引用计数，每派生出一种新的会独立向事件收集器注册事件的动作时（如ngx_http_
read_ client_request_body方法或者ngx_http_subrequest方法），都会把引用计数加1，这样每个动作结束时都通过调用ngx_http_finalize_request方法
来结束请求，而ngx_http_finalize_request方法实际上却会在引用计数减1后先检查引用计数的值，如果不为O是不会真正销毁请求的。
*/ 


//ngx_http_finalize_request -> ngx_http_finalize_connection ,注意和ngx_http_terminate_request的区别
void
ngx_http_finalize_request(ngx_http_request_t *r, ngx_int_t rc) 
{//subrequest注意ngx_http_run_posted_requests与ngx_http_postpone_filter ngx_http_finalize_request配合阅读
    ngx_connection_t          *c;
    ngx_http_request_t        *pr;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;

    ngx_log_debug7(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http finalize request rc: %d, \"%V?%V\" a:%d, c:%d, b:%d, p:%p",
                   rc, &r->uri, &r->args, r == c->data, r->main->count, (int)r->buffered, r->postponed);

    /*
    NGX_DONE参数表示不需要做任何事，直接调用ngx_http_finalize_connection方法，之后ngx_http_finalize_request方法结束。当某一种动作
（如接收HTTP请求包体）正常结束而请求还有业务要继续处理时，多半都是传递NGX_DONE参数。这个ngx_http_finalize_connection方法还会去检
查引用计数情况，并不一定会销毁请求。
     */
    if (rc == NGX_DONE) {
        ngx_http_finalize_connection(r);  
        return;
    }

    if (rc == NGX_OK && r->filter_finalize) {
        c->error = 1;
    }

    /*
   NGX_DECLINED参数表示请求还需要按照11个HTTP阶段继续处理下去，这时需要继续调用ngx_http_core_run_phases方法处理请求。这
一步中首先会把ngx_http_request_t结构体的write—event handler设为ngx_http_core_run_phases方法。同时，将请求的content_handler成员
置为NULL空指针，它是一种用于在NGX_HTTP_CONTENT_PHASE阶段处理请求的方式，将其设置为NULL足为了让ngx_http_core_content_phase方法
可以继续调用NGX_HTTP_CONTENT_PHASE阶段的其他处理方法。
     */
    if (rc == NGX_DECLINED) {
        r->content_handler = NULL;
        r->write_event_handler = ngx_http_core_run_phases;
        ngx_http_core_run_phases(r);
        return;
    }

    /*
    检查当前请求是否为subrequest子请求，如果是子请求，那么调用post_subrequest下的handler回调方法。subrequest的用法，可以看
    到post_subrequest正是此时被调用的。
     */  /* 如果当前请求是一个子请求，检查它是否有回调handler，有的话执行之 */  
    if (r != r->main && r->post_subrequest) {//如果当前请求属于某个原始请求的子请求
        rc = r->post_subrequest->handler(r, r->post_subrequest->data, rc); //r变量是子请求（不是父请求）
    }

    if (rc == NGX_ERROR
        || rc == NGX_HTTP_REQUEST_TIME_OUT
        || rc == NGX_HTTP_CLIENT_CLOSED_REQUEST
        || c->error)
    {
        //直接调用ngx_http_terminate_request方法强制结束请求，同时，ngx_http_finalize_request方法结束。
        if (ngx_http_post_action(r) == NGX_OK) {
            return;
        }

        if (r->main->blocked) {
            r->write_event_handler = ngx_http_request_finalizer;
        }

        ngx_http_terminate_request(r, rc);
        return;
    }

    /*
    如果rc为NGX_HTTP_NO_CONTENT、NGX_HTTP_CREATED或者大于或等于NGX_HTTP_SPECIAL_RESPONSE，则表示请求的动作是上传文件，
    或者HTTP模块需要HTTP框架构造并发送响应码大于或等于300以上的特殊响应
     */
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE
        || rc == NGX_HTTP_CREATED
        || rc == NGX_HTTP_NO_CONTENT)
    {
        if (rc == NGX_HTTP_CLOSE) {
            ngx_http_terminate_request(r, rc);
            return;
        }

        /*
            检查当前请求的main是否指向自己，如果是，这个请求就是来自客户端的原始请求（非子请求），这时检查读/写事件的timer_set标志位，
            如果timer_set为1，则表明事件在定时器申，需要调用ngx_del_timer方法把读/写事件从定时器中移除。
          */
        if (r == r->main) {
            if (c->read->timer_set) {
                ngx_del_timer(c->read, NGX_FUNC_LINE);
            }

            if (c->write->timer_set) {
                ngx_del_timer(c->write, NGX_FUNC_LINE);
            }
        }

        /* 设置读／写事件的回调方法为ngx_http_request_handler方法，这个方法，它会继续处理HTTP请求。 */
        c->read->handler = ngx_http_request_handler;
        c->write->handler = ngx_http_request_handler;

    /*
      调用ngx_http_special_response_handler方法，该方法负责根据rc参数构造完整的HTTP响应。为什么可以在这一步中构造这样的响应呢？
      这时rc要么是表示上传成功的201或者204，要么就是表示异步的300以上的响应码，对于这些情况，都是可以让HTTP框架独立构造响应包的。
      */
        ngx_http_finalize_request(r, ngx_http_special_response_handler(r, rc));
        return;
    }

    if (r != r->main) { //子请求
         /* 该子请求还有未处理完的数据或者子请求 */  
        if (r->buffered || r->postponed) { //检查out缓冲区内是否还有没发送完的响应
             /* 添加一个该子请求的写事件，并设置合适的write event hander， 
               以便下次写事件来的时候继续处理，这里实际上下次执行时会调用ngx_http_output_filter函数， 
               最终还是会进入ngx_http_postpone_filter进行处理，在该函数中不一定会把数据发送出去，而是挂接到postpone链上，等高优先级的子请求先发送 */ 
            if (ngx_http_set_write_handler(r) != NGX_OK) { 
                ngx_http_terminate_request(r, 0);
            }

            return;
        }

        /*
            由于当前请求是子请求，那么正常情况下需要跳到它的父请求上，激活父请求继续向下执行，所以这一步首先根据ngx_http_request_t结
        构体的parent成员找到父请求，再构造一个ngx_http_posted_request_t结构体把父请求放置其中，最后把该结构体添加到原始请求的
        posted_requests链表中，这样ngx_http_run_posted_requests方法就会调用父请求的write_event_handler方法了。
        */
        pr = r->parent;

        /*
          sub1_r和sub2_r都是同一个父请求，就是root_r请求，sub1_r和sub2_r就是ngx_http_postponed_request_s->request成员
          它们由ngx_http_postponed_request_s->next连接在一起，参考ngx_http_subrequest

                          -----root_r(主请求)     
                          |postponed
                          |                next
            -------------sub1_r(data1)--------------sub2_r(data1)
            |                                       |postponed                    
            |postponed                              |
            |                                     sub21_r-----sub22
            |
            |               next
          sub11_r(data11)-----------sub12_r(data12)

     */
        if (r == c->data) { 
        //这个优先级最高的子请求数据发送完毕了，则直接从pr->postponed中摘除，例如这次摘除的是sub11_r，则下个优先级最高发送客户端数据的是sub12_r

            r->main->count--; /* 在上面的rc = r->post_subrequest->handler()已经处理好了该子请求，则减1 */
            r->main->subrequests++;

            if (!r->logged) {

                clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

                if (clcf->log_subrequest) {
                    ngx_http_log_request(r);
                }

                r->logged = 1;

            } else {
                ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                              "subrequest: \"%V?%V\" logged again",
                              &r->uri, &r->args);
            }

            r->done = 1; /* 该子请求的handler已经处理完毕 */
             /* 如果该子请求不是提前完成，则从父请求的postponed链表中删除 */  
            if (pr->postponed && pr->postponed->request == r) {
                pr->postponed = pr->postponed->next;
            }

            /* 将发送权利移交给父请求，父请求下次执行的时候会发送它的postponed链表中可以 
               发送的数据节点，或者将发送权利移交给它的下一个子请求 */ 
            c->data = pr;
        } else {

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http finalize non-active request: \"%V?%V\"",
                           &r->uri, &r->args);
            /* 到这里其实表明该子请求提前执行完成，而且它没有产生任何数据，则它下次再次获得 
               执行机会时，将会执行ngx_http_request_finalzier函数，它实际上是执行 
               ngx_http_finalize_request（r,0），也就是什么都不干，直到轮到它发送数据时， 
               ngx_http_finalize_request 函数会将它从父请求的postponed链表中删除 */  
            r->write_event_handler = ngx_http_request_finalizer; //也就是优先级低的请求比优先级高的请求先得到后端返回的数据，

            if (r->waited) {
                r->done = 1;
            }
        }

         /* 将父请求加入posted_request队尾，获得一次运行机会，这样pr就会加入到posted_requests，
         在ngx_http_run_posted_requests中就可以调用pr->ngx_http_run_posted_requests */  
        if (ngx_http_post_request(pr, NULL) != NGX_OK) {
            r->main->count++;
            ngx_http_terminate_request(r, 0);
            return;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                       "http wake parent request: \"%V?%V\"",
                       &pr->uri, &pr->args);

        return;
    }

    /* 这里是处理主请求结束的逻辑，如果主请求有未发送的数据或者未处理的子请求， 则给主请求添加写事件，并设置合适的write event hander， 
       以便下次写事件来的时候继续处理 */  
       
    //ngx_http_request_t->out中还有未发送的包体，
    //ngx_http_finalize_request->ngx_http_set_write_handler->ngx_http_writer通过这种方式把未发送完毕的响应报文发送出去
    if (r->buffered || c->buffered || r->postponed || r->blocked) { //例如还有未发送的数据，见ngx_http_copy_filter，则buffered不为0

        if (ngx_http_set_write_handler(r) != NGX_OK) { 
            ngx_http_terminate_request(r, 0);
        }

        return;
    }

    if (r != c->data) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "http finalize non-active request: \"%V?%V\"",
                      &r->uri, &r->args);
        return;
    }

    r->done = 1;
    r->write_event_handler = ngx_http_request_empty_handler;

    if (!r->post_action) {
        r->request_complete = 1;
    }

    if (ngx_http_post_action(r) == NGX_OK) {
        return;
    }

    /* 到了这里真的要结束请求了。首先判断读／写事件的timer-set标志位，如果timer-set为1，则需要把相应的读/写事件从定时器中移除 */

    if (c->read->timer_set) {
        ngx_del_timer(c->read, NGX_FUNC_LINE);
    }

    if (c->write->timer_set) {
        c->write->delayed = 0;
        //这里的定时器一般在ngx_http_set_write_handler->ngx_add_timer中添加的
        ngx_del_timer(c->write, NGX_FUNC_LINE);
    }

    if (c->read->eof) {
        ngx_http_close_request(r, 0);
        return;
    }

    ngx_http_finalize_connection(r);
}

/*
ngx_http_terminate_request方法是提供给HTTP模块使用的结束请求方法，但它属于非正常结束的场景，可以理解为强制关闭请求。也就是说，
当调用ngx_http_terminate_request方法结束请求时，它会直接找出该请求的main成员指向的原始请求，并直接将该原始请求的引用计数置为1，
同时会调用ngx_http_close_request方法去关闭请求
*/ //ngx_http_finalize_request -> ngx_http_finalize_connection ,注意和ngx_http_terminate_request的区别
static void
ngx_http_terminate_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_http_cleanup_t    *cln;
    ngx_http_request_t    *mr;
    ngx_http_ephemeral_t  *e;

    mr = r->main;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http terminate request count:%d", mr->count);

    if (rc > 0 && (mr->headers_out.status == 0 || mr->connection->sent == 0)) {
        mr->headers_out.status = rc;
    }

    cln = mr->cleanup;
    mr->cleanup = NULL;

    while (cln) {
        if (cln->handler) {
            cln->handler(cln->data);
        }

        cln = cln->next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http terminate cleanup count:%d blk:%d",
                   mr->count, mr->blocked);

    if (mr->write_event_handler) {

        if (mr->blocked) {
            return;
        }

        e = ngx_http_ephemeral(mr);
        mr->posted_requests = NULL;
        mr->write_event_handler = ngx_http_terminate_handler;
        (void) ngx_http_post_request(mr, &e->terminal_posted_request);
        return;
    }

    ngx_http_close_request(mr, rc);
}


static void
ngx_http_terminate_handler(ngx_http_request_t *r)
{
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http terminate handler count:%d", r->count);

    r->count = 1;

    ngx_http_close_request(r, 0);
}

/*
ngx_http_finalize_connection方法虽然比ngx_http_close_request方法高了一个层次，但HTTP模块一般还是不会直接调用它。
ngx_http_finalize_connection方法在结束请求时，解决了keepalive特性和子请求的问题   ngx_http_finalize_request -> ngx_http_finalize_connection ,注意和ngx_http_terminate_request的区别
*/ 

//该函数用于判断是理解关闭连接，还是通过保活超时关闭连接，还是延迟关闭连接
static void
ngx_http_finalize_connection(ngx_http_request_t *r) //ngx_http_finalize_request->ngx_http_finalize_connection
{
    ngx_http_core_loc_conf_t  *clcf;

#if (NGX_HTTP_V2)
    if (r->stream) {
        ngx_http_close_request(r, 0);
        return;
    }
#endif

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    /*
    查看原始请求的引用计数．如果不等于1，则表示还有多个动作在操作着请求，接着继续检查discard_body标志位。如果
discard_body为l，则表示正在丢弃包体，这时会再一次把请求的read_event_handler成员设为ngx_http_discarded_request_body_handler方法，
     */
    if (r->main->count != 1) {

        if (r->discard_body) {
            r->read_event_handler = ngx_http_discarded_request_body_handler;
            //将读事件添加到定时器中，其中超时时间是lingering_timeout配置项。
            ngx_add_timer(r->connection->read, clcf->lingering_timeout, NGX_FUNC_LINE);

            if (r->lingering_time == 0) {
                r->lingering_time = ngx_time()
                                      + (time_t) (clcf->lingering_time / 1000);
            }
        }

        ngx_http_close_request(r, 0);
        return;
    }

    if (r->reading_body) {
        r->keepalive = 0; //使用延迟关闭连接功能，就不需要再判断keepalive功能关连接了
        r->lingering_close = 1;
    }

    /*
    如果引用计数为1，则说明这时要真的准备结束请求了。不过，还要检查请求的keepalive成员，如果keepalive为1，则说明这个请求需要释放，
但TCP连接还是要复用的；如果keepalive为0就不需要考虑keepalive请求了，但还需要检测请求的lingering_close成员，如果lingering_close为1，
则说明需要延迟关闭请求，这时也不能真的去结束请求，如果lingering_close为0，才真的结束请求。
     */
    if (!ngx_terminate
         && !ngx_exiting
         && r->keepalive
         && clcf->keepalive_timeout > 0) 
         //如果客户端请求携带的报文头中设置了长连接，并且我们的keepalive_timeout配置项大于0(默认75s),则不能关闭连接，只有等这个时间到后还没有数据到来，才关闭连接
    {
        ngx_http_set_keepalive(r); 
        return;
    }

    if (clcf->lingering_close == NGX_HTTP_LINGERING_ALWAYS
        || (clcf->lingering_close == NGX_HTTP_LINGERING_ON
            && (r->lingering_close
                || r->header_in->pos < r->header_in->last
                || r->connection->read->ready)))
    {
       /*
        调用ngx_http_set_lingering_close方法延迟关闭请求。实际上，这个方法的意义就在于把一些必须做的事情做完
        （如接收用户端发来的字符流）再关闭连接。
        */
        ngx_http_set_lingering_close(r);
        return;
    }

    ngx_http_close_request(r, 0);
}

//调用ngx_http_write_filter写数据，如果返回NGX_AGAIN,则以后的写数据触发通过在ngx_http_set_write_handler->ngx_http_writer添加epoll write事件来触发
static ngx_int_t
ngx_http_set_write_handler(ngx_http_request_t *r)
{
    ngx_event_t               *wev;
    ngx_http_core_loc_conf_t  *clcf;

    r->http_state = NGX_HTTP_WRITING_REQUEST_STATE;

    r->read_event_handler = r->discard_body ?
                                ngx_http_discarded_request_body_handler:
                                ngx_http_test_reading;
    r->write_event_handler = ngx_http_writer;

#if (NGX_HTTP_V2)
    if (r->stream) {
        return NGX_OK;
    }
#endif

    wev = r->connection->write;

    if (wev->ready && wev->delayed) {
        return NGX_OK;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (!wev->delayed) { //本次ngx_http_write_filter没有把数据发送完毕，则需要添加写事件定时器
    /*
    因为超时的原因执行了该write_event_handler(例如在向客户端发送数据的过程中，客户端一直不recv，就会造成内核缓存区满，
    数据永远发送不出去，于是就在ngx_http_set_write_handler中添加了写事件定时器)，从而可以检查是否写超时，从而可以关闭连接
     */

     /*
        当数据全部发送到客户端后，在ngx_http_finalize_request中删除
        if (c->write->timer_set) {
            c->write->delayed = 0;
            //这里的定时器一般在ngx_http_set_write_handler->ngx_add_timer中添加的
            ngx_del_timer(c->write, NGX_FUNC_LINE);
        }
       */
        ngx_add_timer(wev, clcf->send_timeout, NGX_FUNC_LINE);
    }

    if (ngx_handle_write_event(wev, clcf->send_lowat, NGX_FUNC_LINE) != NGX_OK) {
        ngx_http_close_request(r, 0);
        return NGX_ERROR;
    }

    return NGX_OK;
}

//ngx_http_finalize_request->ngx_http_set_write_handler->ngx_http_writer通过这种方式把未发送完毕的响应报文发送出去
/*
ngx_http_writer方法对各个HTTP模块而言是不可见的，但实际上它非常重要，因为无论是ngx_http_send_header还是
ngx_http_output_filter方法，它们在调用时一般都无法发送全部的响应，剩下的响应内容都得靠ngx_http_writer方法来发送
*/ //ngx_http_writer方法仅用于在后台发送响应到客户端。
//调用ngx_http_write_filter写数据，如果返回NGX_AGAIN,则以后的写数据触发通过在ngx_http_set_write_handler->ngx_http_writer添加epoll write事件来触发
static void
ngx_http_writer(ngx_http_request_t *r)
{
    int                        rc;
    ngx_event_t               *wev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    wev = c->write;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, wev->log, 0,
                   "http writer handler: \"%V?%V\"", &r->uri, &r->args);

    clcf = ngx_http_get_module_loc_conf(r->main, ngx_http_core_module);

/*
    检查连接上写事件的timedout标志位，如果timedout为0，则表示写事件未超时；如果timedout为1，则表示当前的写事件已经超时，这时
有两种可能性：
第一种，由于网络异常或者客户端长时间不接收响应，导致真实的发送响应超时；
第二种，由于上一次发送响应时发送速率过快，超过了请求的limit_rate速率上限，ngx_http_write_filter方法就会设置一个超时
时间将写事件添加到定时器中，这时本次的超时只是由限速导致，并非真正超时。那么，如何判断这个超时是真的超时还是出于限速的考虑呢？这
要看事件的delayed标志位。如果是限速把写事件加入定时器，一定会把delayed标志位置为1，如果写事件的delayed标志位为0，那就是真的超时
了，这时调用ngx_http_finalize_request方法结束请求，传人的参数是NGX_HTTP_REQUEST_TIME_OUT，表示需要向客户端发送408错误码；
 */
    if (wev->timedout) { 
    /*
    因为超时的原因执行了该write_event_handler(例如在向客户端发送数据的过程中，客户端一直不recv，就会造成内核缓存区满，
    数据永远发送不出去，于是就在ngx_http_set_write_handler中添加了写事件定时器)，从而可以检查是否写超时，从而可以关闭连接
     */
        if (!wev->delayed) { //由于网络异常或者客户端长时间不接收响应，导致真实的发送响应超时；
            ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT,
                          "client timed out");
            c->timedout = 1;

            ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
            return;
        }

        //limit_rate速率引起的超时，见ngx_http_write_filter，这属于正常情况
        wev->timedout = 0;
        wev->delayed = 0;

        /*
          再检查写事件的ready标志位，如果为1，则表示在与客户端的TCP连接上可以发送数据；如果为0，则表示暂不可发送数据。
          */
        if (!wev->ready) {
            //将写事件添加到定时器中，这里的超时时间就是配置文件中的send_timeout参数，与限速功能无关。
            ngx_add_timer(wev, clcf->send_timeout, NGX_FUNC_LINE);

            if (ngx_handle_write_event(wev, clcf->send_lowat, NGX_FUNC_LINE) != NGX_OK) {
                ngx_http_close_request(r, 0);
            }

            return;
        }

    }

    if (wev->delayed || r->aio) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0,
                       "http writer delayed");

        if (ngx_handle_write_event(wev, clcf->send_lowat, NGX_FUNC_LINE) != NGX_OK) {
            ngx_http_close_request(r, 0);
        }

        return;
    }

    /*
    调用ngx_http_output_filter方法发送响应，其中第2个参数（也就是表示需要发送的缓冲区）为NULL指针。这意味着，需要调用各包体过
    滤模块处理out缓冲区中的剩余内容，最后调用ngx_http_write filter方法把响应发送出去。
     */
    rc = ngx_http_output_filter(r, NULL);//NULL表示这次没有心的数据加入到out中，直接把上次没有发送完的out发送出去即可

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http writer output filter: %d, \"%V?%V\"",
                   rc, &r->uri, &r->args);

    if (rc == NGX_ERROR) {
        ngx_http_finalize_request(r, rc);
        return;
    }

/*
发送响应后，查看ngx_http_request_t结构体中的buffered和postponed标志位，如果任一个不为0，则意味着没有发送完out中的全部响应，
请求main指针指向请求自身，表示这个请求是原始请求，再检查与客户端间的连接ngx_connection-t结构体中的buffered标志位，如果buffered
不为0，同样表示没有发送完out中的全部响应；除此以外，都表示out中的全部响应皆发送完毕。
 */
    if (r->buffered || r->postponed || (r == r->main && c->buffered)) {

#if (NGX_HTTP_V2)
        if (r->stream) {
            return;
        }
#endif

        if (!wev->delayed) {
            ngx_add_timer(wev, clcf->send_timeout, NGX_FUNC_LINE);
        }

        if (ngx_handle_write_event(wev, clcf->send_lowat, NGX_FUNC_LINE) != NGX_OK) {
            ngx_http_close_request(r, 0);
        }

        return;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, wev->log, 0,
                   "http writer done: \"%V?%V\"", &r->uri, &r->args);

/*
将请求的write_event_handler方法置为ngx_http_request_empty_handler，也就是说，如果这个请求的连接上再有可写事件，将不做任何处理。
 */
    r->write_event_handler = ngx_http_request_empty_handler;

    ngx_http_finalize_request(r, rc);
}


static void
ngx_http_request_finalizer(ngx_http_request_t *r)
{
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http finalizer done: \"%V?%V\"", &r->uri, &r->args);

    ngx_http_finalize_request(r, 0);
}

/*
把读事件从epoll中移除。只对epoll lt模式其作用它的意义在于，目前已经开始处理HTTP请求，除非某个HTTP模块重新设置了read_event_handler方法，
否则任何读事件都将得不到处理，也可似认为读事件被阻 塞了。

注意这里面会调用ngx_del_event，因此如果需要继续读取客户端请求内容，需要加上ngx_add_event，例如可以参考下ngx_http_discard_request_body
*/
void
ngx_http_block_reading(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http reading blocked");

    /* aio does not call this handler */

    if ((ngx_event_flags & NGX_USE_LEVEL_EVENT)
        && r->connection->read->active)
    {
        if (ngx_del_event(r->connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
            ngx_http_close_request(r, 0);
        }
    }
}

//象征性的读1个字节，其实啥也没干
void ngx_http_test_reading(ngx_http_request_t *r)
{
    int                n;
    char               buf[1];
    ngx_err_t          err;
    ngx_event_t       *rev;
    ngx_connection_t  *c;

    c = r->connection;
    rev = c->read;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http test reading");

#if (NGX_HTTP_V2)

    if (r->stream) {
        if (c->error) {
            err = 0;
            goto closed;
        }

        return;
    }

#endif

#if (NGX_HAVE_KQUEUE)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {

        if (!rev->pending_eof) {
            return;
        }

        rev->eof = 1;
        c->error = 1;
        err = rev->kq_errno;

        goto closed;
    }

#endif

#if (NGX_HAVE_EPOLLRDHUP)

    if ((ngx_event_flags & NGX_USE_EPOLL_EVENT) && rev->pending_eof) {
        socklen_t  len;

        rev->eof = 1;
        c->error = 1;

        err = 0;
        len = sizeof(ngx_err_t);

        /*
         * BSDs and Linux return 0 and set a pending error in err
         * Solaris returns -1 and sets errno
         */

        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len)
            == -1)
        {
            err = ngx_socket_errno;
        }

        goto closed;
    }

#endif

    n = recv(c->fd, buf, 1, MSG_PEEK);

    if (n == 0) {
        rev->eof = 1;
        c->error = 1;
        err = 0;

        goto closed;

    } else if (n == -1) {
        err = ngx_socket_errno;

        if (err != NGX_EAGAIN) {
            rev->eof = 1;
            c->error = 1;

            goto closed;
        }
    }

    /* aio does not call this handler */

    if ((ngx_event_flags & NGX_USE_LEVEL_EVENT) && rev->active) {

        if (ngx_del_event(rev, NGX_READ_EVENT, 0) != NGX_OK) {
            ngx_http_close_request(r, 0);
        }
    }

    return;

closed:

    if (err) {
        rev->error = 1;
    }

    ngx_log_error(NGX_LOG_INFO, c->log, err,
                  "client prematurely closed connection");

    ngx_http_finalize_request(r, NGX_HTTP_CLIENT_CLOSED_REQUEST);
}

/*
ngx_http_set_keepalive方法将当前连接设为keepalive状态。它实际上会把表示请求的ngx_http_request_t结构体释放，却又不会调用
ngx_http_close_connection方法关闭连接，同时也在检测keepalive连接是否超时，对于这个方法，此处不做详细解释
*/
//ngx_http_finalize_request -> ngx_http_finalize_connection ->ngx_http_set_keepalive
static void
ngx_http_set_keepalive(ngx_http_request_t *r)
{
    int                        tcp_nodelay;
    ngx_int_t                  i;
    ngx_buf_t                 *b, *f;
    ngx_event_t               *rev, *wev;
    ngx_connection_t          *c;
    ngx_http_connection_t     *hc;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    rev = c->read;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "set http keepalive handler");

    if (r->discard_body) {
        r->write_event_handler = ngx_http_request_empty_handler;
        r->lingering_time = ngx_time() + (time_t) (clcf->lingering_time / 1000);
        ngx_add_timer(rev, clcf->lingering_timeout, NGX_FUNC_LINE);
        return;
    }

    c->log->action = "closing request";

    hc = r->http_connection;
    b = r->header_in;

    if (b->pos < b->last) {

        /* the pipelined request */

        if (b != c->buffer) {

            /*
             * If the large header buffers were allocated while the previous
             * request processing then we do not use c->buffer for
             * the pipelined request (see ngx_http_create_request()).
             *
             * Now we would move the large header buffers to the free list.
             */

            cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

            if (hc->free == NULL) {
                hc->free = ngx_palloc(c->pool,
                  cscf->large_client_header_buffers.num * sizeof(ngx_buf_t *));

                if (hc->free == NULL) {
                    ngx_http_close_request(r, 0);
                    return;
                }
            }

            for (i = 0; i < hc->nbusy - 1; i++) {
                f = hc->busy[i];
                hc->free[hc->nfree++] = f;
                f->pos = f->start;
                f->last = f->start;
            }

            hc->busy[0] = b;
            hc->nbusy = 1;
        }
    }

    /* guard against recursive call from ngx_http_finalize_connection() */
    r->keepalive = 0;

    ngx_http_free_request(r, 0);

    c->data = hc;

    if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) {
        ngx_http_close_connection(c);
        return;
    }

    wev = c->write;
    wev->handler = ngx_http_empty_handler;

    if (b->pos < b->last) {

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "pipelined request");

        c->log->action = "reading client pipelined request line";

        r = ngx_http_create_request(c);
        if (r == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        r->pipeline = 1;

        c->data = r;

        c->sent = 0;
        c->destroyed = 0;

        if (rev->timer_set) {
            ngx_del_timer(rev, NGX_FUNC_LINE);
        }

        rev->handler = ngx_http_process_request_line;
        ngx_post_event(rev, &ngx_posted_events);
        return;
    }

    /*
     * To keep a memory footprint as small as possible for an idle keepalive
     * connection we try to free c->buffer's memory if it was allocated outside
     * the c->pool.  The large header buffers are always allocated outside the
     * c->pool and are freed too.
     */

    b = c->buffer;

    if (ngx_pfree(c->pool, b->start) == NGX_OK) {

        /*
         * the special note for ngx_http_keepalive_handler() that
         * c->buffer's memory was freed
         */

        b->pos = NULL;

    } else {
        b->pos = b->start;
        b->last = b->start;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "hc free: %p %d",
                   hc->free, hc->nfree);

    if (hc->free) {
        for (i = 0; i < hc->nfree; i++) {
            ngx_pfree(c->pool, hc->free[i]->start);
            hc->free[i] = NULL;
        }

        hc->nfree = 0;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "hc busy: %p %d",
                   hc->busy, hc->nbusy);

    if (hc->busy) {
        for (i = 0; i < hc->nbusy; i++) {
            ngx_pfree(c->pool, hc->busy[i]->start);
            hc->busy[i] = NULL;
        }

        hc->nbusy = 0;
    }

#if (NGX_HTTP_SSL)
    if (c->ssl) {
        ngx_ssl_free_buffer(c);
    }
#endif

    rev->handler = ngx_http_keepalive_handler;

    if (wev->active && (ngx_event_flags & NGX_USE_LEVEL_EVENT)) {
        if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
            ngx_http_close_connection(c);
            return;
        }
    }

    c->log->action = "keepalive";

    if (c->tcp_nopush == NGX_TCP_NOPUSH_SET) {
        if (ngx_tcp_push(c->fd) == -1) {
            ngx_connection_error(c, ngx_socket_errno, ngx_tcp_push_n " failed");
            ngx_http_close_connection(c);
            return;
        }

        c->tcp_nopush = NGX_TCP_NOPUSH_UNSET;
        tcp_nodelay = ngx_tcp_nodelay_and_tcp_nopush ? 1 : 0;

    } else {
        tcp_nodelay = 1;
    }

    if (tcp_nodelay
        && clcf->tcp_nodelay
        && c->tcp_nodelay == NGX_TCP_NODELAY_UNSET)
    {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "tcp_nodelay");

        if (setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY,
                       (const void *) &tcp_nodelay, sizeof(int))
            == -1)
        {
#if (NGX_SOLARIS)
            /* Solaris returns EINVAL if a socket has been shut down */
            c->log_error = NGX_ERROR_IGNORE_EINVAL;
#endif

            ngx_connection_error(c, ngx_socket_errno,
                                 "setsockopt(TCP_NODELAY) failed");

            c->log_error = NGX_ERROR_INFO;
            ngx_http_close_connection(c);
            return;
        }

        c->tcp_nodelay = NGX_TCP_NODELAY_SET;
    }

#if 0
    /* if ngx_http_request_t was freed then we need some other place */
    r->http_state = NGX_HTTP_KEEPALIVE_STATE;
#endif

    c->idle = 1;
    ngx_reusable_connection(c, 1);

    ngx_add_timer(rev, clcf->keepalive_timeout, NGX_FUNC_LINE); //启动保活定时器，时间通过keepalive_timeout设置

    if (rev->ready) {
        ngx_post_event(rev, &ngx_posted_events);
    }
}

//ngx_http_finalize_request -> ngx_http_finalize_connection ->ngx_http_set_keepalive ->ngx_http_keepalive_handler
/*
可以通过两种方式执行到该函数:
1. 保活定时器超时ngx_http_set_keepalive
2. 客户端先断开连接，客户端断开连接的时候epoll_wait() error on fd，然后直接调用该读事件handler
*/
static void
ngx_http_keepalive_handler(ngx_event_t *rev)
{
    size_t             size;
    ssize_t            n;
    ngx_buf_t         *b;
    ngx_connection_t  *c;

    c = rev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http keepalive handler");

    if (rev->timedout || c->close) { //保活超时
        ngx_http_close_connection(c);
        return;
    }

#if (NGX_HAVE_KQUEUE)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        if (rev->pending_eof) {
            c->log->handler = NULL;
            ngx_log_error(NGX_LOG_INFO, c->log, rev->kq_errno,
                          "kevent() reported that client %V closed "
                          "keepalive connection", &c->addr_text);
#if (NGX_HTTP_SSL)
            if (c->ssl) {
                c->ssl->no_send_shutdown = 1;
            }
#endif
            ngx_http_close_connection(c);
            return;
        }
    }

#endif

    b = c->buffer;
    size = b->end - b->start;

    if (b->pos == NULL) {

        /*
         * The c->buffer's memory was freed by ngx_http_set_keepalive().
         * However, the c->buffer->start and c->buffer->end were not changed
         * to keep the buffer size.
         */

        b->pos = ngx_palloc(c->pool, size);
        if (b->pos == NULL) {
            ngx_http_close_connection(c);
            return;
        }

        b->start = b->pos;
        b->last = b->pos;
        b->end = b->pos + size;
    }

    /*
     * MSIE closes a keepalive connection with RST flag
     * so we ignore ECONNRESET here.
     */

    c->log_error = NGX_ERROR_IGNORE_ECONNRESET;
    ngx_set_socket_errno(0);

    n = c->recv(c, b->last, size); //保活超时，同时执行一次读操作，这样就可以先检查对方是否已经关闭了tcp连接， 
    c->log_error = NGX_ERROR_INFO;

    if (n == NGX_AGAIN) {//tcp连接正常，并且没有数据
        if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) {
            ngx_http_close_connection(c);
            return;
        }

        /*
         * Like ngx_http_set_keepalive() we are trying to not hold
         * c->buffer's memory for a keepalive connection.
         */

        if (ngx_pfree(c->pool, b->start) == NGX_OK) {

            /*
             * the special note that c->buffer's memory was freed
             */

            b->pos = NULL;
        }

        return;
    }

    if (n == NGX_ERROR) {
        ngx_http_close_connection(c);
        return;
    }

    c->log->handler = NULL;

    if (n == 0) { //对方已经关闭连接
        ngx_log_error(NGX_LOG_INFO, c->log, ngx_socket_errno,
                      "client %V closed keepalive connection", &c->addr_text);
        ngx_http_close_connection(c);
        return;
    }

    b->last += n;

    c->log->handler = ngx_http_log_error;
    c->log->action = "reading client request line";

    c->idle = 0;
    ngx_reusable_connection(c, 0);

    c->data = ngx_http_create_request(c);
    if (c->data == NULL) {
        ngx_http_close_connection(c);
        return;
    }

    c->sent = 0;
    c->destroyed = 0;

    ngx_del_timer(rev, NGX_FUNC_LINE);

/*
这样的请求行长度是不定的，它与URI长度相关，这意味着在读事件被触发时，内核套接字缓冲区的大小未必足够接收到全部的HTTP请求行，由此可以得出结论：
调用一次ngx_http_process_request_line方法不一定能够做完这项工作。所以，ngx_http_process_request_line方法也会作为读事件的回调方法，它可能会被
epoll这个事件驱动机制多次调度，反复地接收TCP流并使用状态机解析它们，直到确认接收到了完整的HTTP请求行，这个阶段才算完成，才会进入下一个阶段接收HTTP头部。
*/
    rev->handler = ngx_http_process_request_line;
    ngx_http_process_request_line(rev);
}

/*
lingering_close
语法：lingering_close off | on | always;
默认：lingering_close on;
配置块：http、server、location
该配置控制Nginx关闭用户连接的方式。always表示关闭用户连接前必须无条件地处理连接上所有用户发送的数据。off表示关闭连接时完全不管连接
上是否已经有准备就绪的来自用户的数据。on是中间值，一般情况下在关闭连接前都会处理连接上的用户发送的数据，除了有些情况下在业务上认定这之后的数据是不必要的。
*/ //由lingering_close决定
/*
lingering_close存在的意义就是来读取剩下的客户端发来的数据，所以nginx会有一个读超时时间，通过lingering_timeout选项来设置，如果在
lingering_timeout时间内还没有收到数据，则直接关掉连接。nginx还支持设置一个总的读取时间，通过lingering_time来设置，这个时间也就是
nginx在关闭写之后，保留socket的时间，客户端需要在这个时间内发送完所有的数据，否则nginx在这个时间过后，会直接关掉连接。当然，nginx
是支持配置是否打开lingering_close选项的，通过lingering_close选项来配置。 那么，我们在实际应用中，是否应该打开lingering_close呢？这
个就没有固定的推荐值了，如Maxim Dounin所说，lingering_close的主要作用是保持更好的客户端兼容性，但是却需要消耗更多的额外资源（比如
连接会一直占着）。
*/
static void
ngx_http_set_lingering_close(ngx_http_request_t *r)
{
    ngx_event_t               *rev, *wev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    rev = c->read;
    rev->handler = ngx_http_lingering_close_handler;

    r->lingering_time = ngx_time() + (time_t) (clcf->lingering_time / 1000);
    //在准备断开连接的时候，如果设置了lingering_close always，那么在处理完各种事情后，会延迟lingering_timeout这么多时间内，如果客户端还是数据到来，服务器端可以继续读取
    ngx_add_timer(rev, clcf->lingering_timeout, NGX_FUNC_LINE); //rev->handler = ngx_http_lingering_close_handler;

    if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) {
        ngx_http_close_request(r, 0);
        return;
    }

    wev = c->write;
    wev->handler = ngx_http_empty_handler;

    if (wev->active && (ngx_event_flags & NGX_USE_LEVEL_EVENT)) {
        if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) != NGX_OK) {
            ngx_http_close_request(r, 0);
            return;
        }
    }

    if (ngx_shutdown_socket(c->fd, NGX_WRITE_SHUTDOWN) == -1) { //关闭写端，shutdown,但是还可以继续读
        ngx_connection_error(c, ngx_socket_errno,
                             ngx_shutdown_socket_n " failed");
        ngx_http_close_request(r, 0);
        return;
    }

    if (rev->ready) {
        ngx_http_lingering_close_handler(rev);
    }
}


static void
ngx_http_lingering_close_handler(ngx_event_t *rev)
{
    ssize_t                    n;
    ngx_msec_t                 timer;
    ngx_connection_t          *c;
    ngx_http_request_t        *r;
    ngx_http_core_loc_conf_t  *clcf;
    u_char                     buffer[NGX_HTTP_LINGERING_BUFFER_SIZE];

    c = rev->data;
    r = c->data;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http lingering close handler");

    if (rev->timedout) { //该定时器在ngx_http_set_lingering_close设置的
        ngx_http_close_request(r, 0);
        return;
    }

    timer = (ngx_msec_t) r->lingering_time - (ngx_msec_t) ngx_time();
    if ((ngx_msec_int_t) timer <= 0) {
        ngx_http_close_request(r, 0);
        return;
    }

    do {
        n = c->recv(c, buffer, NGX_HTTP_LINGERING_BUFFER_SIZE);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "lingering read: %d", n);

        if (n == NGX_ERROR || n == 0) {
            ngx_http_close_request(r, 0);
            return;
        }

    } while (rev->ready);

    if (ngx_handle_read_event(rev, 0, NGX_FUNC_LINE) != NGX_OK) {
        ngx_http_close_request(r, 0);
        return;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    timer *= 1000;

    if (timer > clcf->lingering_timeout) {
        timer = clcf->lingering_timeout; 
    }

    //如果在lingering_timeout时间内又数据达到，则把重新添加定时器，其时间为timer，因此只要lingering_timeout时间内有数据达到，
    //则连接在lingering_timeout的时候关闭
    ngx_add_timer(rev, timer, NGX_FUNC_LINE); //ngx_http_lingering_close_handler
}

/*
这个方法仅右一个用途：当业务上不需要处理可写事件时，就把ngx_http_empty_handler方法设置到连接的可写事件的handler中，
这样可写事件被定时器或者epoll触发后是不做任何工作的。
*/
void
ngx_http_empty_handler(ngx_event_t *wev)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0, "http empty handler");

    return;
}


void
ngx_http_request_empty_handler(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http request empty handler");

    return;
}

//在发送后端的返回的数据到客户端成功后，会调用该函数，一般在chunk传送方式的时候有效，调用ngx_http_chunked_body_filter标识该chunk传送方式的包体传送接收

//实际上在接受完后端数据后，在想客户端发送包体部分的时候，会两次调用该函数，一次是ngx_event_pipe_write_to_downstream-> p->output_filter(),
//另一次是ngx_http_upstream_finalize_request->ngx_http_send_special,

//网页包体一般进过该函数触发进行发送，头部行在该函数外已经发送出去了，例如ngx_http_upstream_send_response->ngx_http_send_header
ngx_int_t
ngx_http_send_special(ngx_http_request_t *r, ngx_uint_t flags)
{//该函数实际上就是做一件事，就是在要发送出去的chain链末尾添加一个空chain(见ngx_output_chain->ngx_output_chain_add_copy)，当遍历chain发送数据的时候，以此来标记数据发送完毕，这是最后一个chain，

//见ngx_http_write_filter -> if (!last && !flush && in && size < (off_t) clcf->postpone_output) {
    ngx_buf_t    *b;
    ngx_chain_t   out;

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    //见ngx_http_write_filter -> if (!last && !flush && in && size < (off_t) clcf->postpone_output) {
    if (flags & NGX_HTTP_LAST) {

        if (r == r->main && !r->post_action) {//一般进入这个if里面
            b->last_buf = 1; 

        } else {
            b->sync = 1;
            b->last_in_chain = 1;
        }
    }

    if (flags & NGX_HTTP_FLUSH) {
        b->flush = 1;
    }

    out.buf = b;
    out.next = NULL;

    ngx_int_t last_buf = b->last_buf;
    ngx_int_t sync = b->sync;
    ngx_int_t last_in_chain = b->last_in_chain;
    ngx_int_t flush = b->flush;
    
    ngx_log_debugall(r->connection->log, 0, "ngx http send special, flags:%ui, last_buf:%i, sync:%i, last_in_chain:%i, flush:%i",
        flags, last_buf, sync,  last_in_chain, flush);
    return ngx_http_output_filter(r, &out);
}

//
static ngx_int_t
ngx_http_post_action(ngx_http_request_t *r)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->post_action.data == NULL) {//post_action XXX没有配置直接返回
        return NGX_DECLINED;
    }

    if (r->post_action && r->uri_changes == 0) {
        return NGX_DECLINED;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "post action: \"%V\"", &clcf->post_action);

    r->main->count--;

    r->http_version = NGX_HTTP_VERSION_9;
    r->header_only = 1;
    r->post_action = 1;

    r->read_event_handler = ngx_http_block_reading;

    if (clcf->post_action.data[0] == '/') {
        ngx_http_internal_redirect(r, &clcf->post_action, NULL);

    } else {
        ngx_http_named_location(r, &clcf->post_action);
    }

    return NGX_OK;
}

/*
ngx_http_close_request方法是更高层的用于关闭请求的方法，当然，HTTP模块一般也不会直接调用它的。在上面几节中反复提到的引用计数，
就是由ngx_http_close_request方法负责检测的，同时它会在引用计数清零时正式调用ngx_http_free_request方法和ngx_http_close_connection(ngx_close_connection)
方法来释放请求、关闭连接,见ngx_http_close_request,注意和ngx_http_finalize_connection的区别
*/
static void
ngx_http_close_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_connection_t  *c;

    //引用计数一般都作用于这个请求的原始请求上，因此，在结束请求时统一检查原始请求的引用计数就可以了
    r = r->main;
    c = r->connection;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http request count:%d blk:%d", r->count, r->blocked);

    if (r->count == 0) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "http request count is zero");
    }

    r->count--;

    /* 
     在HTTP模块中每进行一类新的操作，包括为一个请求添加新的事件，或者把一些已绎由定时器、epoll中移除的事件重新加入其中，都需要把这个
     请求的引用计数加1。这是因为需要让HTTP框架知道，HTTP模块对于该请求有独立的异步处理机制，将由该HTTP模块决定这个操作什么时候结束，防
     止在这个操作还未结束时HTTP框架却把这个请求销毁了（如其他HTTP模块通过调用ngx_http_finalize_request方法要求HTTP框架结束请求），导致
     请求出现不可知的严重错误。这就要求每个操作在“认为”自身的动作结束时，都得最终调用到ngx_http_close_request方法，该方法会自动检查引用
     计数，当引用计数为0时才真正地销毁请求

         由ngx_http_request_t结构体的main成员中取出对应的原始请求（当然，可能就是这个请求本身），再取出count引用计数并减l。
	然后，检查count引用计数是否已经为0，以及blocked标志位是否为0。如果count已经为O，则证明请求没有其他动作要使用了，同时blocked标
	志位也为0，表示没有HTTP模块还需要处理请求，所以此时请求可以真正释放；如果count引用计数大干0，或者blocked大于0，这样都不可以结
	束请求，ngx_http_close_reques't方法直接结束。
     */
    if (r->count || r->blocked) {
        return;
    }

    //只有count为0才能继续后续的释放资源操作
#if (NGX_HTTP_SPDY)
    if (r->spdy_stream) {
        ngx_http_spdy_close_stream(r->spdy_stream, rc);
        return;
    }
#endif

    #if (NGX_HTTP_V2)
    if (r->stream) {
        ngx_http_v2_close_stream(r->stream, rc);
        return;
    }
    #endif

    ngx_http_free_request(r, rc);
    ngx_http_close_connection(c);
}

/*
ngx_http_free_request穷法将会释放请求对应的ngx_http_request_t数据结构，它并不会像ngx_http_close_connection方法一样去释放承载请求的
TCP连接，每一个TCP连接可以反复地承载多个HTTP请求，因此，ngx_http_free_request是比ngx_http_close_connection更高层次的方法，前者必然先于后者调用

ngx_http_close_request方法是更高层的用于关闭请求的方法，当然，HTTP模块一般也不会直接调用它的。在上面几节中反复提到的引用计数，
就是由ngx_http_close_request方法负责检测的，同时它会在引用计数清零时正式调用ngx_http_free_request方法和ngx_http_close_connection(ngx_close_connection)
方法来释放请求、关闭连接,见ngx_http_close_request
*/
void
ngx_http_free_request(ngx_http_request_t *r, ngx_int_t rc) //释放request的相关资源
{
    ngx_log_t                 *log;
    ngx_pool_t                *pool;
    struct linger              linger;
    ngx_http_cleanup_t        *cln;
    ngx_http_log_ctx_t        *ctx;
    ngx_http_core_loc_conf_t  *clcf;

    log = r->connection->log;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "http close request");

    if (r->pool == NULL) {
        ngx_log_error(NGX_LOG_ALERT, log, 0, "http request already closed");
        return;
    }

    cln = r->cleanup;
    r->cleanup = NULL;
    /* 循环地遍历请求ngx_http_request_t结构体中的cleanup链表，依次调用每一个ngx_http_cleanup_pt方法释放资源。 */
    while (cln) {
        if (cln->handler) {
            cln->handler(cln->data);
        }

        cln = cln->next;
    }

#if (NGX_STAT_STUB)

    if (r->stat_reading) {
        (void) ngx_atomic_fetch_add(ngx_stat_reading, -1);
    }

    if (r->stat_writing) {
        (void) ngx_atomic_fetch_add(ngx_stat_writing, -1);
    }

#endif

    if (rc > 0 && (r->headers_out.status == 0 || r->connection->sent == 0)) {
        r->headers_out.status = rc;
    }

    log->action = "logging request";

    /* 
    在11个ngx_http_phases阶段中，最后一个阶段叫做NGX_HTTP_LOG_PHASE，它是用来记录客户端的访问日志的。在这一步骤中，将会依次
    调用NGX_HTTP_LOG_PHASE阶段的所有回调方法记录日志。官方的ngx_http_log_module模块就是在这里记录access_log的。
     */
    ngx_http_log_request(r); //打印http log handler  接入按照access_format格式日志写入接入日志文件

    log->action = "closing request";

    /*
     socketstructtcpwindows数据结构此选项指定函数close对面向连接的协议如何操作（如TCP）。内核缺省close操作是立即返回，如果有
     数据残留在套接口缓冲区中则系统将试着将这些数据发送给对方。 

     SO_LINGER选项用来改变此缺省设置。使用如下结构： 
     struct linger { 
     
          int l_onoff; / * 0 = off, nozero = on * / 
     
          int l_linger; / * linger time * / 
     
     }; 
     
     有下列三种情况： 
     
     1、设置 l_onoff为0，则该选项关闭，l_linger的值被忽略，等于内核缺省情况，close调用会立即返回给调用者，如果可能将会传输任何未发送的数据； 
     
     2、设置 l_onoff为非0，l_linger为0，则套接口关闭时TCP夭折连接，TCP将丢弃保留在套接口发送缓冲区中的任何数据并发送一个RST给对方，而不是通常的四
     分组终止序列，这避免了TIME_WAIT状态； 
     3、设置 l_onoff 为非0，l_linger为非0，当套接口关闭时内核将拖延一段时间（由l_linger决定）。如果套接口缓冲区中仍残留数据，进程将处于睡眠状态，
     直到（a）所有数据发送完且被对方确认，之后进行正常的终止序列（描述字访问计数为0）或（b）延迟时间到。此种情况下，应用程序检查close的返回值是非常重要的，
     如果在数据发送完并被确认前时间到，close将返回EWOULDBLOCK错误且套接口发送缓冲区中的任何数据都丢失 。 close的成功返回仅告诉我们发送的数据（和FIN）已
     由对方TCP确认，它并不能告诉我们对方应用进程是否已读了数据。如果套接口设为非阻塞的，它将不等待close完成。 

     SO_LINGER是一个socket选项，通过setsockopt API进行设置，使用起来比较简单，但其实现机制比较复杂，且字面意思上比较难理解。
     解释最清楚的当属《Unix网络编程卷1》中的说明（7.5章节），这里简单摘录：
     SO_LINGER的值用如下数据结构表示：
     struct linger {
          int l_onoff; / * 0 = off, nozero = on * /
          int l_linger; / * linger time * /
     
     };
     
     
     
     
     其取值和处理如下：
     1、设置 l_onoff为0，则该选项关闭，l_linger的值被忽略，等于内核缺省情况，close调用会立即返回给调用者，如果可能将会传输任何未发送的数据；
     2、设置 l_onoff为非0，l_linger为0，则套接口关闭时TCP夭折连接，TCP将丢弃保留在套接口发送缓冲区中的任何数据并发送一个RST给对方，
        而不是通常的四分组终止序列，这避免了TIME_WAIT状态；
     3、设置 l_onoff 为非0，l_linger为非0，当套接口关闭时内核将拖延一段时间（由l_linger决定）。
        如果套接口缓冲区中仍残留数据，进程将处于睡眠状态，直 到（a）所有数据发送完且被对方确认，之后进行正常的终止序列（描述字访问计数为0）
        或（b）延迟时间到。此种情况下，应用程序检查close的返回值是非常重要的，如果在数据发送完并被确认前时间到，close将返回EWOULDBLOCK错误且套接口发送缓冲区中的任何数据都丢失。
        close的成功返回仅告诉我们发送的数据（和FIN）已由对方TCP确认，它并不能告诉我们对方应用进程是否已读了数据。如果套接口设为非阻塞的，它将不等待close完成。
        
     第一种情况其实和不设置没有区别，第二种情况可以用于避免TIME_WAIT状态，但在Linux上测试的时候，并未发现发送了RST选项，而是正常进行了四步关闭流程，
     初步推断是“只有在丢弃数据的时候才发送RST”，如果没有丢弃数据，则走正常的关闭流程。
     查看Linux源码，确实有这么一段注释和源码：
     =====linux-2.6.37 net/ipv4/tcp.c 1915=====
     / * As outlined in RFC 2525, section 2.17, we send a RST here because
     * data was lost. To witness the awful effects of the old behavior of
     * always doing a FIN, run an older 2.1.x kernel or 2.0.x, start a bulk
     * GET in an FTP client, suspend the process, wait for the client to
     * advertise a zero window, then kill -9 the FTP client, wheee...
     * Note: timeout is always zero in such a case.
     * /
     if (data_was_unread) {
     / * Unread data was tossed, zap the connection. * /
     NET_INC_STATS_USER(sock_net(sk), LINUX_MIB_TCPABORTONCLOSE);
     tcp_set_state(sk, TCP_CLOSE);
     tcp_send_active_reset(sk, sk->sk_allocation);
     } 
     另外，从原理上来说，这个选项有一定的危险性，可能导致丢数据，使用的时候要小心一些，但我们在实测libmemcached的过程中，没有发现此类现象，
     应该是和libmemcached的通讯协议设置有关，也可能是我们的压力不够大，不会出现这种情况。
     
     
     第三种情况其实就是第一种和第二种的折中处理，且当socket为非阻塞的场景下是没有作用的。
     对于应对短连接导致的大量TIME_WAIT连接问题，个人认为第二种处理是最优的选择，libmemcached就是采用这种方式，
     从实测情况来看，打开这个选项后，TIME_WAIT连接数为0，且不受网络组网（例如是否虚拟机等）的影响。 
     */
    if (r->connection->timedout) { //减少time_wait状态，内核的四次挥手,加快四次挥手。只有在关闭套接字的时候内核还有数据没有接收或者发送才会发送RST
        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        if (clcf->reset_timedout_connection) {
            linger.l_onoff = 1;
            linger.l_linger = 0;

            if (setsockopt(r->connection->fd, SOL_SOCKET, SO_LINGER,
                           (const void *) &linger, sizeof(struct linger)) == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno,
                              "setsockopt(SO_LINGER) failed");
            }
        }
    }

    /* the various request strings were allocated from r->pool */
    ctx = log->data;
    ctx->request = NULL;

    r->request_line.len = 0;

    r->connection->destroyed = 1;

    /*
     * Setting r->pool to NULL will increase probability to catch double close
     * of request since the request object is allocated from its own pool.
     */

    pool = r->pool;
    r->pool = NULL;

    ngx_destroy_pool(pool); /* 释放request->pool */
}


//当包体应答给客户端后，在ngx_http_free_request中调用日志的handler来记录请求信息到log日志
static void
ngx_http_log_request(ngx_http_request_t *r)
{
    ngx_uint_t                  i, n;
    ngx_http_handler_pt        *log_handler;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    log_handler = cmcf->phases[NGX_HTTP_LOG_PHASE].handlers.elts;
    n = cmcf->phases[NGX_HTTP_LOG_PHASE].handlers.nelts;

    for (i = 0; i < n; i++) {
        log_handler[i](r); //ngx_http_log_handler
    }
}

/*
ngx_http_close connection方法是HTTP框架提供的一个用于释放TCP连接的方法，它的目的很简单，就是关闭这个TCP连接，当且仅当HTTP
请求真正结束时才会调用这个方法
*/
/*
ngx_http_free_request穷法将会释放请求对应的ngx_http_request_t数据结构，它并不会像ngx_http_close_connection方法一样去释放承载请求的
TCP连接，每一个TCP连接可以反复地承载多个HTTP请求，因此，ngx_http_free_request是比ngx_http_close_connection更高层次的方法，前者必然先于后者调用

ngx_http_close_request方法是更高层的用于关闭请求的方法，当然，HTTP模块一般也不会直接调用它的。在上面几节中反复提到的引用计数，
就是由ngx_http_close_request方法负责检测的，同时它会在引用计数清零时正式调用ngx_http_free_request方法和ngx_http_close_connection(ngx_close_connection)
方法来释放请求、关闭连接,见ngx_http_close_request
*/
void
ngx_http_close_connection(ngx_connection_t *c)
{
    ngx_pool_t  *pool;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "close http connection: %d", c->fd);

#if (NGX_HTTP_SSL)

    if (c->ssl) {
        if (ngx_ssl_shutdown(c) == NGX_AGAIN) {
            c->ssl->handler = ngx_http_close_connection;
            return;
        }
    }

#endif

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_active, -1);
#endif

    c->destroyed = 1; //表示connection销毁

    pool = c->pool;

    ngx_close_connection(c);

    ngx_destroy_pool(pool);
}


static u_char *
ngx_http_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    u_char              *p;
    ngx_http_request_t  *r;
    ngx_http_log_ctx_t  *ctx;

    if (log->action) {
        p = ngx_snprintf(buf, len, " while %s", log->action);
        len -= p - buf;
        buf = p;
    }

    ctx = log->data;

    p = ngx_snprintf(buf, len, ", client: %V", &ctx->connection->addr_text);
    len -= p - buf;

    r = ctx->request;

    if (r) {
        return r->log_handler(r, ctx->current_request, p, len); //ngx_http_log_error_handler

    } else {
        p = ngx_snprintf(p, len, ", server: %V",
                         &ctx->connection->listening->addr_text);
    }

    return p;
}


static u_char *
ngx_http_log_error_handler(ngx_http_request_t *r, ngx_http_request_t *sr,
    u_char *buf, size_t len)
{
    char                      *uri_separator;
    u_char                    *p;
    ngx_http_upstream_t       *u;
    ngx_http_core_srv_conf_t  *cscf;

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

    p = ngx_snprintf(buf, len, ", server: %V", &cscf->server_name);
    len -= p - buf;
    buf = p;

    if (r->request_line.data == NULL && r->request_start) {
        for (p = r->request_start; p < r->header_in->last; p++) {
            if (*p == CR || *p == LF) {
                break;
            }
        }

        r->request_line.len = p - r->request_start;
        r->request_line.data = r->request_start;
    }

    if (r->request_line.len) {
        p = ngx_snprintf(buf, len, ", request: \"%V\"", &r->request_line);
        len -= p - buf;
        buf = p;
    }

    if (r != sr) {
        p = ngx_snprintf(buf, len, ", subrequest: \"%V\"", &sr->uri);
        len -= p - buf;
        buf = p;
    }

    u = sr->upstream;

    if (u && u->peer.name) {

        uri_separator = "";

#if (NGX_HAVE_UNIX_DOMAIN)
        if (u->peer.sockaddr && u->peer.sockaddr->sa_family == AF_UNIX) {
            uri_separator = ":";
        }
#endif

        p = ngx_snprintf(buf, len, ", upstream: \"%V%V%s%V\"",
                         &u->schema, u->peer.name,
                         uri_separator, &u->uri);
        len -= p - buf;
        buf = p;
    }

    if (r->headers_in.host) {
        p = ngx_snprintf(buf, len, ", host: \"%V\"",
                         &r->headers_in.host->value);
        len -= p - buf;
        buf = p;
    }

    if (r->headers_in.referer) {
        p = ngx_snprintf(buf, len, ", referrer: \"%V\"",
                         &r->headers_in.referer->value);
        buf = p;
    }

    return buf;
}
