
// MFCApplication4Dlg.cpp: 实现文件
//

#include "stdafx.h"
#include "MFCApplication4.h"
#include "MFCApplication4Dlg.h"
#include "afxdialogex.h"

#include "ngx_c_crc32.h"
#include "MersenneTwister.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#pragma warning(disable : 4996)
#define _CONNECTION_COUNT_          2048      //创建多少个连接，此值越大，当然recv失败的机会越大，返回10060,表示超时，setsockopt里设置了5秒的；
#define _THREAD_COUNT_              100       //准备创建这么多个线程
#define CESHIXIBIAO _CONNECTION_COUNT_ + 2000 //测试下标
#define SERVERIPADDR "192.168.1.126"
#define DEFAULT_PORT 80
#define _RECVTIMEOUT_               1500 //超时等待时间（单位：毫秒）

CListBox *g_plist;


struct localStruc
{
	unsigned  *m_ThreadID;
};
struct localStruc passin_struct;   //传递到线程里去的一个结构

struct socketinfo
{
	SOCKET        sClient[CESHIXIBIAO];            //创建的套接字句柄
	int           sign[CESHIXIBIAO];               //标记这个连接是否成功连接上   ,1:成功连接，0:未成功
	DWORD         createsockettime[CESHIXIBIAO];   //创建套接字的时间
	DWORD         sendinfotime[CESHIXIBIAO];       //发送信息的事件
	DWORD         recvintoreturnerrortime[CESHIXIBIAO]; //接收信息返回错误时间
};

//结构定义------------------------------------
#pragma pack (1) //对齐方式,1字节对齐 
//一些和网络通讯相关的结构放在这里
typedef struct _COMM_PKG_HEADER
{
	unsigned short pkgLen;    //报文总长度【包头+包体】--2字节，2字节可以表示的最大数字为6万多，我们定义_PKG_MAX_LENGTH 30000，所以用pkgLen足够保存下
	unsigned short msgCode;   //消息类型代码--2字节，用于区别每个不同的命令【不同的消息】
	int            crc32;     //CRC32效验--4字节，为了防止收发数据中出现收到内容和发送内容不一致的情况，引入这个字段做一个基本的校验用	
}COMM_PKG_HEADER, *LPCOMM_PKG_HEADER;

typedef struct _STRUCT_REGISTER
{
	int           iType;          //类型
	char          username[56];   //用户名 
	char          password[40];   //密码

}STRUCT_REGISTER, *LPSTRUCT_REGISTER;

typedef struct _STRUCT_LOGIN
{
	char          username[56];   //用户名 
	char          password[40];   //密码

}STRUCT_LOGIN, *LPSTRUCT_LOGIN;

#pragma pack() //取消指定对齐，恢复缺省对齐


//线程相关的全局量定义
unsigned dwThreadId[_THREAD_COUNT_];
HANDLE   hProcessThread[_THREAD_COUNT_];

//计算时间相关
DWORD g_beginmillsecond = 0;   //开始工作的时间秒数

volatile long g_ifstopmonisendrecv = 1;              //是否模拟收发--1表示模拟
volatile long g_ifstopmoniduan = 1;                  //是否模拟断线--1表示模拟
volatile long g_ifstopmoniconn = 1;                  //是否模拟重新连接--1表示模拟
volatile long g_ifpausetest    = 0;                  //是否暂停测试，0表示暂停测试，1表示不暂停测试

//统计相关
volatile long g_createsocketerror = 0;               //创建socket错误次数统计
volatile long g_connectsocketerror = 0;              //connect服务器失败次数统计
volatile int  g_connectsocketerrorlastErrorinfo = 0; //connect服务器失败，最后一次出错信息,先给0表示没错误.
volatile long g_connecting = 0;                      //当前正连接着的数量
volatile long g_connectsocketsucc = 0;               //connect服务器成功次数统计
volatile long g_sendsocketerror = 0;                 //send失败socket错误次数统计
volatile int  g_sendsocketerrorlastErrorinfo = 0;    //send失败,最后一次出错信息,先给0表示错误
volatile long g_sendokcount = 0;                     //send完成的包的数量统计
volatile long g_remoteclosesocket = 0;               //远程不知什么原因关闭的数量统计
volatile long g_recvsocketerror = 0;                 //recv失败socket错误次数统计
volatile int  g_recvsocketerrorlastErrorinfo = 0;    //recv失败,最后一次出错信息,先给0表示没错误.
volatile long g_recvokcount = 0;                     //recv完成的包的数量统计
volatile long g_closesocketcount = 0;                //模拟断开的socket数量统计

unsigned long g_sendBytesCount = 0;                  //发出总字节数
unsigned long g_recvBytesCount = 0;                  //收到总字节数


//全局变量
MTRand g_ac;

int GetBetweenRand(int iStart, int iEnd)	
{
	unsigned __int32 iRand = g_ac.randInt();
	if (iRand < 0) iRand = iRand * (-1);
	iRand = iRand % (iEnd - iStart + 1);
	iRand += iStart;
	return iRand;
}

//模拟新建和连接socket
void FunccreateSocket(struct socketinfo &clientproc, int count)
{
	int k1;

	for (k1 = 0; k1 < count; k1++)
	{
		if (clientproc.sClient[k1] == INVALID_SOCKET)
		{
			clientproc.sClient[k1] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (clientproc.sClient[k1] == INVALID_SOCKET)
			{
				InterlockedIncrement(&g_createsocketerror);
			}
			else
			{
				clientproc.createsockettime[k1] = GetTickCount();
			}
			clientproc.sign[k1] = 0; //表示连接失败的，不管socket创建是否成功		
		}
	}

	char   szServer[128];
	struct sockaddr_in server;
	int    iPort = DEFAULT_PORT;  // Port on server to connect to
	int iRecvTimeOut = _RECVTIMEOUT_;

	memset(szServer, 0, sizeof(szServer));
	strcpy_s(szServer, sizeof(szServer), SERVERIPADDR);
	server.sin_family = AF_INET;
	server.sin_port = htons(iPort);
	server.sin_addr.s_addr = inet_addr(szServer);

	for (k1 = 0; k1 < count; k1++)
	{
		if (clientproc.sClient[k1] != INVALID_SOCKET && clientproc.sign[k1] == 0)
		{			
			if (connect(clientproc.sClient[k1], (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
			{
				InterlockedIncrement(&g_connectsocketerror);
				int reco = WSAGetLastError();
				g_connectsocketerrorlastErrorinfo = reco;
				Sleep(10);
			}
			else
			{
				int reco;
				reco = setsockopt(clientproc.sClient[k1], SOL_SOCKET, SO_RCVTIMEO, (LPCTSTR)&iRecvTimeOut, sizeof(int));
				reco = setsockopt(clientproc.sClient[k1], SOL_SOCKET, SO_SNDTIMEO, (LPCTSTR)&iRecvTimeOut, sizeof(int));
				clientproc.sign[k1] = 1; //表连接成功标志
				InterlockedIncrement(&g_connecting);
				InterlockedIncrement(&g_connectsocketsucc);
			}	
		} //end if
	}//end for
	return;
}

//模拟断开处理：
void FunccloseSocket(struct socketinfo &clientproc, int count)
{
	int k1;
	srand((unsigned)time(NULL));
	for (k1 = 0; k1 < count; k1++)
	{
		if (clientproc.sClient[k1] != INVALID_SOCKET && clientproc.sign[k1] == 1)
		{
			//有40%机会断开
			if ((rand() % 100) > 75)
			{
				InterlockedIncrement(&g_closesocketcount);
				InterlockedDecrement(&g_connecting);
				closesocket(clientproc.sClient[k1]);
				clientproc.sClient[k1] = INVALID_SOCKET;
				clientproc.sign[k1] = 0;//没connect
				continue;
			}
		}
	}//end for
}

//收发数据处理
void FuncsendrecvData(struct socketinfo &clientproc, int count)
{	
	int k1;
	int ma;
	COMM_PKG_HEADER infohead;
	CCRC32 *p_crc32 = CCRC32::GetInstance();
	int ilen = sizeof(COMM_PKG_HEADER);
	unsigned __int32 iRand;
	int ibodylen;
	unsigned short msgCode;
	char *pq, *pbuf, *pbuf2;
	int sendlen;
	int ret;

	pq = new char[4500];
	for (k1 = 0; k1 < count; k1++)  //count = 每个线程需要创建的连接数目
	{
		if (clientproc.sClient[k1] != INVALID_SOCKET && clientproc.sign[k1] == 1)  //连接成功的socket,可以收发数据
		{
			ma = rand() % 255;
			if (ma % 2 == 0) //偶数
			{
				memset(&infohead, 0, ilen);
				
				//因为本函数是又发包又收包，所以，必须保证所有这几个命令，服务器都有处理【都返回数据包】才行；
				iRand = GetBetweenRand(1, 3);  //构造3个种类的包测试，一个是注册，一个是登录，一个是ping，因为服务器端目前就实现了这三个，而外增加一个任意随机包体尺寸1-500之间								
				if (iRand == 1)
				{
					//注册
					ibodylen = sizeof(STRUCT_REGISTER);
					msgCode = 5;
				}
				else if (iRand == 2)
				{
					//登录
					ibodylen = sizeof(STRUCT_LOGIN);
					msgCode = 6;
				}
				else //if (iRand == 3)
				{
					//ping
					ibodylen = 0;    //无包体
					msgCode = 0;
				}
				//else
				//{
				//	ibodylen = GetBetweenRand(1, 1000); //包体在这个之间
				//	msgCode = ibodylen = GetBetweenRand(0, 10); //超出范围服务器也应该整出处理不会导致问题
				//}

				infohead.pkgLen = ilen + ibodylen;
				infohead.msgCode = msgCode;
				pbuf = pq + ilen; //指向包体
				pbuf2 = pbuf;
				unsigned char a;
				for (int i = 0; i < ibodylen; i++) //随机值填充包体
				{
					a = rand() % 255;
					(*pbuf) = a;
					pbuf++;
				} //end for

				if (iRand != 3)
				{
					//有包体的
					infohead.crc32 = p_crc32->Get_CRC((unsigned char *)pbuf2, ibodylen);		
					//char buffer[1000];
					//sprintf_s(buffer, sizeof(buffer), "crc计算得到%d\n", infohead.crc32); 
					//g_plist->SetCurSel(g_plist->AddString(buffer)); if (g_plist->GetCount() > 500) g_plist->DeleteString(0);
				}
				else
				{
					//无包体
					infohead.crc32 = 0;
				}
				sendlen = infohead.pkgLen;
				infohead.msgCode = htons(infohead.msgCode);
				infohead.pkgLen = htons(infohead.pkgLen);
				infohead.crc32 = htonl(infohead.crc32);
				memcpy(pq, &infohead, ilen); //把头搞进去
				pbuf = pq;				

			lblsend:
				clientproc.sendinfotime[k1] = GetTickCount(); //发送时机
				ret = send(clientproc.sClient[k1], pbuf, sendlen, 0);
				if (ret == SOCKET_ERROR) //对方断开
				{
					InterlockedIncrement(&g_sendsocketerror);  //send失败socket错误次数统计
					g_sendsocketerrorlastErrorinfo = WSAGetLastError();
					closesocket(clientproc.sClient[k1]);
					clientproc.sClient[k1] = INVALID_SOCKET;
					clientproc.sign[k1] = 0;             //没connect
					InterlockedDecrement(&g_connecting); //当前正连接着的数量
					continue;
				}
				else if (ret < sendlen)
				{
					InterlockedExchangeAdd((long *)&g_sendBytesCount, ret); //原子+
					pbuf += ret;
					sendlen = sendlen - ret;
					goto lblsend;
				}
				else
				{
					InterlockedExchangeAdd((long *)&g_sendBytesCount, ret);
					InterlockedIncrement(&g_sendokcount);

					//接收
					pbuf = pq;
					int recvbuflen;
					recvbuflen = 4500;

			lblrecv:
					ret = recv(clientproc.sClient[k1], pbuf, 4500, 0);
					if (ret == 0)//远程关闭
					{
						InterlockedIncrement(&g_remoteclosesocket);
						closesocket(clientproc.sClient[k1]);
						clientproc.sClient[k1] = INVALID_SOCKET;
						clientproc.sign[k1] = 0;//没connect
						InterlockedDecrement(&g_connecting);
						continue;
					}
					else if (ret == SOCKET_ERROR)
					{
						clientproc.recvintoreturnerrortime[k1] = GetTickCount();
						InterlockedIncrement(&g_recvsocketerror);
						g_recvsocketerrorlastErrorinfo = WSAGetLastError();
						closesocket(clientproc.sClient[k1]);
						clientproc.sClient[k1] = INVALID_SOCKET;
						clientproc.sign[k1] = 0;//没connect
						InterlockedDecrement(&g_connecting);
						continue;
					}
					else if (ret < sendlen)
					{
						InterlockedExchangeAdd((long *)&g_recvBytesCount, ret);
						pbuf += ret;
						recvbuflen -= ret;
						goto lblrecv;
					}
					else
					{
						//成功接收
						InterlockedExchangeAdd((long *)&g_recvBytesCount, ret);
						InterlockedIncrement(&g_recvokcount);
						continue;
					}
				} //end if
			} //end if
		} //end if
	}//end for
	delete pq;
	return;
}

//主要干活的线程
unsigned __stdcall ScanThread(LPVOID lppassin_struct)
{
	int    i,k1;
	bool   bFind = false;
	struct socketinfo clientproc;
	char   szServer[128];
	struct sockaddr_in server;
	int    iPort = DEFAULT_PORT;
	int iRecvTimeOut = _RECVTIMEOUT_;

	struct localStruc *smpassin_struct = (struct localStruc *)lppassin_struct;

	for (i = 0; i < _THREAD_COUNT_; i++)
	{
		unsigned hCurrThreadID = GetCurrentThreadId();
		if (hCurrThreadID == smpassin_struct->m_ThreadID[i])
		{
			bFind = true;
			break;
		}
	}
	if (!bFind)
	{
		AfxMessageBox("线程没有找到自己的创建编号");
		_endthreadex(0);
		return -1;
	}
	//i就是编号
	//这个是常规线程，第一个事情就是创建这样多个socket	
	int count = (int)_CONNECTION_COUNT_ / _THREAD_COUNT_;   //创建的连接数量 / 线程数量，得到每个线程需要创建的连接数目
	count += 1; //每个线程要创建这么多个socket；

	for (k1 = 0; k1 < count; k1++)
	{
		clientproc.sClient[k1] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (clientproc.sClient[k1] == INVALID_SOCKET)
		{
			InterlockedIncrement(&g_createsocketerror);
		}
		else
		{
			clientproc.createsockettime[k1] = GetTickCount();
		}
		clientproc.sign[k1] = 0; //无论socket成功没都设置此值，表示连接失败的
	} //end for

	memset(szServer, 0, sizeof(szServer));
	strcpy_s(szServer, sizeof(szServer), SERVERIPADDR);
	server.sin_family = AF_INET;
	server.sin_port = htons(iPort);
	server.sin_addr.s_addr = inet_addr(szServer);

	for (k1 = 0; k1 < count; k1++)
	{
		if (clientproc.sClient[k1] != INVALID_SOCKET)
		{
			if ((rand() % 100) > 65) //1下子连接上去负担太重
			{
				if (connect(clientproc.sClient[k1], (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
				{
					InterlockedIncrement(&g_connectsocketerror);
					int reco = WSAGetLastError();
					g_connectsocketerrorlastErrorinfo = reco;  //connect服务器失败，最后一次出错信息,先给0表示没错误.
					Sleep(10);
				}
				else
				{
					int reco;
					reco = setsockopt(clientproc.sClient[k1], SOL_SOCKET, SO_RCVTIMEO, (LPCTSTR)&iRecvTimeOut, sizeof(int));
					reco = setsockopt(clientproc.sClient[k1], SOL_SOCKET, SO_SNDTIMEO, (LPCTSTR)&iRecvTimeOut, sizeof(int));
					clientproc.sign[k1] = 1; //表连接成功标志
					InterlockedIncrement(&g_connecting);
					InterlockedIncrement(&g_connectsocketsucc);
				}//end if
			} //end if
		} //end if
	} //end for

	//---------------------------------------------------------
	//模拟收发数据
startmoni:
	if (g_ifpausetest == 1)
	{
		//暂停测试
		Sleep(1000);
		goto startmoni;
	}
	if (g_ifstopmonisendrecv == 1)
	{
		FuncsendrecvData(clientproc, count);
	}

	//模拟断一些socket
	if (g_ifstopmoniduan == 1)
	{
		if ((rand() % 100) > 88)
		{
			//有一定机率模拟断开
			FunccloseSocket(clientproc, count);
		}
	}

	//模拟把断的socket进行重连
	if (g_ifstopmoniconn == 1)
	{
		if ((rand() % 100) < 60)
		{
			FunccreateSocket(clientproc, count);
		}
	}
	Sleep(100);
	goto startmoni;
	return 0;
}

// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CMFCApplication4Dlg 对话框



CMFCApplication4Dlg::CMFCApplication4Dlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_MFCAPPLICATION4_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMFCApplication4Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDC_BTN_PAUSE, m_pausebtn);
	DDX_Control(pDX, IDC_BUTTON5, m_btnss);
}

BEGIN_MESSAGE_MAP(CMFCApplication4Dlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BTNREFRESH, &CMFCApplication4Dlg::OnBnClickedBtnrefresh)
	ON_BN_CLICKED(IDC_BUTTON1, &CMFCApplication4Dlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_BUTTON2, &CMFCApplication4Dlg::OnBnClickedButton2)
	ON_BN_CLICKED(IDC_BTNSTART, &CMFCApplication4Dlg::OnBnClickedBtnstart)
	ON_BN_CLICKED(IDC_BTN_PAUSE, &CMFCApplication4Dlg::OnBnClickedBtnPause)
	ON_BN_CLICKED(IDC_BUTTON5, &CMFCApplication4Dlg::OnBnClickedButton5)
END_MESSAGE_MAP()


// CMFCApplication4Dlg 消息处理程序

BOOL CMFCApplication4Dlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 将“关于...”菜单项添加到系统菜单中。

	// IDM_ABOUTBOX 必须在系统命令范围内。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	// TODO: 在此添加额外的初始化代码	
	g_plist = &m_list;
	
	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CMFCApplication4Dlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CMFCApplication4Dlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CMFCApplication4Dlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

//开始测试
void CMFCApplication4Dlg::OnBnClickedBtnstart()
{
	// TODO: 在此添加控件通知处理程序代码
	WSADATA       wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
	{
		AfxMessageBox("初始化socket失败");
		return ;
	}
	for (int m_threadsCount = 0; m_threadsCount < _THREAD_COUNT_; m_threadsCount++)
	{
		hProcessThread[m_threadsCount] = NULL;
		dwThreadId[m_threadsCount] = NULL;
	}
	passin_struct.m_ThreadID = (unsigned *)&dwThreadId;
	g_beginmillsecond = GetTickCount(); //GetTickCount返回（retrieve）从操作系统启动所经过（elapsed）的毫秒数，它的返回值是DWORD

	for (int tmpk = 0; tmpk < _THREAD_COUNT_; tmpk++)
	{
		hProcessThread[tmpk] = (HANDLE)_beginthreadex(NULL, 0, &ScanThread, (LPVOID)&passin_struct, 0, &dwThreadId[tmpk]);
		if (hProcessThread[tmpk] == NULL)
		{
			AfxMessageBox("创建线程失败");
		}
	}
	return;
}

//暂停测试
void CMFCApplication4Dlg::OnBnClickedBtnPause()
{
	// TODO: 在此添加控件通知处理程序代码
	if (g_ifpausetest == 0)
	{
		g_ifpausetest = 1;
		m_pausebtn.SetWindowText("继续测试");
	}
	else
	{
		g_ifpausetest = 0;
		m_pausebtn.SetWindowText("暂停测试");
	}
}

//开启/暂停 模拟断线
void CMFCApplication4Dlg::OnBnClickedButton5()
{
	// TODO: 在此添加控件通知处理程序代码
	if (g_ifstopmoniduan == 1)
	{
		InterlockedDecrement(&g_ifstopmoniduan);
		m_btnss.SetWindowText("开启模拟断线");
	}
	else
	{
		InterlockedIncrement(&g_ifstopmoniduan);
		m_btnss.SetWindowText("暂停模拟断线");
	}
}

//刷新/显示 信息
void CMFCApplication4Dlg::OnBnClickedBtnrefresh()
{		
	// TODO: 在此添加控件通知处理程序代码
	char buffer[200];

	m_list.SetCurSel(m_list.AddString("---------------------------------------------\n")); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	DWORD liushimilsec = GetTickCount() - g_beginmillsecond;  //流逝时间
	m_list.SetCurSel(m_list.AddString("这是client端程序,按包头+灵活包体格式发送数据包,服务器按同样格式接收数据包!\n")); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	m_list.SetCurSel(m_list.AddString("server端收到正确的包后入队列,触发入队列线程池提取队列消息,立即将收到的消息直接完整的扔到发送队列,触发出队列线程池并发送消息.\n")); if (m_list.GetCount() > 500) m_list.DeleteString(0);


	sprintf_s(buffer, sizeof(buffer), "系统运行了=%ld秒\n", int(liushimilsec / 1000)); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	int chushu = int(liushimilsec / 1000);
	if (chushu > 0)
	{
		sprintf_s(buffer, sizeof(buffer), "总共发送字节%ldK(每秒发送%ldK),接收字节%ldK(每秒接收%ldK)\n",
			(int)(g_sendBytesCount / 1024),
			((int)(g_sendBytesCount / 1024)) / chushu,
			(int)(g_recvBytesCount / 1024),
			((int)(g_recvBytesCount / 1024)) / chushu
		);
		m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	}
	sprintf_s(buffer, sizeof(buffer), "当前连接的数量=%ld\n", g_connecting); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	sprintf_s(buffer, sizeof(buffer), "总共连接成功次数=%ld\n", g_connectsocketsucc); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	sprintf_s(buffer, sizeof(buffer), "创建socket错误次数=%ld\n", g_createsocketerror); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	sprintf_s(buffer, sizeof(buffer), "Connect服务器失败次数=%ld,最后一次连接错误号=%ld\n", g_connectsocketerror, g_connectsocketerrorlastErrorinfo); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	sprintf_s(buffer, sizeof(buffer), "Send失败次数=%ld,最后一次Send错误号=%ld\n", g_sendsocketerror, g_sendsocketerrorlastErrorinfo); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	sprintf_s(buffer, sizeof(buffer), "Recv失败次数=%ld,最后一次Recv错误号=%ld\n", g_recvsocketerror, g_recvsocketerrorlastErrorinfo); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	if (chushu > 0)
	{
		sprintf_s(buffer, sizeof(buffer), "Send完成的包数量=%ld,平均每秒Send包数量=%ld\n", g_sendokcount, (int)(g_sendokcount / chushu)); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
		sprintf_s(buffer, sizeof(buffer), "Recv完成的包数量=%ld,平均每秒Recv包数量=%ld\n", g_recvokcount, (int)(g_recvokcount / chushu)); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	}
	sprintf_s(buffer, sizeof(buffer), "远程莫名关闭的连接数量=%ld\n", g_remoteclosesocket); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);
	sprintf_s(buffer, sizeof(buffer), "模拟断开的socket数量=%ld\n", g_closesocketcount); m_list.SetCurSel(m_list.AddString(buffer)); if (m_list.GetCount() > 500) m_list.DeleteString(0);

	return;
}

//清空信息
void CMFCApplication4Dlg::OnBnClickedButton1()
{
	// TODO: 在此添加控件通知处理程序代码
	m_list.ResetContent();
}


//复位全部数据
void CMFCApplication4Dlg::OnBnClickedButton2()
{
	// TODO: 在此添加控件通知处理程序代码
	//复位所有数据，清0的意思
	g_beginmillsecond = GetTickCount();
	g_sendBytesCount = 0;
	g_recvBytesCount = 0;
	//---
	g_sendsocketerror = 0;
	g_recvsocketerror = 0;
	g_connectsocketerror = 0;
	g_sendokcount = 0;
	g_recvokcount = 0;
	g_closesocketcount = 0;
}





