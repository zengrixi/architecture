// project1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdarg.h>
#include <stdio.h>

using namespace std;
#pragma warning(disable : 4996)

int main()
{
	//一：信号功能实战
	//signal()：注册信号处理程序的函数；
	//商业软件中，不用signal()，而要用sigaction();

	//二：nginx中创建worker子进程
	//官方nginx ,一个master进程，创建了多个worker子进程；
	// master process ./nginx 
	// worker process
	//(i)ngx_master_process_cycle()        //创建子进程等一系列动作
	//(i)    ngx_setproctitle()            //设置进程标题    
	//(i)    ngx_start_worker_processes()  //创建worker子进程   
	//(i)        for (i = 0; i < threadnums; i++)   //master进程在走这个循环，来创建若干个子进程
	//(i)            ngx_spawn_process(i,"worker process");
	//(i)                pid = fork(); //分叉，从原来的一个master进程（一个叉），分成两个叉（原有的master进程，以及一个新fork()出来的worker进程
	//(i)                //只有子进程这个分叉才会执行ngx_worker_process_cycle()
	//(i)                ngx_worker_process_cycle(inum,pprocname);  //子进程分叉
	//(i)                    ngx_worker_process_init();
	//(i)                        sigemptyset(&set);  
	//(i)                        sigprocmask(SIG_SETMASK, &set, NULL); //允许接收所有信号
	//(i)                        ngx_setproctitle(pprocname);          //重新为子进程设置标题为worker process
	//(i)                        for ( ;; ) {}. ....                   //子进程开始在这里不断的死循环

	//(i)    sigemptyset(&set); 
	//(i)    for ( ;; ) {}.                //父进程[master进程]会一直在这里循环

	//kill -9 -1344   ，用负号 -组id，可以杀死一组进程

	//（2.1）sigsuspend()函数讲解
	//a)根据给定的参数设置新的mask 并 阻塞当前进程【因为是个空集，所以不阻塞任何信号】
	//b)此时，一旦收到信号，便恢复原先的信号屏蔽【我们原来的mask在上边设置的，阻塞了多达10个信号，从而保证我下边的执行流程不会再次被其他信号截断】
	//c)调用该信号对应的信号处理函数
	//d)信号处理函数返回后，sigsuspend返回，使程序流程继续往下走


	//三：日志输出重要信息谈
	//（3.1）换行回车进一步示意
	//\r：回车符,把打印【输出】信息的为止定位到本行开头
	//\n：换行符，把输出为止移动到下一行 
	//一般把光标移动到下一行的开头，\r\n
	//a)比如windows下，每行结尾 \r\n
	//b)类Unix，每行结尾就只有\n 
	//c)Mac苹果系统，每行结尾只有\r
	//结论：统一用\n就行了

	//（3.2）printf()函数不加\n无法及时输出的解释
	//printf末尾不加\n就无法及时的将信息显示到屏幕 ，这是因为 行缓存[windows上一般没有，类Unix上才有]
	//需要输出的数据不直接显示到终端，而是首先缓存到某个地方，当遇到行刷新表指或者该缓存已满的情况下，菜会把缓存的数据显示到终端设备；
	//ANSI C中定义\n认为是行刷新标记，所以，printf函数没有带\n是不会自动刷新输出流，直至行缓存被填满才显示到屏幕上；
	//所以大家用printf的时候，注意末尾要用\n；
	//或者：fflush(stdout);
	//或者：setvbuf(stdout,NULL,_IONBF,0); //这个函数. 直接将printf缓冲区禁止， printf就直接输出了。
	//标准I/O函数，后边还会讲到

	//四：write()函数思考
	//多个进程同时去写一个文件,比如5个进程同时往日志文件中写，会不会造成日志文件混乱。
	//多个进程同时写 一个日志文件，我们看到输出结果并不混乱，是有序的；我们的日志代码应对多进程往日志文件中写时没有问题；
	//《Unix环境高级编程 第三版》第三章：文件I/O里边的3.10-3.12，涉及到了文件共享、原子操作以及函数dup,dup2的讲解；
	 //第八章：进程控制 里庇安的8.3，涉及到了fork()函数；
	//a)多个进程写一个文件，可能会出现数据覆盖，混乱等情况
	//b)ngx_log.fd = open((const char *)plogname,O_WRONLY|O_APPEND|O_CREAT,0644);  
	  //O_APPEND这个标记能够保证多个进程操作同一个文件时不会相互覆盖；
	//c)内核wirte()写入时是原子操作；
	//d)父进程fork()子进程是亲缘关系。是会共享文件表项，
	//--------------关于write()写的安全问题，是否数据成功被写到磁盘；
	//e)write()调用返回时，内核已经将应用程序缓冲区所提供的数据放到了内核缓冲区，但是无法保证数据已经写出到其预定的目的地【磁盘 】；
	//的确，因为write()调用速度极快，可能没有时间完成该项目的工作【实际写磁盘】，所以这个wirte()调用不等价于数据在内核缓冲区和磁盘之间的数据交换
	//f)打开文件使用了 O_APPEND，多个进程写日志用write()来写；
	//（4.1）掉电导致write()的数据丢失破解法
	//a)直接I/O：直接访问物理磁盘：
	//O_DIRECT：绕过内核缓冲区。用posix_memalign
	//b)open文件时用O_SYNC选项：
	//同步选项【把数据直接同步到磁盘】,只针对write函数有效，使每次write()操作等待物理I/O操作的完成；
	//具体说，就是将写入内核缓冲区的数据立即写入磁盘，将掉电等问题造成的损失减到最小；
	//每次写磁盘数据，务必要大块大块写，一般都512-4k 4k的写；不要每次只写几个字节，否则会被抽死；************

	//c)缓存同步：尽量保证缓存数据和写道磁盘上的数据一致；
	//sync(void)：将所有修改过的块缓冲区排入写队列；然后返回，并不等待实际写磁盘操作结束，数据是否写入磁盘并没有保证；
	//fsync(int fd)：将fd对应的文件的块缓冲区立即写入磁盘，并等待实际写磁盘操作结束返回；*******************************
	//fdatasync(int fd)：类似于fsync，但只影响文件的数据部分。而fsync不一样，fsync除数据外，还会同步更新文件属性；

	//write(4k),1000次之后，一直到把这个write完整[假设整个文件4M]。
	//fsync(fd) ,1次fsync [多次write,每次write建议都4k，然后调用一次fsync()，这才是用fsync()的正确用法****************]

	//五：标准IO库
	//fopen,fclose
	//fread,fwrite
	//fflush
	//fseek
	//fgetc,getc,getchar
	//fputc,put,putchar
	//fgets,gets
	//printf,fprintf,sprintf
	//scanf,fscan,sscanf

	//fwrite和write有啥区别；
	//fwrite()是标准I/O库一般在stdio.h文件
	//write()：系统调用；

	//有一句话：所有系统调用都是原子性的


}


