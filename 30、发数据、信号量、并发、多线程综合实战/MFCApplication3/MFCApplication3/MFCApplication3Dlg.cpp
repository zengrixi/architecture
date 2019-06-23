
// MFCApplication3Dlg.cpp: 实现文件
//

#include "stdafx.h"
#include "MFCApplication3.h"
#include "MFCApplication3Dlg.h"
#include "afxdialogex.h"

#include "ngx_c_crc32.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CMFCApplication3Dlg 对话框



CMFCApplication3Dlg::CMFCApplication3Dlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_MFCAPPLICATION3_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMFCApplication3Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CMFCApplication3Dlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON1, &CMFCApplication3Dlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_BUTTON2, &CMFCApplication3Dlg::OnBnClickedButton2)
	ON_BN_CLICKED(IDC_BUTTON3, &CMFCApplication3Dlg::OnBnClickedButton3)
END_MESSAGE_MAP()


// CMFCApplication3Dlg 消息处理程序

BOOL CMFCApplication3Dlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	// TODO: 在此添加额外的初始化代码

	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CMFCApplication3Dlg::OnPaint()
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
HCURSOR CMFCApplication3Dlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


SOCKET            sClient; //当全局量用
//单击 连接 按钮 会调用本函数
void CMFCApplication3Dlg::OnBnClickedButton1()
{
	// TODO: 在此添加控件通知处理程序代码
	//AfxMessageBox(_T("aaaa"));
	//连接
	static int ifinit = false;
	if (ifinit == false)
	{
		ifinit = true;
		WSADATA m_wsadata;
		if (WSAStartup(0x0202, &m_wsadata))  //在windows平台使用socket前，我们必须要来做一下初始化,最终应该用WSACleanup()释放，但因为是测试代码，就无所谓了
		{
			AfxMessageBox(_T("socket初始化失败!"));
		}
	}
	//(1)创建socket
	sClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sClient == INVALID_SOCKET)
	{
		AfxMessageBox(_T("socket()失败"));
		return;
	}
	//(2)连接服务器
	SOCKADDR_IN       server_in;
	memset(&server_in, 0, sizeof(SOCKADDR_IN));
	server_in.sin_family = AF_INET;
	server_in.sin_port = htons(80);
	server_in.sin_addr.s_addr = inet_addr("192.168.1.126");

	if (connect(sClient, (struct sockaddr *)&server_in, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		AfxMessageBox(_T("connect()失败"));
		closesocket(sClient);
		return;
	}

	//(3)一般来讲可以设置个收发数据超时时间，以免send(),recv()函数调用时被完全卡主
	int iSendRecvTimeOut = 5000;
	if (setsockopt(sClient, SOL_SOCKET, SO_SNDTIMEO, (const char *)&iSendRecvTimeOut, sizeof(int)) == SOCKET_ERROR)
	{
		AfxMessageBox(_T("setsockopt(SO_SNDTIMEO)失败"));
		closesocket(sClient);
		return;
	}
	if (setsockopt(sClient, SOL_SOCKET, SO_RCVTIMEO, (const char *)&iSendRecvTimeOut, sizeof(int)) == SOCKET_ERROR)
	{
		AfxMessageBox(_T("setsockopt(SO_RCVTIMEO)失败"));
		closesocket(sClient);
		return;
	}
	AfxMessageBox(_T("非常好，connect()成功"));
	return;
}

//发送数据，值得讲解
int SendData(SOCKET sSocket, char *p_sendbuf, int ibuflen)
{
	int usend = ibuflen; //要发送的数目
	int uwrote = 0;      //已发送的数目
	int tmp_sret;

	while (uwrote < usend)
	{
		tmp_sret = send(sSocket, p_sendbuf + uwrote, usend - uwrote, 0);
		if ((tmp_sret == SOCKET_ERROR) || (tmp_sret == 0))
		{
			//有错误发生了
			return SOCKET_ERROR;
		}
		uwrote += tmp_sret;
	}//end while
	return uwrote;
}



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

int  g_iLenPkgHeader = sizeof(COMM_PKG_HEADER);

//按常规规则收取数据:
int RecvCommonData(SOCKET sClient, char *precvBuffer)
{
	int               bytes;

	char              *ptmpbuf;
	int               rbytes = 0;         //收到的总字节数
	int               allowbytes;       //总共允许收这么多数据 

	//先收个包头的宽度
	bytes = recv(sClient, precvBuffer, g_iLenPkgHeader, 0);  

	if ((bytes == SOCKET_ERROR) || (bytes == 0))
		return SOCKET_ERROR;

	ptmpbuf = precvBuffer;
	allowbytes = g_iLenPkgHeader;
	rbytes += bytes;

	if (bytes < g_iLenPkgHeader)
	{
		//没收够包头
contrecvhead:
		allowbytes = allowbytes - bytes; //允许收的字节数减少
		ptmpbuf = ptmpbuf + bytes;    //内存向后移动
		//继续收，
		bytes = recv(sClient, ptmpbuf, allowbytes, 0);
		if ((bytes == SOCKET_ERROR) || (bytes == 0))
			return SOCKET_ERROR;
		rbytes += bytes;
		if (bytes < allowbytes)
		{
			//没收够包头,继续收
			goto contrecvhead;
		}
		//包头收完了，该收其他的了
		goto recvqita;
	}

	//收够了
recvqita:
	LPCOMM_PKG_HEADER pPkgHeader;
	pPkgHeader = (LPCOMM_PKG_HEADER)precvBuffer;
	unsigned short iLen = ntohs(pPkgHeader->pkgLen); //包长
	//if (iLen > _PKG_MAX_LENGTH)
	//{
	//	return SOCKET_ERROR;//非法数据包
	//}
	if (iLen == g_iLenPkgHeader)   //该包只有一个包头，已经收完
	{
		return iLen;
	}
	//继续收包体
	allowbytes = iLen - g_iLenPkgHeader; //允许收这么多
	ptmpbuf = precvBuffer + g_iLenPkgHeader; //收缓冲区跳过包头

contrecv2:
	bytes = recv(sClient, ptmpbuf, allowbytes, 0);
	if ((bytes == SOCKET_ERROR) || (bytes == 0))
		return SOCKET_ERROR;
	rbytes += bytes;
	if (bytes < allowbytes)
	{
		//没收够包头,继续收
		allowbytes = allowbytes - bytes; //允许收的字节数减少
		ptmpbuf = ptmpbuf + bytes;    //内存向后移动
		goto contrecv2;
	}
	//这里表示收够了
	return rbytes;
}

//单击 发包 按钮 会调用本函数
void CMFCApplication3Dlg::OnBnClickedButton2()
{
	// TODO: 在此添加控件通知处理程序代码
	//AfxMessageBox(_T("bbbb"));
	//先发个注册命令
	CCRC32   *p_crc32 = CCRC32::GetInstance();

	char *p_sendbuf = (char *)new char[g_iLenPkgHeader + sizeof(STRUCT_REGISTER)];

	LPCOMM_PKG_HEADER         pinfohead;
	pinfohead = (LPCOMM_PKG_HEADER)p_sendbuf;
	pinfohead->msgCode = 5;
	pinfohead->msgCode = htons(pinfohead->msgCode);
	//pinfohead->crc32 = htonl(123); //测试，所以随便来 一个
	pinfohead->pkgLen = htons(g_iLenPkgHeader + sizeof(STRUCT_REGISTER));

	LPSTRUCT_REGISTER pstruc_sendstruc = (LPSTRUCT_REGISTER)(p_sendbuf + g_iLenPkgHeader);
	pstruc_sendstruc->iType = htonl(100);
	strcpy(pstruc_sendstruc->username, "1234");

	//crc值需要最后算的
	pinfohead->crc32   = p_crc32->Get_CRC((unsigned char *)pstruc_sendstruc, sizeof(STRUCT_REGISTER));
	pinfohead->crc32   = htonl(pinfohead->crc32); //针对四字节数字，主机序转网络序

	if (SendData(sClient, p_sendbuf, g_iLenPkgHeader + sizeof(STRUCT_REGISTER)) == SOCKET_ERROR)
	{
		AfxMessageBox(_T("SendData()失败"));
		//测试程序也就不关闭socket，直接返回去算了
		delete[] p_sendbuf; //内存总还是要释放的
		return;
	}

	delete[] p_sendbuf; //释放上边的内存
	/*

	//再发个登录命令
	p_sendbuf = (char *)new char[g_iLenPkgHeader + sizeof(STRUCT_LOGIN)];

	//LPCOMM_PKG_HEADER         pinfohead;
	pinfohead = (LPCOMM_PKG_HEADER)p_sendbuf;
	pinfohead->msgCode = 6;  //变化一下
	pinfohead->msgCode = htons(pinfohead->msgCode);
	//pinfohead->crc32 = htonl(345); //测试，所以随便来 一个
	pinfohead->pkgLen = htons(g_iLenPkgHeader + sizeof(STRUCT_LOGIN));

	LPSTRUCT_LOGIN pstruc_sendstruc2 = (LPSTRUCT_LOGIN)(p_sendbuf + g_iLenPkgHeader);
	strcpy(pstruc_sendstruc2->username, "5678");

	//crc值需要最后算的
	pinfohead->crc32 = p_crc32->Get_CRC((unsigned char *)pstruc_sendstruc2, sizeof(STRUCT_LOGIN));
	pinfohead->crc32 = htonl(pinfohead->crc32); //针对四字节数字，主机序转网络序

	if (SendData(sClient, p_sendbuf, g_iLenPkgHeader + sizeof(STRUCT_LOGIN)) == SOCKET_ERROR)
	{
		AfxMessageBox(_T("SendData()失败"));
		//测试程序也就不关闭socket，直接返回去算了
		delete[] p_sendbuf; //内存总还是要释放的
		return;
	}
	delete[] p_sendbuf;
	//closesocket(sClient);*/

	AfxMessageBox(_T("非常好，数据包通过SendData()都发送完毕"));
		
	/*
	char    recvBuffer[100000] = { 0 };
	int rec = RecvCommonData(sClient, recvBuffer);
	if (rec == SOCKET_ERROR)
	{
		AfxMessageBox(_T("很不幸，接收数据失败"));
		closesocket(sClient);
		return;
	}
	char result[10000] = { 0 };
	sprintf_s(result, sizeof(result), "非常好，接收数据成功，收到字节数是%d个", rec);
	AfxMessageBox(result);
	*/
}


//单击 收包 按钮 会调用本函数
void CMFCApplication3Dlg::OnBnClickedButton3()
{
	// TODO: 在此添加控件通知处理程序代码
	//Sleep(10000);
	//这里收包
	char    recvBuffer[100000] = { 0 };
	int rec = RecvCommonData(sClient,recvBuffer);
	if (rec == SOCKET_ERROR)
	{
		AfxMessageBox(_T("很不幸，接收数据失败"));
		closesocket(sClient);
		return;
	}
	char result[10000] = { 0 };
	sprintf_s(result, sizeof(result), "非常好，接收数据成功，收到字节数是%d个", rec);	
	AfxMessageBox(result);
}
