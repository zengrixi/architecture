/*
 * MIT License
 *
 * Copyright (c) [2018] [WangBoJing]

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
****       *****                                  ************
  ***        *                                    **   **    *
  ***        *         *                          *    **    **
  * **       *         *                         *     **     *
  * **       *         *                         *     **     *
  *  **      *        **                               **
  *  **      *       ***                               **
  *   **     *    ***********    *****    *****        **             *****         *  ****
  *   **     *        **           **      **          **           ***    *     **** *   **
  *    **    *        **           **      *           **           **     **      ***     **
  *    **    *        **            *      *           **          **      **      **       *
  *     **   *        **            **     *           **         **       **      **       **
  *     **   *        **             *    *            **         **               **       **
  *      **  *        **             **   *            **         **               **       **
  *      **  *        **             **   *            **         **               **       **
  *       ** *        **              *  *             **         **               **       **
  *       ** *        **              ** *             **         **               **       **
  *        ***        **               * *             **         **         *     **       **
  *        ***        **     *         **              **          **        *     **      **
  *         **        **     *         **              **          **       *      ***     **
  *         **         **   *          *               **           **     *       ****   **
*****        *          ****           *             ******           *****        **  ****
                                       *                                           **
                                      *                                            **
                                  *****                                            **
                                  ****                                           ******

 *
 */



#ifndef __NTY_EPOLL_INNER_H__
#define __NTY_EPOLL_INNER_H__


#include "nty_socket.h"
#include "nty_epoll.h"
#include "nty_buffer.h"
#include "nty_header.h"


typedef struct _nty_epoll_stat {
	uint64_t calls;
	uint64_t waits;
	uint64_t wakes;

	uint64_t issued;
	uint64_t registered;
	uint64_t invalidated;
	uint64_t handled;
} nty_epoll_stat;

typedef struct _nty_epoll_event_int {
	nty_epoll_event ev;
	int sockid;
} nty_epoll_event_int;

typedef enum {
	USR_EVENT_QUEUE = 0,
	USR_SHADOW_EVENT_QUEUE = 1,
	NTY_EVENT_QUEUE = 2
} nty_event_queue_type;


typedef struct _nty_event_queue {
	nty_epoll_event_int *events;
	int start;
	int end;
	int size;
	int num_events;
} nty_event_queue;

typedef struct _nty_epoll {
	nty_event_queue *usr_queue;
	nty_event_queue *usr_shadow_queue;
	nty_event_queue *queue;

	uint8_t waiting;
	nty_epoll_stat stat;

	pthread_cond_t epoll_cond;
	pthread_mutex_t epoll_lock;
} nty_epoll;

int nty_epoll_add_event(nty_epoll *ep, int queue_type, struct _nty_socket_map *socket, uint32_t event);
int nty_close_epoll_socket(int epid);
int nty_epoll_flush_events(uint32_t cur_ts);


#if NTY_ENABLE_EPOLL_RB

//这是个节点相关的结构
//作为红黑树的一个节点
struct epitem {
	RB_ENTRY(epitem) rbn;
	/*  RB_ENTRY相当如定义了如下的一个结构成员变量
	struct {											
	struct type *rbe_left;		//指向左子树
	struct type *rbe_right;		//指向右子树
	struct type *rbe_parent;	//指向父节点
	int rbe_color;			    //该红黑树节点颜色
	} rbn*/

	LIST_ENTRY(epitem) rdlink;
	/*
	struct {									
		struct type *le_next;	//指向下个元素
		struct type **le_prev;	//前一个元素的地址
	}*/

	int rdy; //exist in list 是否这个节点是同时在双向链表中【这个节点刚开始是在红黑树中】
	
	int sockfd;
	struct epoll_event event; 
};

static int sockfd_cmp(struct epitem *ep1, struct epitem *ep2) {
	if (ep1->sockfd < ep2->sockfd) return -1;
	else if (ep1->sockfd == ep2->sockfd) return 0;
	return 1;
}


RB_HEAD(_epoll_rb_socket, epitem);
/*
#define RB_HEAD(_epoll_rb_socket, epitem)	 等价于定义了如下这个结构 
struct _epoll_rb_socket {
		struct epitem *rbh_root; 			
}
*/

RB_GENERATE_STATIC(_epoll_rb_socket, epitem, rbn, sockfd_cmp);

typedef struct _epoll_rb_socket ep_rb_tree;

//调用epoll_create()的时候我们会创建这个结构的对象
struct eventpoll {
	ep_rb_tree rbr;      //ep_rb_tree是个结构，所以rbr是结构变量，这里代表红黑树的根；
	int rbcnt;
	
	LIST_HEAD( ,epitem) rdlist;    //rdlist是结构变量，这里代表双向链表的根；
	/*	这个LIST_HEAD等价于下边这个 
		struct {
			struct epitem *lh_first;
		}rdlist;
	*/
	int rdnum; //双向链表里边的节点数量（也就是有多少个TCP连接来事件了）

	int waiting;

	pthread_mutex_t mtx; //rbtree update
	pthread_spinlock_t lock; //rdlist update
	
	pthread_cond_t cond; //block for event
	pthread_mutex_t cdmtx; //mutex for cond
	
};


int epoll_event_callback(struct eventpoll *ep, int sockid, uint32_t event);



#endif



#endif



