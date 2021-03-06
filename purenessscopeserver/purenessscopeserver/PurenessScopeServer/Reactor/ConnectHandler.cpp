#include "ConnectHandler.h"

CConnectHandler::CConnectHandler(void)
{
    m_szError[0]          = '\0';
    m_u4ConnectID         = 0;
    m_u2SendCount         = 0;
    m_u4AllRecvCount      = 0;
    m_u4AllSendCount      = 0;
    m_u4AllRecvSize       = 0;
    m_u4AllSendSize       = 0;
    m_nIOCount            = 1;
    m_u4SendThresHold     = MAX_MSG_SNEDTHRESHOLD;
    m_u2SendQueueMax      = MAX_MSG_SENDPACKET;
    m_u1ConnectState      = CONNECT_INIT;
    m_u1SendBuffState     = CONNECT_SENDNON;
    m_pCurrMessage        = NULL;
    m_pBlockMessage       = NULL;
    m_pPacketParse        = NULL;
    m_u4CurrSize          = 0;
    m_u4HandlerID         = 0;
    m_u2MaxConnectTime    = 0;
    m_u1IsActive          = 0;
    m_u4MaxPacketSize     = MAX_MSG_PACKETLENGTH;
    m_blBlockState        = false;
    m_nBlockCount         = 0;
    m_nBlockSize          = MAX_BLOCK_SIZE;
    m_nBlockMaxCount      = MAX_BLOCK_COUNT;
    m_u8RecvQueueTimeCost = 0;
    m_u4RecvQueueCount    = 0;
    m_u8SendQueueTimeCost = 0;
    m_u4ReadSendSize      = 0;
    m_u4SuccessSendSize   = 0;
    m_nHashID             = 0;
    m_u8SendQueueTimeout  = MAX_QUEUE_TIMEOUT * 1000 * 1000;  //目前因为记录的是纳秒
    m_u8RecvQueueTimeout  = MAX_QUEUE_TIMEOUT * 1000 * 1000;  //目前因为记录的是纳秒
    m_u2TcpNodelay        = TCP_NODELAY_ON;
    m_emStatus            = CLIENT_CLOSE_NOTHING;
    m_u4SendMaxBuffSize   = 5*MAX_BUFF_1024;
}

CConnectHandler::~CConnectHandler(void)
{
    //OUR_DEBUG((LM_INFO, "[CConnectHandler::~CConnectHandler].\n"));
    //OUR_DEBUG((LM_INFO, "[CConnectHandler::~CConnectHandler]End.\n"));
}

const char* CConnectHandler::GetError()
{
    return m_szError;
}

bool CConnectHandler::Close(int nIOCount)
{
    m_ThreadLock.acquire();

    if(nIOCount > m_nIOCount)
    {
        m_nIOCount = 0;
    }

    if(m_nIOCount > 0)
    {
        m_nIOCount -= nIOCount;
    }

    if(m_nIOCount == 0)
    {
        m_u1IsActive = 0;
    }

    m_ThreadLock.release();

    //OUR_DEBUG((LM_ERROR, "[CConnectHandler::Close]ConnectID=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));

    //从反应器注销事件
    if(m_nIOCount == 0)
    {
        //查看是否是IP追踪信息，是则记录
        //App_IPAccount::instance()->CloseIP((string)m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllSendSize);

        //OUR_DEBUG((LM_ERROR, "[CConnectHandler::Close]ConnectID=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));

        //删除对象缓冲的PacketParse
        if(m_pCurrMessage != NULL)
        {
            App_MessageBlockManager::instance()->Close(m_pCurrMessage);
        }

        //调用连接断开消息
        App_PacketParseLoader::instance()->GetPacketParseInfo()->DisConnect(GetConnectID());

        //组织数据
        _MakePacket objMakePacket;

        objMakePacket.m_u4ConnectID       = GetConnectID();
        objMakePacket.m_pPacketParse      = NULL;
        objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

        //发送客户端链接断开消息。
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();

        if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
        {
            OUR_DEBUG((LM_ERROR, "[CConnectHandler::Close] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
        }

        //msg_queue()->deactivate();
        shutdown();
        AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);

        //删除链接对象
        App_ConnectManager::instance()->CloseConnectByClient(GetConnectID());

        m_u4ConnectID = 0;

        //回归用过的指针
        App_ConnectHandlerPool::instance()->Delete(this);
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::Close](0x%08x)Close(ConnectID=%d) OK.\n", this, GetConnectID()));
        return true;
    }

    return false;
}

void CConnectHandler::Init(uint16 u2HandlerID)
{
    m_u4HandlerID      = u2HandlerID;
    m_u2MaxConnectTime = App_MainConfig::instance()->GetMaxConnectTime();
    m_u4SendThresHold  = App_MainConfig::instance()->GetSendTimeout();
    m_u2SendQueueMax   = App_MainConfig::instance()->GetSendQueueMax();
    m_u4MaxPacketSize  = App_MainConfig::instance()->GetRecvBuffSize();
    m_u2TcpNodelay     = App_MainConfig::instance()->GetTcpNodelay();

    m_u8SendQueueTimeout = App_MainConfig::instance()->GetSendQueueTimeout() * 1000 * 1000;

    if(m_u8SendQueueTimeout == 0)
    {
        m_u8SendQueueTimeout = MAX_QUEUE_TIMEOUT * 1000 * 1000;
    }

    m_u8RecvQueueTimeout = App_MainConfig::instance()->GetRecvQueueTimeout() * 1000 * 1000;

    if(m_u8RecvQueueTimeout <= 0)
    {
        m_u8RecvQueueTimeout = MAX_QUEUE_TIMEOUT * 1000 * 1000;
    }

    m_u4SendMaxBuffSize  = App_MainConfig::instance()->GetBlockSize();
    //m_pBlockMessage      = new ACE_Message_Block(m_u4SendMaxBuffSize);
    m_pBlockMessage      = NULL;
    m_emStatus           = CLIENT_CLOSE_NOTHING;
}

uint32 CConnectHandler::GetHandlerID()
{
    return m_u4HandlerID;
}

bool CConnectHandler::ServerClose(EM_Client_Close_status emStatus, uint8 u1OptionEvent)
{
    OUR_DEBUG((LM_ERROR, "[CConnectHandler::ServerClose]Close(%d) OK.\n", GetConnectID()));
    //AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);

    if(CLIENT_CLOSE_IMMEDIATLY == emStatus)
    {
        //组织数据
        _MakePacket objMakePacket;

        objMakePacket.m_u4ConnectID       = GetConnectID();
        objMakePacket.m_pPacketParse      = NULL;
        objMakePacket.m_u1Option          = u1OptionEvent;

        //发送客户端链接断开消息。
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();

        if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
        {
            OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
        }

        //msg_queue()->deactivate();
        shutdown();

        ClearPacketParse();

        m_u4ConnectID = 0;

        //回归用过的指针
        App_ConnectHandlerPool::instance()->Delete(this);
    }
    else
    {
        m_emStatus = emStatus;
    }

    return true;
}

void CConnectHandler::SetConnectID(uint32 u4ConnectID)
{
    m_u4ConnectID = u4ConnectID;
}

uint32 CConnectHandler::GetConnectID()
{
    return m_u4ConnectID;
}

int CConnectHandler::open(void*)
{
    //OUR_DEBUG((LM_ERROR, "[CConnectHandler::open](0x%08x),m_nIOCount=%d.\n", this, m_nIOCount));
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadLock);

    m_nIOCount            = 1;
    m_blBlockState        = false;
    m_nBlockCount         = 0;
    m_u8SendQueueTimeCost = 0;
    m_blIsLog             = false;
    m_szConnectName[0]    = '\0';
    m_u1IsActive          = 1;

    //重置缓冲区
    //m_pBlockMessage->reset();

    //获得远程链接地址和端口
    if(this->peer().get_remote_addr(m_addrRemote) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]this->peer().get_remote_addr error.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::open]this->peer().get_remote_addr error.");
        return -1;
    }

    if(App_ForbiddenIP::instance()->CheckIP(m_addrRemote.get_host_addr()) == false)
    {
        //在禁止列表中，不允许访问
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]IP Forbidden(%s).\n", m_addrRemote.get_host_addr()));
        return -1;
    }

    //检查单位时间链接次数是否达到上限
    if(false == App_IPAccount::instance()->AddIP((string)m_addrRemote.get_host_addr()))
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]IP connect frequently.\n", m_addrRemote.get_host_addr()));
        App_ForbiddenIP::instance()->AddTempIP(m_addrRemote.get_host_addr(), App_MainConfig::instance()->GetIPAlert()->m_u4IPTimeout);

        //发送告警邮件
        AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
                                               App_MainConfig::instance()->GetIPAlert()->m_u4MailID,
                                               (char* )"Alert IP",
                                               "[CConnectHandler::open] IP is more than IP Max,");

        return -1;
    }

    //初始化检查器
    m_TimeConnectInfo.Init(App_MainConfig::instance()->GetClientDataAlert()->m_u4RecvPacketCount,
                           App_MainConfig::instance()->GetClientDataAlert()->m_u4RecvDataMax,
                           App_MainConfig::instance()->GetClientDataAlert()->m_u4SendPacketCount,
                           App_MainConfig::instance()->GetClientDataAlert()->m_u4SendDataMax);

    int nRet = 0;
    /*
    int nRet = ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open();
    if(nRet != 0)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open() error [%d].\n", nRet));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::open]ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open() error [%d].", nRet);
        return -1;
    }
    */

    //设置链接为非阻塞模式
    if (this->peer().enable(ACE_NONBLOCK) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]this->peer().enable  = ACE_NONBLOCK error.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::open]this->peer().enable  = ACE_NONBLOCK error.");
        return -1;
    }

    //设置默认别名
    SetConnectName(m_addrRemote.get_host_addr());
    OUR_DEBUG((LM_INFO, "[CConnectHandler::open] Connection from [%s:%d]\n",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number()));

    //初始化当前链接的某些参数
    m_atvConnect          = ACE_OS::gettimeofday();
    m_atvInput            = ACE_OS::gettimeofday();
    m_atvOutput           = ACE_OS::gettimeofday();
    m_atvSendAlive        = ACE_OS::gettimeofday();

    m_u4AllRecvCount      = 0;
    m_u4AllSendCount      = 0;
    m_u4AllRecvSize       = 0;
    m_u4AllSendSize       = 0;
    m_u8RecvQueueTimeCost = 0;
    m_u4RecvQueueCount    = 0;
    m_u8SendQueueTimeCost = 0;
    m_u4CurrSize          = 0;

    m_u4ReadSendSize      = 0;
    m_u4SuccessSendSize   = 0;
    m_emStatus            = CLIENT_CLOSE_NOTHING;

    //设置接收缓冲池的大小
    int nTecvBuffSize = MAX_MSG_SOCKETBUFF;
    //ACE_OS::setsockopt(this->get_handle(), SOL_SOCKET, SO_RCVBUF, (char* )&nTecvBuffSize, sizeof(nTecvBuffSize));
    ACE_OS::setsockopt(this->get_handle(), SOL_SOCKET, SO_SNDBUF, (char* )&nTecvBuffSize, sizeof(nTecvBuffSize));

    if(m_u2TcpNodelay == TCP_NODELAY_OFF)
    {
        //如果设置了禁用Nagle算法，则这里要禁用。
        int nOpt=1;
        ACE_OS::setsockopt(this->get_handle(), IPPROTO_TCP, TCP_NODELAY, (char* )&nOpt, sizeof(int));
    }

    //int nOverTime = MAX_MSG_SENDTIMEOUT;
    //ACE_OS::setsockopt(this->get_handle(), SOL_SOCKET, SO_SNDTIMEO, (char* )&nOverTime, sizeof(nOverTime));

    m_pPacketParse = App_PacketParsePool::instance()->Create();

    if(NULL == m_pPacketParse)
    {
        OUR_DEBUG((LM_DEBUG,"[CConnectHandler::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
        return -1;
    }

    //申请头的大小对应的mb
    if(m_pPacketParse->GetPacketMode() == PACKET_WITHHEAD)
    {
        m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadSrcLen());
    }
    else
    {
        m_pCurrMessage = App_MessageBlockManager::instance()->Create(App_MainConfig::instance()->GetServerRecvBuff());
    }

    if(m_pCurrMessage == NULL)
    {
        //AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open] pmb new is NULL.\n"));

        App_ConnectManager::instance()->Close(GetConnectID());
        return -1;
    }

    //将这个链接放入链接库
    if(false == App_ConnectManager::instance()->AddConnect(this))
    {
        OUR_DEBUG((LM_ERROR, "%s.\n", App_ConnectManager::instance()->GetError()));
        sprintf_safe(m_szError, MAX_BUFF_500, "%s", App_ConnectManager::instance()->GetError());
        return -1;
    }

    AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Connection from [%s:%d].",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number());

    //告诉PacketParse连接应建立
    App_PacketParseLoader::instance()->GetPacketParseInfo()->Connect(GetConnectID(), GetClientIPInfo(), GetLocalIPInfo());

    //组织数据
    _MakePacket objMakePacket;

    objMakePacket.m_u4ConnectID       = GetConnectID();
    objMakePacket.m_pPacketParse      = NULL;
    objMakePacket.m_u1Option          = PACKET_CONNECT;

    //发送链接建立消息。
    ACE_Time_Value tvNow = ACE_OS::gettimeofday();

    if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open] ConnectID=%d, PACKET_CONNECT is error.\n", GetConnectID()));
    }

    m_u1ConnectState = CONNECT_OPEN;

    nRet = this->reactor()->register_handler(this, ACE_Event_Handler::READ_MASK);
    //OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]ConnectID=%d, nRet=%d.\n", GetConnectID(), nRet));

    return nRet;
}

//接受数据
int CConnectHandler::handle_input(ACE_HANDLE fd)
{
    //OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input](0x%08x)ConnectID=%d,m_nIOCount=%d.\n", this, GetConnectID(), m_nIOCount));
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadLock);
    //OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]ConnectID=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));

    m_atvInput = ACE_OS::gettimeofday();

    if(fd == ACE_INVALID_HANDLE)
    {
        m_u4CurrSize = 0;
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]fd == ACE_INVALID_HANDLE.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input]fd == ACE_INVALID_HANDLE.");

        //组织数据
        _MakePacket objMakePacket;

        objMakePacket.m_u4ConnectID       = GetConnectID();
        objMakePacket.m_pPacketParse      = NULL;
        objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

        //发送客户端链接断开消息。
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();

        if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
        {
            OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
        }

        return -1;
    }

    //判断数据包结构是否为NULL
    if(m_pPacketParse == NULL)
    {
        m_u4CurrSize = 0;
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]ConnectID=%d, m_pPacketParse == NULL.\n", GetConnectID()));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input]m_pPacketParse == NULL.");

        //组织数据
        _MakePacket objMakePacket;

        objMakePacket.m_u4ConnectID       = GetConnectID();
        objMakePacket.m_pPacketParse      = NULL;
        objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

        //发送客户端链接断开消息。
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();

        if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
        {
            OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
        }

        return -1;
    }

    //感谢玉白石的建议
    //这里考虑代码的et模式的支持
    if(App_MainConfig::instance()->GetNetworkMode() != (uint8)NETWORKMODE_RE_EPOLL_ET)
    {
        return RecvData();
    }
    else
    {
        return RecvData_et();
    }
}

//剥离接收数据代码
int CConnectHandler::RecvData()
{
    m_ThreadLock.acquire();
    m_nIOCount++;
    m_ThreadLock.release();

    ACE_Time_Value nowait(0, MAX_QUEUE_TIMEOUT);

    //判断缓冲是否为NULL
    if(m_pCurrMessage == NULL)
    {
        m_u4CurrSize = 0;
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData]m_pCurrMessage == NULL.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::RecvData]m_pCurrMessage == NULL.");

        //关闭当前的PacketParse
        ClearPacketParse();

        Close();
        return -1;
    }

    //计算应该接收的数据长度
    int nCurrCount = 0;

    if(m_pPacketParse->GetIsHandleHead())
    {
        nCurrCount = (uint32)m_pPacketParse->GetPacketHeadSrcLen() - m_u4CurrSize;
    }
    else
    {
        nCurrCount = (uint32)m_pPacketParse->GetPacketBodySrcLen() - m_u4CurrSize;
    }

    //这里需要对m_u4CurrSize进行检查。
    if(nCurrCount < 0)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData][%d] nCurrCount < 0 m_u4CurrSize = %d.\n", GetConnectID(), m_u4CurrSize));
        m_u4CurrSize = 0;

        //关闭当前的PacketParse
        ClearPacketParse();

        Close();
        return -1;
    }

    int nDataLen = this->peer().recv(m_pCurrMessage->wr_ptr(), nCurrCount, MSG_NOSIGNAL, &nowait);

    if(nDataLen <= 0)
    {
        m_u4CurrSize = 0;
        uint32 u4Error = (uint32)errno;
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData] ConnectID=%d, recv data is error nDataLen = [%d] errno = [%d].\n", GetConnectID(), nDataLen, u4Error));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::RecvData] ConnectID = %d, recv data is error[%d].\n", GetConnectID(), nDataLen);

        //关闭当前的PacketParse
        ClearPacketParse();

        Close();
        return -1;
    }

    //如果是DEBUG状态，记录当前接受包的二进制数据
    if(App_MainConfig::instance()->GetDebug() == DEBUG_ON || m_blIsLog == true)
    {
        char szDebugData[MAX_BUFF_1024] = {'\0'};
        char szLog[10]  = {'\0'};
        int  nDebugSize = 0;
        bool blblMore   = false;

        if(nDataLen >= MAX_BUFF_200)
        {
            nDebugSize = MAX_BUFF_200;
            blblMore   = true;
        }
        else
        {
            nDebugSize = nDataLen;
        }

        char* pData = m_pCurrMessage->wr_ptr();

        for(int i = 0; i < nDebugSize; i++)
        {
            sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
            sprintf_safe(szDebugData + 5*i, MAX_BUFF_1024 - 5*i, "%s", szLog);
        }

        if(blblMore == true)
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.(数据包过长只记录前200字节)", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
        else
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
    }

    m_u4CurrSize += nDataLen;

    m_pCurrMessage->wr_ptr(nDataLen);

    if(m_pPacketParse->GetPacketMode() == PACKET_WITHHEAD)
    {
        //如果没有读完，短读
        if(nCurrCount - nDataLen > 0)
        {
            Close();
            return 0;
        }
        else if(m_pCurrMessage->length() == m_pPacketParse->GetPacketHeadSrcLen() && m_pPacketParse->GetIsHandleHead())
        {
            _Head_Info objHeadInfo;
            bool blStateHead = App_PacketParseLoader::instance()->GetPacketParseInfo()->Parse_Packet_Head_Info(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance(), &objHeadInfo);

            if(false == blStateHead)
            {
                m_u4CurrSize = 0;
                OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData]SetPacketHead is false.\n"));

                //关闭当前的PacketParse
                ClearPacketParse();

                Close();
                return -1;
            }
            else
            {
                m_pPacketParse->SetPacket_IsHandleHead(false);
                m_pPacketParse->SetPacket_Head_Message(objHeadInfo.m_pmbHead);
                m_pPacketParse->SetPacket_Head_Curr_Length(objHeadInfo.m_u4HeadCurrLen);
                m_pPacketParse->SetPacket_Body_Src_Length(objHeadInfo.m_u4BodySrcLen);
                m_pPacketParse->SetPacket_CommandID(objHeadInfo.m_u2PacketCommandID);
            }

            uint32 u4PacketBodyLen = m_pPacketParse->GetPacketBodySrcLen();
            m_u4CurrSize = 0;


            //这里添加只处理包头的数据
            //如果数据只有包头，不需要包体，在这里必须做一些处理，让数据只处理包头就扔到DoMessage()
            if(u4PacketBodyLen == 0)
            {
                //只有数据包头
                if(false == CheckMessage())
                {
                    Close();
                    return -1;
                }

                m_u4CurrSize = 0;

                //申请新的包
                m_pPacketParse = App_PacketParsePool::instance()->Create();

                if(NULL == m_pPacketParse)
                {
                    OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::RecvData] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
                    Close();
                    return -1;
                }

                //申请头的大小对应的mb
                m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadSrcLen());

                if(m_pCurrMessage == NULL)
                {
                    AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
                    OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData] pmb new is NULL.\n"));

                    //组织数据
                    _MakePacket objMakePacket;

                    objMakePacket.m_u4ConnectID       = GetConnectID();
                    objMakePacket.m_pPacketParse      = NULL;
                    objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

                    //发送客户端链接断开消息。
                    ACE_Time_Value tvNow = ACE_OS::gettimeofday();

                    if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
                    {
                        OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                    }

                    Close();
                    return -1;
                }
            }
            else
            {
                //如果超过了最大包长度，为非法数据
                if(u4PacketBodyLen >= m_u4MaxPacketSize)
                {
                    m_u4CurrSize = 0;
                    OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData]u4PacketHeadLen(%d) more than %d.\n", u4PacketBodyLen, m_u4MaxPacketSize));

                    //关闭当前的PacketParse
                    ClearPacketParse();

                    Close();
                    return -1;
                }
                else
                {
                    //OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] m_pPacketParse->GetPacketBodyLen())=%d.\n", m_pPacketParse->GetPacketBodyLen()));
                    //申请头的大小对应的mb
                    m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketBodySrcLen());

                    if(m_pCurrMessage == NULL)
                    {
                        m_u4CurrSize = 0;
                        //AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);
                        OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData] pmb new is NULL.\n"));

                        //关闭当前的PacketParse
                        ClearPacketParse();

                        Close();
                        return -1;
                    }
                }
            }

        }
        else
        {
            //接受完整数据完成，开始分析完整数据包
            _Body_Info obj_Body_Info;
            bool blStateBody = App_PacketParseLoader::instance()->GetPacketParseInfo()->Parse_Packet_Body_Info(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance(), &obj_Body_Info);

            if(false == blStateBody)
            {
                //如果数据包体是错误的，则断开连接
                m_u4CurrSize = 0;
                OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData]SetPacketBody is false.\n"));

                //关闭当前的PacketParse
                ClearPacketParse();

                Close();
                return -1;
            }
            else
            {
                m_pPacketParse->SetPacket_Body_Message(obj_Body_Info.m_pmbBody);
                m_pPacketParse->SetPacket_Body_Curr_Length(obj_Body_Info.m_u4BodyCurrLen);

                if(obj_Body_Info.m_u2PacketCommandID > 0)
                {
                    m_pPacketParse->SetPacket_CommandID(obj_Body_Info.m_u2PacketCommandID);
                }
            }

            if(false == CheckMessage())
            {
                Close();
                return -1;
            }

            m_u4CurrSize = 0;

            //申请新的包
            m_pPacketParse = App_PacketParsePool::instance()->Create();

            if(NULL == m_pPacketParse)
            {
                OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::RecvData] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
                Close();
                return -1;
            }

            //申请头的大小对应的mb
            m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadSrcLen());

            if(m_pCurrMessage == NULL)
            {
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
                OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData] pmb new is NULL.\n"));

                //组织数据
                _MakePacket objMakePacket;

                objMakePacket.m_u4ConnectID       = GetConnectID();
                objMakePacket.m_pPacketParse      = NULL;
                objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

                //发送客户端链接断开消息。
                ACE_Time_Value tvNow = ACE_OS::gettimeofday();

                if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
                {
                    OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                }

                Close();
                return -1;
            }
        }
    }
    else
    {
        //以流模式解析
        while(true)
        {
            _Packet_Info obj_Packet_Info;
            uint8 n1Ret = App_PacketParseLoader::instance()->GetPacketParseInfo()->Parse_Packet_Stream(GetConnectID(), m_pCurrMessage, (IMessageBlockManager* )App_MessageBlockManager::instance(), &obj_Packet_Info);

            if(PACKET_GET_ENOUGTH == n1Ret)
            {
                m_pPacketParse->SetPacket_Head_Message(obj_Packet_Info.m_pmbHead);
                m_pPacketParse->SetPacket_Body_Message(obj_Packet_Info.m_pmbBody);
                m_pPacketParse->SetPacket_CommandID(obj_Packet_Info.m_u2PacketCommandID);
                m_pPacketParse->SetPacket_Head_Src_Length(obj_Packet_Info.m_u4HeadSrcLen);
                m_pPacketParse->SetPacket_Head_Curr_Length(obj_Packet_Info.m_u4HeadCurrLen);
                m_pPacketParse->SetPacket_Head_Src_Length(obj_Packet_Info.m_u4BodySrcLen);
                m_pPacketParse->SetPacket_Body_Curr_Length(obj_Packet_Info.m_u4BodyCurrLen);

                if(false == CheckMessage())
                {
                    Close();
                    return -1;
                }

                m_u4CurrSize = 0;

                //申请新的包
                m_pPacketParse = App_PacketParsePool::instance()->Create();

                if(NULL == m_pPacketParse)
                {
                    OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::RecvData] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
                    Close();
                    return -1;
                }

                //看看是否接收完成了
                if(m_pCurrMessage->length() == 0)
                {
                    break;
                }
                else
                {
                    //还有数据，继续分析
                    continue;
                }

            }
            else if(PACKET_GET_NO_ENOUGTH == n1Ret)
            {
                break;
            }
            else
            {
                m_pPacketParse->Clear();

                AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
                OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData] pmb new is NULL.\n"));

                //组织数据
                _MakePacket objMakePacket;

                objMakePacket.m_u4ConnectID       = GetConnectID();
                objMakePacket.m_pPacketParse      = NULL;
                objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

                //发送客户端链接断开消息。
                ACE_Time_Value tvNow = ACE_OS::gettimeofday();

                if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
                {
                    OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                }

                Close();
                return -1;
            }
        }

        App_MessageBlockManager::instance()->Close(m_pCurrMessage);
        m_u4CurrSize = 0;

        //申请头的大小对应的mb
        m_pCurrMessage = App_MessageBlockManager::instance()->Create(App_MainConfig::instance()->GetServerRecvBuff());

        if(m_pCurrMessage == NULL)
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
            OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData] pmb new is NULL.\n"));

            //组织数据
            _MakePacket objMakePacket;

            objMakePacket.m_u4ConnectID       = GetConnectID();
            objMakePacket.m_pPacketParse      = NULL;
            objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

            //发送客户端链接断开消息。
            ACE_Time_Value tvNow = ACE_OS::gettimeofday();

            if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
            {
                OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
            }

            Close();
            return -1;
        }
    }

    Close();
    return 0;
}

//et模式接收数据
int CConnectHandler::RecvData_et()
{
    m_ThreadLock.acquire();
    m_nIOCount++;
    m_ThreadLock.release();

    while(true)
    {
        //OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData_et]m_nIOCount=%d.\n", m_nIOCount));

        //判断缓冲是否为NULL
        if(m_pCurrMessage == NULL)
        {
            m_u4CurrSize = 0;
            OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData_et]m_pCurrMessage == NULL.\n"));
            sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::RecvData_et]m_pCurrMessage == NULL.");

            //关闭当前的PacketParse
            ClearPacketParse();

            Close();
            return -1;
        }

        //计算应该接收的数据长度
        int nCurrCount = 0;

        if(m_pPacketParse->GetIsHandleHead())
        {
            nCurrCount = (uint32)m_pPacketParse->GetPacketHeadSrcLen() - m_u4CurrSize;
        }
        else
        {
            nCurrCount = (uint32)m_pPacketParse->GetPacketBodySrcLen() - m_u4CurrSize;
        }

        //这里需要对m_u4CurrSize进行检查。
        if(nCurrCount < 0)
        {
            OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData_et][%d] nCurrCount < 0 m_u4CurrSize = %d.\n", GetConnectID(), m_u4CurrSize));
            m_u4CurrSize = 0;

            //关闭当前的PacketParse
            ClearPacketParse();

            Close();

            return -1;
        }

        int nDataLen = this->peer().recv(m_pCurrMessage->wr_ptr(), nCurrCount, MSG_NOSIGNAL);

        //OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input] ConnectID=%d, GetData=[%d],errno=[%d].\n", GetConnectID(), nDataLen, errno));
        if(nDataLen <= 0)
        {
            m_u4CurrSize = 0;
            uint32 u4Error = (uint32)errno;

            //如果是-1 且为11的错误，忽略之
            if(nDataLen == -1 && u4Error == EAGAIN)
            {
                break;
            }

            OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData_et] ConnectID = %d, recv data is error nDataLen = [%d] errno = [%d] EAGAIN=[%d].\n", GetConnectID(), nDataLen, u4Error, EAGAIN));
            sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::RecvData_et] ConnectID = %d,nDataLen = [%d],recv data is error[%d].\n", GetConnectID(), nDataLen, u4Error);

            //关闭当前的PacketParse
            ClearPacketParse();

            Close();
            return -1;
        }

        //如果是DEBUG状态，记录当前接受包的二进制数据
        if(App_MainConfig::instance()->GetDebug() == DEBUG_ON || m_blIsLog == true)
        {
            char szDebugData[MAX_BUFF_1024] = {'\0'};
            char szLog[10]  = {'\0'};
            int  nDebugSize = 0;
            bool blblMore   = false;

            if(nDataLen >= MAX_BUFF_200)
            {
                nDebugSize = MAX_BUFF_200;
                blblMore   = true;
            }
            else
            {
                nDebugSize = nDataLen;
            }

            char* pData = m_pCurrMessage->wr_ptr();

            for(int i = 0; i < nDebugSize; i++)
            {
                sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
                sprintf_safe(szDebugData + 5*i, MAX_BUFF_1024 - 5*i, "%s", szLog);
            }

            if(blblMore == true)
            {
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.(数据包过长只记录前200字节)", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
            }
            else
            {
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
            }
        }

        m_u4CurrSize += nDataLen;

        m_pCurrMessage->wr_ptr(nDataLen);

        if(m_pPacketParse->GetPacketMode() == PACKET_WITHHEAD)
        {
            //如果没有读完，短读
            if(nCurrCount - nDataLen > 0)
            {
                Close();
                return 0;
            }
            else if(m_pCurrMessage->length() == m_pPacketParse->GetPacketHeadSrcLen() && m_pPacketParse->GetIsHandleHead())
            {
                _Head_Info objHeadInfo;
                bool blStateHead = App_PacketParseLoader::instance()->GetPacketParseInfo()->Parse_Packet_Head_Info(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance(), &objHeadInfo);

                if(false == blStateHead)
                {
                    m_u4CurrSize = 0;
                    OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData_et]SetPacketHead is false.\n"));

                    //关闭当前的PacketParse
                    ClearPacketParse();

                    Close();
                    return -1;
                }
                else
                {
                    m_pPacketParse->SetPacket_IsHandleHead(false);
                    m_pPacketParse->SetPacket_Head_Message(objHeadInfo.m_pmbHead);
                    m_pPacketParse->SetPacket_Head_Curr_Length(objHeadInfo.m_u4HeadCurrLen);
                    m_pPacketParse->SetPacket_Body_Src_Length(objHeadInfo.m_u4BodySrcLen);
                    m_pPacketParse->SetPacket_CommandID(objHeadInfo.m_u2PacketCommandID);
                }

                uint32 u4PacketBodyLen = m_pPacketParse->GetPacketBodySrcLen();
                m_u4CurrSize = 0;


                //这里添加只处理包头的数据
                //如果数据只有包头，不需要包体，在这里必须做一些处理，让数据只处理包头就扔到DoMessage()
                if(u4PacketBodyLen == 0)
                {
                    //只有数据包头
                    if(false == CheckMessage())
                    {
                        Close();
                        return -1;
                    }

                    m_u4CurrSize = 0;

                    //申请新的包
                    m_pPacketParse = App_PacketParsePool::instance()->Create();

                    if(NULL == m_pPacketParse)
                    {
                        OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::RecvData_et] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
                        Close();
                        return -1;
                    }

                    //申请头的大小对应的mb
                    m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadSrcLen());

                    if(m_pCurrMessage == NULL)
                    {
                        AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
                        OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData_et] pmb new is NULL.\n"));

                        //组织数据
                        _MakePacket objMakePacket;

                        objMakePacket.m_u4ConnectID       = GetConnectID();
                        objMakePacket.m_pPacketParse      = NULL;
                        objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

                        //发送客户端链接断开消息。
                        ACE_Time_Value tvNow = ACE_OS::gettimeofday();

                        if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
                        {
                            OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData_et] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                        }

                        Close();
                        return -1;
                    }
                }
                else
                {
                    //如果超过了最大包长度，为非法数据
                    if(u4PacketBodyLen >= m_u4MaxPacketSize)
                    {
                        m_u4CurrSize = 0;
                        OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData_et]u4PacketHeadLen(%d) more than %d.\n", u4PacketBodyLen, m_u4MaxPacketSize));

                        Close();
                        //关闭当前的PacketParse
                        ClearPacketParse();

                        return -1;
                    }
                    else
                    {
                        //OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] m_pPacketParse->GetPacketBodyLen())=%d.\n", m_pPacketParse->GetPacketBodyLen()));
                        //申请头的大小对应的mb
                        m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketBodySrcLen());

                        if(m_pCurrMessage == NULL)
                        {
                            m_u4CurrSize = 0;
                            //AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);
                            OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData_et] pmb new is NULL.\n"));

                            Close();
                            //关闭当前的PacketParse
                            ClearPacketParse();

                            return -1;
                        }
                    }
                }
            }
            else
            {
                //接受完整数据完成，开始分析完整数据包
                _Body_Info obj_Body_Info;
                bool blStateBody = App_PacketParseLoader::instance()->GetPacketParseInfo()->Parse_Packet_Body_Info(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance(), &obj_Body_Info);

                if(false == blStateBody)
                {
                    //如果数据包体是错误的，则断开连接
                    m_u4CurrSize = 0;
                    OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData_et]SetPacketBody is false.\n"));

                    Close();
                    //关闭当前的PacketParse
                    ClearPacketParse();

                    return -1;
                }
                else
                {
                    m_pPacketParse->SetPacket_Body_Message(obj_Body_Info.m_pmbBody);
                    m_pPacketParse->SetPacket_Body_Curr_Length(obj_Body_Info.m_u4BodyCurrLen);

                    if(obj_Body_Info.m_u2PacketCommandID > 0)
                    {
                        m_pPacketParse->SetPacket_CommandID(obj_Body_Info.m_u2PacketCommandID);
                    }
                }

                if(false == CheckMessage())
                {
                    Close();
                    return -1;
                }

                m_u4CurrSize = 0;

                //申请新的包
                m_pPacketParse = App_PacketParsePool::instance()->Create();

                if(NULL == m_pPacketParse)
                {
                    OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::RecvData_et] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
                    Close();
                    return -1;
                }

                //申请头的大小对应的mb
                m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadSrcLen());

                if(m_pCurrMessage == NULL)
                {
                    AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
                    OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData_et] pmb new is NULL.\n"));

                    //组织数据
                    _MakePacket objMakePacket;

                    objMakePacket.m_u4ConnectID       = GetConnectID();
                    objMakePacket.m_pPacketParse      = NULL;
                    objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

                    //发送客户端链接断开消息。
                    ACE_Time_Value tvNow = ACE_OS::gettimeofday();

                    if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
                    {
                        OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData_et] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                    }

                    Close();
                    return -1;
                }
            }
        }
        else
        {
            //以流模式解析
            while(true)
            {
                _Packet_Info obj_Packet_Info;
                uint8 n1Ret = App_PacketParseLoader::instance()->GetPacketParseInfo()->Parse_Packet_Stream(GetConnectID(), m_pCurrMessage, (IMessageBlockManager* )App_MessageBlockManager::instance(), &obj_Packet_Info);

                if(PACKET_GET_ENOUGTH == n1Ret)
                {
                    m_pPacketParse->SetPacket_Head_Message(obj_Packet_Info.m_pmbHead);
                    m_pPacketParse->SetPacket_Body_Message(obj_Packet_Info.m_pmbBody);
                    m_pPacketParse->SetPacket_CommandID(obj_Packet_Info.m_u2PacketCommandID);
                    m_pPacketParse->SetPacket_Head_Src_Length(obj_Packet_Info.m_u4HeadSrcLen);
                    m_pPacketParse->SetPacket_Head_Curr_Length(obj_Packet_Info.m_u4HeadCurrLen);
                    m_pPacketParse->SetPacket_Head_Src_Length(obj_Packet_Info.m_u4BodySrcLen);
                    m_pPacketParse->SetPacket_Body_Curr_Length(obj_Packet_Info.m_u4BodyCurrLen);

                    if(false == CheckMessage())
                    {
                        Close();
                        return -1;
                    }

                    m_u4CurrSize = 0;

                    //申请新的包
                    m_pPacketParse = App_PacketParsePool::instance()->Create();

                    if(NULL == m_pPacketParse)
                    {
                        OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::RecvData_et] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
                        return -1;
                    }

                    //看看是否接收完成了
                    if(m_pCurrMessage->length() == 0)
                    {
                        break;
                    }
                    else
                    {
                        //还有数据，继续分析
                        continue;
                    }

                }
                else if(PACKET_GET_NO_ENOUGTH == n1Ret)
                {
                    break;
                }
                else
                {
                    m_pPacketParse->Clear();

                    AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
                    OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData_et] pmb new is NULL.\n"));

                    //组织数据
                    _MakePacket objMakePacket;

                    objMakePacket.m_u4ConnectID       = GetConnectID();
                    objMakePacket.m_pPacketParse      = NULL;
                    objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

                    //发送客户端链接断开消息。
                    ACE_Time_Value tvNow = ACE_OS::gettimeofday();

                    if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
                    {
                        OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData_et] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                    }

                    Close();
                    return -1;
                }
            }

            App_MessageBlockManager::instance()->Close(m_pCurrMessage);
            m_u4CurrSize = 0;

            //申请头的大小对应的mb
            m_pCurrMessage = App_MessageBlockManager::instance()->Create(App_MainConfig::instance()->GetServerRecvBuff());

            if(m_pCurrMessage == NULL)
            {
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
                OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvData_et] pmb new is NULL.\n"));

                //组织数据
                _MakePacket objMakePacket;

                objMakePacket.m_u4ConnectID       = GetConnectID();
                objMakePacket.m_pPacketParse      = NULL;
                objMakePacket.m_u1Option          = PACKET_CDISCONNECT;

                //发送客户端链接断开消息。
                ACE_Time_Value vtNow = ACE_OS::gettimeofday();

                if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, vtNow))
                {
                    OUR_DEBUG((LM_ERROR, "[CProConnectHandle::RecvData_et] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                }

                Close();
                return -1;
            }
        }
    }

    Close();
    return 0;
}

//关闭链接
int CConnectHandler::handle_close(ACE_HANDLE h, ACE_Reactor_Mask mask)
{
    if(h == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_DEBUG,"[CConnectHandler::handle_close] h is NULL mask=%d.\n", (int)mask));
    }

    //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::handle_close]Connectid=[%d] begin(%d)...\n",GetConnectID(), errno));
    //App_ConnectManager::instance()->Close(GetConnectID());
    //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::handle_close] Connectid=[%d] finish ok...\n", GetConnectID()));
    Close(2);

    return 0;
}

bool CConnectHandler::CheckAlive(ACE_Time_Value& tvNow)
{
    //ACE_Time_Value tvNow = ACE_OS::gettimeofday();
    ACE_Time_Value tvIntval(tvNow - m_atvInput);

    if(tvIntval.sec() > m_u2MaxConnectTime)
    {
        //如果超过了最大时间，则服务器关闭链接
        OUR_DEBUG ((LM_ERROR, "[CConnectHandle::CheckAlive] Connectid=%d Server Close!\n", GetConnectID()));
        return false;
    }
    else
    {
        return true;
    }
}

bool CConnectHandler::SetRecvQueueTimeCost(uint32 u4TimeCost)
{
    //如果超过阀值，则记录到日志中去
    if((uint32)(m_u8RecvQueueTimeout * 1000) <= u4TimeCost)
    {
        AppLogManager::instance()->WriteLog(LOG_SYSTEM_RECVQUEUEERROR, "[TCP]IP=%s,Prot=%d, m_u8RecvQueueTimeout=[%d], Timeout=[%d].", GetClientIPInfo().m_szClientIP, GetClientIPInfo().m_nPort, (uint32)m_u8RecvQueueTimeout, u4TimeCost);
    }

    m_u4RecvQueueCount++;

    return true;
}

bool CConnectHandler::SetSendQueueTimeCost(uint32 u4TimeCost)
{
    //如果超过阀值，则记录到日志中去
    if((uint32)(m_u8SendQueueTimeout) <= u4TimeCost)
    {
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();
        AppLogManager::instance()->WriteLog(LOG_SYSTEM_SENDQUEUEERROR, "[TCP]IP=%s,Prot=%d,m_u8SendQueueTimeout = [%d], Timeout=[%d].", GetClientIPInfo().m_szClientIP, GetClientIPInfo().m_nPort, (uint32)m_u8SendQueueTimeout, u4TimeCost);

        //组织数据
        _MakePacket objMakePacket;

        objMakePacket.m_u4ConnectID       = GetConnectID();
        objMakePacket.m_pPacketParse      = NULL;
        objMakePacket.m_u1Option          = PACKET_SEND_TIMEOUT;

        //告诉插件连接发送超时阀值报警
        if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
        {
            OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
        }
    }

    return true;
}

uint8 CConnectHandler::GetConnectState()
{
    return m_u1ConnectState;
}

uint8 CConnectHandler::GetSendBuffState()
{
    return m_u1SendBuffState;
}

bool CConnectHandler::SendMessage(uint16 u2CommandID, IBuffPacket* pBuffPacket, uint8 u1State, uint8 u1SendType, uint32& u4PacketSize, bool blDelete, int nMessageID)
{
    //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage](0x%08x) Connectid=%d,m_nIOCount=%d.\n", this, GetConnectID(), m_nIOCount));
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadLock);
    //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage]222 Connectid=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));

    //如果当前连接已被别的线程关闭，则这里不做处理，直接退出
    if(m_u1IsActive == 0)
    {
        ACE_Message_Block* pSendMessage = App_MessageBlockManager::instance()->Create(pBuffPacket->GetPacketLen());
        memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), (char* )pSendMessage->wr_ptr(), pBuffPacket->GetPacketLen());
        pSendMessage->wr_ptr(pBuffPacket->GetPacketLen());
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();
        App_MakePacket::instance()->PutSendErrorMessage(0, pSendMessage, tvNow);

        if(blDelete == true)
        {
            App_BuffPacketManager::instance()->Delete(pBuffPacket);
        }

        return false;
    }

    uint32 u4SendSuc = pBuffPacket->GetPacketLen();

    ACE_Message_Block* pMbData = NULL;

    //如果不是直接发送数据，则拼接数据包
    if(u1State == PACKET_SEND_CACHE)
    {
        //先判断要发送的数据长度，看看是否可以放入缓冲，缓冲是否已经放满。
        uint32 u4SendPacketSize = 0;

        if(u1SendType == SENDMESSAGE_NOMAL)
        {
            u4SendPacketSize = App_PacketParseLoader::instance()->GetPacketParseInfo()->Make_Send_Packet_Length(GetConnectID(), pBuffPacket->GetPacketLen(), u2CommandID);
        }
        else
        {
            u4SendPacketSize = (uint32)pBuffPacket->GetPacketLen();
        }

        u4PacketSize = u4SendPacketSize;

        if(u4SendPacketSize + (uint32)m_pBlockMessage->length() >= m_u4SendMaxBuffSize)
        {
            OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage] Connectid=[%d] m_pBlockMessage is not enougth.\n", GetConnectID()));
            ACE_Message_Block* pSendMessage = App_MessageBlockManager::instance()->Create(pBuffPacket->GetPacketLen());
            memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), (char* )pSendMessage->wr_ptr(), pBuffPacket->GetPacketLen());
            pSendMessage->wr_ptr(pBuffPacket->GetPacketLen());
            ACE_Time_Value tvNow = ACE_OS::gettimeofday();
            App_MakePacket::instance()->PutSendErrorMessage(0, pSendMessage, tvNow);

            if(blDelete == true)
            {
                App_BuffPacketManager::instance()->Delete(pBuffPacket);
            }

            return false;
        }
        else
        {
            //添加进缓冲区
            //ACE_Message_Block* pMbBufferData = NULL;

            //SENDMESSAGE_NOMAL是需要包头的时候，否则，不组包直接发送
            if(u1SendType == SENDMESSAGE_NOMAL)
            {
                //这里组成返回数据包
                App_PacketParseLoader::instance()->GetPacketParseInfo()->Make_Send_Packet(GetConnectID(), pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), m_pBlockMessage, u2CommandID);
            }
            else
            {
                //如果不是SENDMESSAGE_NOMAL，则直接组包
                memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), m_pBlockMessage->wr_ptr(), pBuffPacket->GetPacketLen());
                m_pBlockMessage->wr_ptr(pBuffPacket->GetPacketLen());
            }
        }

        if(blDelete == true)
        {
            //删除发送数据包
            App_BuffPacketManager::instance()->Delete(pBuffPacket);
        }

        //放入完成，从这里退出
        return true;
    }
    else
    {
        //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage]Connectid=%d,333.\n", GetConnectID()));
        //先判断是否要组装包头，如果需要，则组装在m_pBlockMessage中
        uint32 u4SendPacketSize = 0;

        if(u1SendType == SENDMESSAGE_NOMAL)
        {
            u4SendPacketSize = App_PacketParseLoader::instance()->GetPacketParseInfo()->Make_Send_Packet_Length(GetConnectID(), pBuffPacket->GetPacketLen(), u2CommandID);

            if(u4SendPacketSize >= m_u4SendMaxBuffSize)
            {
                OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage](%d) u4SendPacketSize is more than(%d)(%d).\n", GetConnectID(), u4SendPacketSize, m_u4SendMaxBuffSize));
                ACE_Message_Block* pSendMessage = App_MessageBlockManager::instance()->Create(pBuffPacket->GetPacketLen());
                memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), (char* )pSendMessage->wr_ptr(), pBuffPacket->GetPacketLen());
                pSendMessage->wr_ptr(pBuffPacket->GetPacketLen());
                ACE_Time_Value tvNow = ACE_OS::gettimeofday();
                App_MakePacket::instance()->PutSendErrorMessage(0, pSendMessage, tvNow);

                if(blDelete == true)
                {
                    //删除发送数据包
                    App_BuffPacketManager::instance()->Delete(pBuffPacket);
                }

                return false;
            }

            //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage] Connectid=[%d] aaa m_pBlockMessage=0x%08x.\n", GetConnectID(), m_pBlockMessage));
            App_PacketParseLoader::instance()->GetPacketParseInfo()->Make_Send_Packet(GetConnectID(), pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), m_pBlockMessage, u2CommandID);
            //这里MakePacket已经加了数据长度，所以在这里不再追加
        }
        else
        {
            u4SendPacketSize = (uint32)pBuffPacket->GetPacketLen();

            if(u4SendPacketSize >= m_u4SendMaxBuffSize)
            {
                OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage](%d) u4SendPacketSize is more than(%d)(%d).\n", GetConnectID(), u4SendPacketSize, m_u4SendMaxBuffSize));
                ACE_Message_Block* pSendMessage = App_MessageBlockManager::instance()->Create(pBuffPacket->GetPacketLen());
                memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), (char* )pSendMessage->wr_ptr(), pBuffPacket->GetPacketLen());
                pSendMessage->wr_ptr(pBuffPacket->GetPacketLen());
                ACE_Time_Value tvNow = ACE_OS::gettimeofday();
                App_MakePacket::instance()->PutSendErrorMessage(0, pSendMessage, tvNow);

                if(blDelete == true)
                {
                    //删除发送数据包
                    App_BuffPacketManager::instance()->Delete(pBuffPacket);
                }

                return false;
            }

            //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage] Connectid=[%d] aaa m_pBlockMessage=0x%08x.\n", GetConnectID(), m_pBlockMessage));
            memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), m_pBlockMessage->wr_ptr(), pBuffPacket->GetPacketLen());
            m_pBlockMessage->wr_ptr(pBuffPacket->GetPacketLen());
        }

        //如果之前有缓冲数据，则和缓冲数据一起发送
        u4PacketSize = m_pBlockMessage->length();

        //这里肯定会大于0
        if(m_pBlockMessage->length() > 0)
        {
            //因为是异步发送，发送的数据指针不可以立刻释放，所以需要在这里创建一个新的发送数据块，将数据考入
            pMbData = App_MessageBlockManager::instance()->Create((uint32)m_pBlockMessage->length());

            if(NULL == pMbData)
            {
                OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage] Connectid=[%d] pMbData is NULL.\n", GetConnectID()));
                ACE_Message_Block* pSendMessage = App_MessageBlockManager::instance()->Create(pBuffPacket->GetPacketLen());
                memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), (char* )pSendMessage->wr_ptr(), pBuffPacket->GetPacketLen());
                pSendMessage->wr_ptr(pBuffPacket->GetPacketLen());
                ACE_Time_Value tvNow = ACE_OS::gettimeofday();
                App_MakePacket::instance()->PutSendErrorMessage(0, pSendMessage, tvNow);

                if(blDelete == true)
                {
                    //删除发送数据包
                    App_BuffPacketManager::instance()->Delete(pBuffPacket);
                }

                return false;
            }

            //OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage] Connectid=[%d] m_pBlockMessage=0x%08x.\n", GetConnectID(), m_pBlockMessage));
            memcpy_safe(m_pBlockMessage->rd_ptr(), m_pBlockMessage->length(), pMbData->wr_ptr(), m_pBlockMessage->length());
            pMbData->wr_ptr(m_pBlockMessage->length());
            //放入完成，则清空缓存数据，使命完成
            m_pBlockMessage->reset();
        }

        if(blDelete == true)
        {
            //删除发送数据包
            App_BuffPacketManager::instance()->Delete(pBuffPacket);
        }

        //如果需要发送完成后删除，则配置标记位
        if(PACKET_SEND_FIN_CLOSE == u1State)
        {
            m_emStatus = CLIENT_CLOSE_SENDOK;
        }

        //将消息ID放入MessageBlock
        ACE_Message_Block::ACE_Message_Type  objType  = ACE_Message_Block::MB_USER + nMessageID;
        pMbData->msg_type(objType);

        bool blRet = PutSendPacket(pMbData);

        if(true == blRet)
        {
            //记录成功发送字节
            m_u4SuccessSendSize += u4SendSuc;
        }

        return blRet;
    }
}

bool CConnectHandler::PutSendPacket(ACE_Message_Block* pMbData)
{
    if(NULL == pMbData)
    {
        return false;
    }

    //如果是DEBUG状态，记录当前发送包的二进制数据
    if(App_MainConfig::instance()->GetDebug() == DEBUG_ON || m_blIsLog == true)
    {
        char szDebugData[MAX_BUFF_1024] = {'\0'};
        char szLog[10]  = {'\0'};
        int  nDebugSize = 0;
        bool blblMore   = false;

        if(pMbData->length() >= MAX_BUFF_200)
        {
            nDebugSize = MAX_BUFF_200;
            blblMore   = true;
        }
        else
        {
            nDebugSize = (int)pMbData->length();
        }

        char* pData = pMbData->rd_ptr();

        for(int i = 0; i < nDebugSize; i++)
        {
            sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
            sprintf_safe(szDebugData + 5*i, MAX_BUFF_1024 - 5*i, "%s", szLog);
        }

        if(blblMore == true)
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTSEND, "[(%s)%s:%d]%s.(数据包过长只记录前200字节)", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
        else
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTSEND, "[(%s)%s:%d]%s.", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
    }

    //统计发送数量
    ACE_Date_Time dtNow;

    if(false == m_TimeConnectInfo.SendCheck((uint8)dtNow.minute(), 1, pMbData->length()))
    {
        //超过了限定的阀值，需要关闭链接，并记录日志
        AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECTABNORMAL,
                                               App_MainConfig::instance()->GetClientDataAlert()->m_u4MailID,
                                               (char* )"Alert",
                                               "[TCP]IP=%s,Prot=%d,SendPacketCount=%d, SendSize=%d.",
                                               m_addrRemote.get_host_addr(),
                                               m_addrRemote.get_port_number(),
                                               m_TimeConnectInfo.m_u4SendPacketCount,
                                               m_TimeConnectInfo.m_u4SendSize);

        //设置封禁时间
        App_ForbiddenIP::instance()->AddTempIP(m_addrRemote.get_host_addr(), App_MainConfig::instance()->GetIPAlert()->m_u4IPTimeout);
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::PutSendPacket] ConnectID = %d, Send Data is more than limit.\n", GetConnectID()));

        ACE_Time_Value tvNow = ACE_OS::gettimeofday();
        App_MakePacket::instance()->PutSendErrorMessage(GetConnectID(), pMbData, tvNow);
        //App_MessageBlockManager::instance()->Close(pMbData);

        return false;
    }

    //发送超时时间设置
    ACE_Time_Value  nowait(0, m_u4SendThresHold*MAX_BUFF_1000);

    if(NULL == pMbData)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::PutSendPacket] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetConnectID()));
        return false;
    }

    if(get_handle() == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::PutSendPacket] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetConnectID()));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::PutSendPacket] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetConnectID());
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();
        App_MakePacket::instance()->PutSendErrorMessage(GetConnectID(), pMbData, tvNow);
        App_MessageBlockManager::instance()->Close(pMbData);
        return false;
    }

    //发送数据
    int nSendPacketLen = (int)pMbData->length();
    int nIsSendSize    = 0;

    //循环发送，直到数据发送完成。
    while(true)
    {
        if(nSendPacketLen <= 0)
        {
            OUR_DEBUG((LM_ERROR, "[CConnectHandler::PutSendPacket] ConnectID = %d, nCurrSendSize error is %d.\n", GetConnectID(), nSendPacketLen));
            App_MessageBlockManager::instance()->Close(pMbData);
            return false;
        }

        int nDataLen = this->peer().send(pMbData->rd_ptr(), nSendPacketLen - nIsSendSize, &nowait);

        if(nDataLen <= 0)
        {
            int nErrno = errno;
            OUR_DEBUG((LM_ERROR, "[CConnectHandler::PutSendPacket] ConnectID = %d, error = %d.\n", GetConnectID(), nErrno));

            AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "WriteError [%s:%d] nErrno = %d  result.bytes_transferred() = %d, ",
                                                m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), nErrno,
                                                nIsSendSize);
            m_atvOutput      = ACE_OS::gettimeofday();

            //错误消息回调
            App_MakePacket::instance()->PutSendErrorMessage(GetConnectID(), pMbData, m_atvOutput);
            //App_MessageBlockManager::instance()->Close(pMbData);

            pMbData->rd_ptr((size_t)0);
            ACE_Time_Value tvNow = ACE_OS::gettimeofday();
            App_MakePacket::instance()->PutSendErrorMessage(GetConnectID(), pMbData, tvNow);

            //关闭当前连接
            App_ConnectManager::instance()->CloseUnLock(GetConnectID());

            return false;
        }
        else if(nDataLen >= nSendPacketLen - nIsSendSize)   //当数据包全部发送完毕，清空。
        {
            //OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_output] ConnectID = %d, send (%d) OK.\n", GetConnectID(), msg_queue()->is_empty()));
            m_u4AllSendCount    += 1;
            m_u4AllSendSize     += (uint32)pMbData->length();
            m_atvOutput         = ACE_OS::gettimeofday();

            int nMessageID = pMbData->msg_type() - ACE_Message_Block::MB_USER;

            if(nMessageID > 0)
            {
                //需要回调发送成功回执
                _MakePacket objMakePacket;

                CPacketParse objPacketParse;
                ACE_Message_Block* pSendOKData = App_MessageBlockManager::instance()->Create(sizeof(int));
                memcpy_safe((char* )&nMessageID, sizeof(int), pSendOKData->wr_ptr(), sizeof(int));
                pSendOKData->wr_ptr(sizeof(int));
                objPacketParse.SetPacket_Head_Message(pSendOKData);
                objPacketParse.SetPacket_Head_Curr_Length(pSendOKData->length());

                objMakePacket.m_u4ConnectID       = GetConnectID();
                objMakePacket.m_pPacketParse      = &objPacketParse;
                objMakePacket.m_u1Option          = PACKET_SEND_OK;

                //发送客户端链接断开消息。
                ACE_Time_Value tvNow = ACE_OS::gettimeofday();

                if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvNow))
                {
                    OUR_DEBUG((LM_ERROR, "[CConnectHandle::Close] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
                }

                //还原消息类型
                pMbData->msg_type(ACE_Message_Block::MB_DATA);
            }

            App_MessageBlockManager::instance()->Close(pMbData);

            //看看需要不需要关闭连接
            if(CLIENT_CLOSE_SENDOK == m_emStatus)
            {
                if(m_u4ReadSendSize - m_u4SuccessSendSize == 0)
                {
                    ServerClose(CLIENT_CLOSE_IMMEDIATLY);
                }
            }

            return true;
        }
        else
        {
            pMbData->rd_ptr(nDataLen);
            nIsSendSize      += nDataLen;
            m_atvOutput      = ACE_OS::gettimeofday();
            continue;
        }
    }

    return true;
}

bool CConnectHandler::CheckMessage()
{
    if(m_pPacketParse->GetMessageBody() == NULL)
    {
        m_u4AllRecvSize += (uint32)m_pPacketParse->GetMessageHead()->length();
    }
    else
    {
        m_u4AllRecvSize += (uint32)m_pPacketParse->GetMessageHead()->length() + (uint32)m_pPacketParse->GetMessageBody()->length();
    }

    //OUR_DEBUG((LM_ERROR, "[CConnectHandler::CheckMessage]head length=%d.\n", m_pPacketParse->GetMessageHead()->length()));
    //OUR_DEBUG((LM_ERROR, "[CConnectHandler::CheckMessage]body length=%d.\n", m_pPacketParse->GetMessageBody()->length()));

    m_u4AllRecvCount++;

    //如果需要统计信息
    //App_IPAccount::instance()->UpdateIP((string)m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllSendSize);

    ACE_Time_Value tvCheck = ACE_OS::gettimeofday();
    ACE_Date_Time dtNow(tvCheck);

    if(false == m_TimeConnectInfo.RecvCheck((uint8)dtNow.minute(), 1, m_u4AllRecvSize))
    {
        //超过了限定的阀值，需要关闭链接，并记录日志
        AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECTABNORMAL,
                                               App_MainConfig::instance()->GetClientDataAlert()->m_u4MailID,
                                               (char* )"Alert",
                                               "[TCP]IP=%s,Prot=%d,PacketCount=%d, RecvSize=%d.",
                                               m_addrRemote.get_host_addr(),
                                               m_addrRemote.get_port_number(),
                                               m_TimeConnectInfo.m_u4RecvPacketCount,
                                               m_TimeConnectInfo.m_u4RecvSize);

        App_PacketParsePool::instance()->Delete(m_pPacketParse);
        m_pPacketParse = NULL;

        //设置封禁时间
        App_ForbiddenIP::instance()->AddTempIP(m_addrRemote.get_host_addr(), App_MainConfig::instance()->GetIPAlert()->m_u4IPTimeout);
        OUR_DEBUG((LM_ERROR, "[CConnectHandle::CheckMessage] ConnectID = %d, PutMessageBlock is check invalid.\n", GetConnectID()));
        return false;
    }

    //组织数据
    _MakePacket objMakePacket;

    objMakePacket.m_u4ConnectID       = GetConnectID();
    objMakePacket.m_pPacketParse      = m_pPacketParse;

    if(ACE_OS::strcmp("INADDR_ANY", m_szLocalIP) == 0)
    {
        objMakePacket.m_AddrListen.set(m_u4LocalPort);
    }
    else
    {
        objMakePacket.m_AddrListen.set(m_u4LocalPort, m_szLocalIP);
    }

    objMakePacket.m_u1Option = PACKET_PARSE;

    //将数据Buff放入消息体中
    if(false == App_MakePacket::instance()->PutMessageBlock(&objMakePacket, tvCheck))
    {
        App_PacketParsePool::instance()->Delete(m_pPacketParse);
        m_pPacketParse = NULL;

        OUR_DEBUG((LM_ERROR, "[CConnectHandle::CheckMessage] ConnectID = %d, PutMessageBlock is error.\n", GetConnectID()));
    }


    App_PacketParsePool::instance()->Delete(m_pPacketParse);

    return true;
}

_ClientConnectInfo CConnectHandler::GetClientInfo()
{
    _ClientConnectInfo ClientConnectInfo;

    ClientConnectInfo.m_blValid             = true;
    ClientConnectInfo.m_u4ConnectID         = GetConnectID();
    ClientConnectInfo.m_addrRemote          = m_addrRemote;
    ClientConnectInfo.m_u4RecvCount         = m_u4AllRecvCount;
    ClientConnectInfo.m_u4SendCount         = m_u4AllSendCount;
    ClientConnectInfo.m_u4AllRecvSize       = m_u4AllSendSize;
    ClientConnectInfo.m_u4AllSendSize       = m_u4AllSendSize;
    ClientConnectInfo.m_u4BeginTime         = (uint32)m_atvConnect.sec();
    ClientConnectInfo.m_u4AliveTime         = (uint32)(ACE_OS::gettimeofday().sec() -  m_atvConnect.sec());
    ClientConnectInfo.m_u4RecvQueueCount    = m_u4RecvQueueCount;
    ClientConnectInfo.m_u8RecvQueueTimeCost = m_u8RecvQueueTimeCost;
    ClientConnectInfo.m_u8SendQueueTimeCost = m_u8SendQueueTimeCost;

    return ClientConnectInfo;
}

_ClientIPInfo  CConnectHandler::GetClientIPInfo()
{
    _ClientIPInfo ClientIPInfo;
    sprintf_safe(ClientIPInfo.m_szClientIP, MAX_BUFF_50, "%s", m_addrRemote.get_host_addr());
    ClientIPInfo.m_nPort = (int)m_addrRemote.get_port_number();
    return ClientIPInfo;
}

_ClientIPInfo  CConnectHandler::GetLocalIPInfo()
{
    _ClientIPInfo ClientIPInfo;
    sprintf_safe(ClientIPInfo.m_szClientIP, MAX_BUFF_50, "%s", m_szLocalIP);
    ClientIPInfo.m_nPort = (int)m_u4LocalPort;
    return ClientIPInfo;
}

bool CConnectHandler::CheckSendMask(uint32 u4PacketLen)
{
    m_u4ReadSendSize += u4PacketLen;

    //OUR_DEBUG ((LM_ERROR, "[CConnectHandler::CheckSendMask]GetSendDataMask = %d, m_u4ReadSendSize=%d, m_u4SuccessSendSize=%d.\n", App_MainConfig::instance()->GetSendDataMask(), m_u4ReadSendSize, m_u4SuccessSendSize));
    if(m_u4ReadSendSize - m_u4SuccessSendSize >= App_MainConfig::instance()->GetSendDataMask())
    {
        OUR_DEBUG ((LM_ERROR, "[CConnectHandler::CheckSendMask]ConnectID = %d, SingleConnectMaxSendBuffer is more than(%d)!\n", GetConnectID(), m_u4ReadSendSize - m_u4SuccessSendSize));
        AppLogManager::instance()->WriteLog(LOG_SYSTEM_SENDQUEUEERROR, "]Connection from [%s:%d], SingleConnectMaxSendBuffer is more than(%d)!.", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4ReadSendSize - m_u4SuccessSendSize);
        return false;
    }
    else
    {
        return true;
    }
}

void CConnectHandler::ClearPacketParse()
{
    if(NULL != m_pPacketParse)
    {
        if(m_pPacketParse->GetMessageHead() != NULL)
        {
            App_MessageBlockManager::instance()->Close(m_pPacketParse->GetMessageHead());
        }

        if(m_pPacketParse->GetMessageBody() != NULL)
        {
            App_MessageBlockManager::instance()->Close(m_pPacketParse->GetMessageBody());
        }

        if(m_pCurrMessage != NULL && m_pPacketParse->GetMessageBody() != m_pCurrMessage && m_pPacketParse->GetMessageHead() != m_pCurrMessage)
        {
            App_MessageBlockManager::instance()->Close(m_pCurrMessage);
        }

        m_pCurrMessage = NULL;

        App_PacketParsePool::instance()->Delete(m_pPacketParse);
        m_pPacketParse = NULL;
    }
}

void CConnectHandler::SetConnectName(const char* pName)
{
    sprintf_safe(m_szConnectName, MAX_BUFF_100, "%s", pName);
}

void CConnectHandler::SetIsLog(bool blIsLog)
{
    m_blIsLog = blIsLog;
}

char* CConnectHandler::GetConnectName()
{
    return m_szConnectName;
}

bool CConnectHandler::GetIsLog()
{
    return m_blIsLog;
}

void CConnectHandler::SetHashID(int nHashID)
{
    m_nHashID = nHashID;
}

int CConnectHandler::GetHashID()
{
    return m_nHashID;
}

void CConnectHandler::SetLocalIPInfo(const char* pLocalIP, uint32 u4LocalPort)
{
    sprintf_safe(m_szLocalIP, MAX_BUFF_50, "%s", pLocalIP);
    m_u4LocalPort = u4LocalPort;
}

void CConnectHandler::SetSendCacheManager(CSendCacheManager* pSendCacheManager)
{
    if(NULL != pSendCacheManager)
    {
        m_pBlockMessage = pSendCacheManager->GetCacheData(GetConnectID());
    }
}

//***************************************************************************
CConnectManager::CConnectManager(void)
{
    m_u4TimeCheckID      = 0;
    m_szError[0]         = '\0';

    m_pTCTimeSendCheck   = NULL;
    m_tvCheckConnect     = ACE_OS::gettimeofday();
    m_blRun              = false;

    m_u4TimeConnect      = 0;
    m_u4TimeDisConnect   = 0;

    //初始化发送对象池
    m_SendMessagePool.Init();
}

CConnectManager::~CConnectManager(void)
{
    OUR_DEBUG((LM_INFO, "[CConnectManager::~CConnectManager].\n"));
    //m_blRun = false;
    //CloseAll();
}

void CConnectManager::CloseAll()
{
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGrard(m_ThreadWriteLock);
    msg_queue()->deactivate();

    KillTimer();

    vector<CConnectHandler*> vecCloseConnectHandler;
    m_objHashConnectList.Get_All_Used(vecCloseConnectHandler);

    for(int i = 0; i < (int)vecCloseConnectHandler.size(); i++)
    {
        CConnectHandler* pConnectHandler = vecCloseConnectHandler[i];

        if(pConnectHandler != NULL)
        {
            vecCloseConnectHandler.push_back(pConnectHandler);
            m_u4TimeDisConnect++;

            //加入链接统计功能
            //App_ConnectAccount::instance()->AddDisConnect();
        }
    }

    //开始关闭所有连接
    for(int i = 0; i < (int)vecCloseConnectHandler.size(); i++)
    {
        CConnectHandler* pConnectHandler = vecCloseConnectHandler[i];
        pConnectHandler->Close();
    }

    //删除hash表空间
    m_objHashConnectList.Close();

    //删除缓冲对象
    m_SendCacheManager.Close();
}

bool CConnectManager::Close(uint32 u4ConnectID)
{
    //OUR_DEBUG((LM_ERROR, "[CConnectManager::Close]ConnectID=%d Begin.\n", u4ConnectID));
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    //OUR_DEBUG((LM_ERROR, "[CConnectManager::Close]ConnectID=%d Begin 1.\n", u4ConnectID));
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    int nPos = m_objHashConnectList.Del_Hash_Data(szConnectID);

    if(0 < nPos)
    {
        //回收发送缓冲
        m_SendCacheManager.FreeCacheData(u4ConnectID);
        m_u4TimeDisConnect++;

        //加入链接统计功能
        App_ConnectAccount::instance()->AddDisConnect();

        //OUR_DEBUG((LM_ERROR, "[CConnectManager::Close]ConnectID=%d End.\n", u4ConnectID));
        return true;
    }
    else
    {
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::Close] ConnectID[%d] is not find.", u4ConnectID);
        return true;
    }
}

bool CConnectManager::CloseUnLock(uint32 u4ConnectID)
{
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    int nPos = m_objHashConnectList.Del_Hash_Data(szConnectID);

    if(0 < nPos)
    {
        //回收发送缓冲
        m_SendCacheManager.FreeCacheData(u4ConnectID);
        m_u4TimeDisConnect++;

        //加入链接统计功能
        App_ConnectAccount::instance()->AddDisConnect();

        OUR_DEBUG((LM_ERROR, "[CConnectManager::CloseUnLock]ConnectID=%d End.\n", u4ConnectID));
        return true;
    }
    else
    {
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::Close] ConnectID[%d] is not find.", u4ConnectID);
        return true;
    }
}

bool CConnectManager::CloseConnect(uint32 u4ConnectID, EM_Client_Close_status emStatus)
{
    //OUR_DEBUG((LM_ERROR, "[CConnectManager::CloseConnect]ConnectID=%d Begin.\n", u4ConnectID));
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    //OUR_DEBUG((LM_ERROR, "[CConnectManager::CloseConnect]ConnectID=%d Begin 1.\n", u4ConnectID));
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(emStatus != CLIENT_CLOSE_IMMEDIATLY)
    {
        return false;
    }

    if(NULL != pConnectHandler)
    {
        //回收发送缓冲
        m_SendCacheManager.FreeCacheData(u4ConnectID);
        pConnectHandler->ServerClose(emStatus);
        m_u4TimeDisConnect++;

        m_objHashConnectList.Del_Hash_Data(szConnectID);

        //加入链接统计功能
        App_ConnectAccount::instance()->AddDisConnect();
        return true;
    }
    else
    {
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::CloseConnect] ConnectID[%d] is not find.", u4ConnectID);
        return true;
    }
}

bool CConnectManager::AddConnect(uint32 u4ConnectID, CConnectHandler* pConnectHandler)
{
    //OUR_DEBUG((LM_ERROR, "[CConnectManager::AddConnect]ConnectID=%d Begin.\n", u4ConnectID));
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    //OUR_DEBUG((LM_ERROR, "[CConnectManager::AddConnect]ConnectID=%d Begin 1.\n", u4ConnectID));

    if(pConnectHandler == NULL)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectManager::AddConnect]ConnectID=%d, pConnectHandler is NULL.\n", u4ConnectID));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::AddConnect] pConnectHandler is NULL.");
        return false;
    }

    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pCurrConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(NULL != pCurrConnectHandler)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectManager::AddConnect]ConnectID=%d is find.\n", u4ConnectID));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::AddConnect] ConnectID[%d] is exist.", u4ConnectID);
        return false;
    }

    pConnectHandler->SetConnectID(u4ConnectID);
    pConnectHandler->SetSendCacheManager(&m_SendCacheManager);
    //加入Hash数组
    m_objHashConnectList.Add_Hash_Data(szConnectID, pConnectHandler);
    m_u4TimeConnect++;

    //OUR_DEBUG((LM_ERROR, "[CConnectManager::AddConnect]ConnectID=%d.\n", u4ConnectID));

    //加入链接统计功能
    App_ConnectAccount::instance()->AddConnect();

    //OUR_DEBUG((LM_ERROR, "[CConnectManager::AddConnect]ConnectID=%d End.\n", u4ConnectID));
    return true;
}

bool CConnectManager::SendMessage(uint32 u4ConnectID, IBuffPacket* pBuffPacket, uint16 u2CommandID, uint8 u1SendState, uint8 u1SendType, ACE_Time_Value& tvSendBegin, bool blDelete, int nMessageID)
{
    //因为是队列调用，所以这里不需要加锁了。
    if(NULL == pBuffPacket)
    {
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::SendMessage] ConnectID[%d] pBuffPacket is NULL.", u4ConnectID);
        return false;
    }

    m_ThreadWriteLock.acquire();
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);
    m_ThreadWriteLock.release();

    if(NULL != pConnectHandler)
    {
        uint32 u4PacketSize = 0;
        //OUR_DEBUG((LM_ERROR, "[CConnectManager::SendMessage]ConnectID=%d Begin 1 pConnectHandler.\n", u4ConnectID));
        pConnectHandler->SendMessage(u2CommandID, pBuffPacket, u1SendState, u1SendType, u4PacketSize, blDelete, nMessageID);
        //OUR_DEBUG((LM_ERROR, "[CConnectManager::SendMessage]ConnectID=%d End 1 pConnectHandler.\n", u4ConnectID));
        //记录消息发送消耗时间
        ACE_Time_Value tvInterval = ACE_OS::gettimeofday() - tvSendBegin;
        uint32 u4SendCost = (uint32)(tvInterval.msec());
        pConnectHandler->SetSendQueueTimeCost(u4SendCost);
        m_CommandAccount.SaveCommandData(u2CommandID, (uint64)u4SendCost, PACKET_TCP, u4PacketSize, u4PacketSize, COMMAND_TYPE_OUT);
        return true;
    }
    else
    {
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::SendMessage] ConnectID[%d] is not find.", u4ConnectID);
        //如果连接不存在了，在这里返回失败，回调给业务逻辑去处理
        ACE_Message_Block* pSendMessage = App_MessageBlockManager::instance()->Create(pBuffPacket->GetPacketLen());
        memcpy_safe((char* )pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), (char* )pSendMessage->wr_ptr(), pBuffPacket->GetPacketLen());
        pSendMessage->wr_ptr(pBuffPacket->GetPacketLen());
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();
        App_MakePacket::instance()->PutSendErrorMessage(0, pSendMessage, tvNow);

        if(true == blDelete)
        {
            App_BuffPacketManager::instance()->Delete(pBuffPacket);
        }

        return true;
    }

    //OUR_DEBUG((LM_ERROR, "[CConnectManager::SendMessage]ConnectID=%d End.\n", u4ConnectID));
    return true;
}

bool CConnectManager::PostMessage(uint32 u4ConnectID, IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    //OUR_DEBUG((LM_INFO, "[CConnectManager::PostMessage]Begin.\n"));
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGrard(m_ThreadWriteLock);
    //OUR_DEBUG((LM_INFO, "[CConnectManager::PostMessage]Begin 1.\n"));

    //放入发送队列
    _SendMessage* pSendMessage = m_SendMessagePool.Create();

    ACE_Message_Block* mb = pSendMessage->GetQueueMessage();

    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(NULL != pConnectHandler)
    {
        bool blState = pConnectHandler->CheckSendMask(pBuffPacket->GetPacketLen());

        if(false == blState)
        {
            //超过了阀值，则关闭连接
            if(blDelete == true)
            {
                App_BuffPacketManager::instance()->Delete(pBuffPacket);
            }

            pConnectHandler->ServerClose(CLIENT_CLOSE_IMMEDIATLY);
            m_objHashConnectList.Del_Hash_Data(szConnectID);
            return false;
        }
    }

    if(NULL != mb)
    {
        if(NULL == pSendMessage)
        {
            OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] new _SendMessage is error.\n"));
            return false;
        }

        pSendMessage->m_u4ConnectID = u4ConnectID;
        pSendMessage->m_pBuffPacket = pBuffPacket;
        pSendMessage->m_nEvents     = u1SendType;
        pSendMessage->m_u2CommandID = u2CommandID;
        pSendMessage->m_blDelete    = blDelete;
        pSendMessage->m_u1SendState = u1SendState;
        pSendMessage->m_nMessageID  = nServerID;
        pSendMessage->m_tvSend      = ACE_OS::gettimeofday();

        //判断队列是否是已经最大
        int nQueueCount = (int)msg_queue()->message_count();

        if(nQueueCount >= (int)MAX_MSG_THREADQUEUE)
        {
            OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue is Full nQueueCount = [%d].\n", nQueueCount));

            if(blDelete == true)
            {
                App_BuffPacketManager::instance()->Delete(pBuffPacket);
            }

            m_SendMessagePool.Delete(pSendMessage);
            return false;
        }

        ACE_Time_Value xtime = ACE_OS::gettimeofday() + ACE_Time_Value(0, m_u4SendQueuePutTime);

        if(this->putq(mb, &xtime) == -1)
        {
            OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue putq  error nQueueCount = [%d] errno = [%d].\n", nQueueCount, errno));

            if(blDelete == true)
            {
                App_BuffPacketManager::instance()->Delete(pBuffPacket);
            }

            m_SendMessagePool.Delete(pSendMessage);
            return false;
        }
    }
    else
    {
        OUR_DEBUG((LM_ERROR,"[CMessageService::PutMessage] mb new error.\n"));

        if(blDelete == true)
        {
            App_BuffPacketManager::instance()->Delete(pBuffPacket);
        }

        return false;
    }

    //OUR_DEBUG((LM_INFO, "[CConnectManager::PostMessage]End.\n"));
    return true;
}

const char* CConnectManager::GetError()
{
    return m_szError;
}

bool CConnectManager::StartTimer()
{
    //启动发送线程
    if(0 != open())
    {
        OUR_DEBUG((LM_ERROR, "[CConnectManager::StartTimer]Open() is error.\n"));
        return false;
    }

    //避免定时器重复启动
    KillTimer();
    OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer()-->begin....\n"));
    //得到第二个Reactor
    ACE_Reactor* pReactor = App_ReactorManager::instance()->GetAce_Reactor(REACTOR_POSTDEFINE);

    if(NULL == pReactor)
    {
        OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer() -->GetAce_Reactor(REACTOR_POSTDEFINE) is NULL.\n"));
        return false;
    }

    m_pTCTimeSendCheck = new _TimerCheckID();

    if(NULL == m_pTCTimeSendCheck)
    {
        OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer() m_pTCTimeSendCheck is NULL.\n"));
        return false;
    }

    m_pTCTimeSendCheck->m_u2TimerCheckID = PARM_CONNECTHANDLE_CHECK;
    m_u4TimeCheckID = pReactor->schedule_timer(this, (const void*)m_pTCTimeSendCheck, ACE_Time_Value(App_MainConfig::instance()->GetCheckAliveTime(), 0), ACE_Time_Value(App_MainConfig::instance()->GetCheckAliveTime(), 0));

    if(0 == m_u4TimeCheckID)
    {
        OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer()--> Start thread m_u4TimeCheckID error.\n"));
        return false;
    }
    else
    {
        OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer()--> Start thread time OK.\n"));
        return true;
    }
}

bool CConnectManager::KillTimer()
{
    if(m_u4TimeCheckID > 0)
    {
        App_ReactorManager::instance()->GetAce_Reactor(REACTOR_POSTDEFINE)->cancel_timer(m_u4TimeCheckID);
        m_u4TimeCheckID = 0;
    }

    SAFE_DELETE(m_pTCTimeSendCheck);
    return true;
}

int CConnectManager::handle_timeout(const ACE_Time_Value& tv, const void* arg)
{
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    ACE_Time_Value tvNow = ACE_OS::gettimeofday();
    vector<CConnectHandler*> vecDelConnectHandler;

    if(arg == NULL)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectManager::handle_timeout]arg is not NULL, tv = %d.\n", tv.sec()));
    }

    _TimerCheckID* pTimerCheckID = (_TimerCheckID*)arg;

    if(NULL == pTimerCheckID)
    {
        return 0;
    }

    //定时检测发送，这里将定时记录链接信息放入其中，减少一个定时器
    if(pTimerCheckID->m_u2TimerCheckID == PARM_CONNECTHANDLE_CHECK)
    {
        if(m_objHashConnectList.Get_Used_Count() > 0)
        {
            m_ThreadWriteLock.acquire();
            vector<CConnectHandler*> vecConnectHandler;
            m_objHashConnectList.Get_All_Used(vecConnectHandler);

            for(int i = 0; i < (int)vecConnectHandler.size(); i++)
            {
                CConnectHandler* pConnectHandler = (CConnectHandler* )vecConnectHandler[i];

                if(pConnectHandler != NULL)
                {
                    if(false == pConnectHandler->CheckAlive(tvNow))
                    {
                        vecDelConnectHandler.push_back(pConnectHandler);
                    }
                }
            }

            m_ThreadWriteLock.release();
        }

        for(uint32 i= 0; i < vecDelConnectHandler.size(); i++)
        {
            //关闭引用关系
            Close(vecDelConnectHandler[i]->GetConnectID());

            //服务器关闭连接
            vecDelConnectHandler[i]->ServerClose(CLIENT_CLOSE_IMMEDIATLY, PACKET_CHEK_TIMEOUT);
        }

        //判定是否应该记录链接日志
        ACE_Time_Value tvNow = ACE_OS::gettimeofday();
        ACE_Time_Value tvInterval(tvNow - m_tvCheckConnect);

        if(tvInterval.sec() >= MAX_MSG_HANDLETIME)
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "[CConnectManager]CurrConnectCount = %d,TimeInterval=%d, TimeConnect=%d, TimeDisConnect=%d.",
                                                GetCount(), MAX_MSG_HANDLETIME, m_u4TimeConnect, m_u4TimeDisConnect);

            //重置单位时间连接数和断开连接数
            m_u4TimeConnect    = 0;
            m_u4TimeDisConnect = 0;
            m_tvCheckConnect   = tvNow;
        }

        //检测连接总数是否超越监控阀值
        if(App_MainConfig::instance()->GetConnectAlert()->m_u4ConnectAlert > 0)
        {
            if(GetCount() > (int)App_MainConfig::instance()->GetConnectAlert()->m_u4ConnectAlert)
            {
                AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
                                                       App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
                                                       (char* )"Alert",
                                                       "[CProConnectManager]active ConnectCount is more than limit(%d > %d).",
                                                       GetCount(),
                                                       App_MainConfig::instance()->GetConnectAlert()->m_u4ConnectAlert);
            }
        }

        //检测单位时间连接数是否超越阀值
        int nCheckRet = App_ConnectAccount::instance()->CheckConnectCount();

        if(nCheckRet == 1)
        {
            AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
                                                   App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
                                                   (char* )"Alert",
                                                   "[CProConnectManager]CheckConnectCount is more than limit(%d > %d).",
                                                   App_ConnectAccount::instance()->GetCurrConnect(),
                                                   App_ConnectAccount::instance()->GetConnectMax());
        }
        else if(nCheckRet == 2)
        {
            AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
                                                   App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
                                                   (char* )"Alert",
                                                   "[CProConnectManager]CheckConnectCount is little than limit(%d < %d).",
                                                   App_ConnectAccount::instance()->GetCurrConnect(),
                                                   App_ConnectAccount::instance()->Get4ConnectMin());
        }

        //检测单位时间连接断开数是否超越阀值
        nCheckRet = App_ConnectAccount::instance()->CheckDisConnectCount();

        if(nCheckRet == 1)
        {
            AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
                                                   App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
                                                   (char* )"Alert",
                                                   "[CProConnectManager]CheckDisConnectCount is more than limit(%d > %d).",
                                                   App_ConnectAccount::instance()->GetCurrConnect(),
                                                   App_ConnectAccount::instance()->GetDisConnectMax());
        }
        else if(nCheckRet == 2)
        {
            AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
                                                   App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
                                                   (char* )"Alert",
                                                   "[CProConnectManager]CheckDisConnectCount is little than limit(%d < %d).",
                                                   App_ConnectAccount::instance()->GetCurrConnect(),
                                                   App_ConnectAccount::instance()->GetDisConnectMin());
        }

    }

    return 0;
}

int CConnectManager::GetCount()
{
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    return m_objHashConnectList.Get_Used_Count();
}

int CConnectManager::open(void* args)
{
    if(args != NULL)
    {
        OUR_DEBUG((LM_INFO,"[CConnectManager::open]args is not NULL.\n"));
    }

    m_blRun = true;
    msg_queue()->high_water_mark(MAX_MSG_MASK);
    msg_queue()->low_water_mark(MAX_MSG_MASK);

    OUR_DEBUG((LM_INFO,"[CConnectManager::open] m_u4HighMask = [%d] m_u4LowMask = [%d]\n", MAX_MSG_MASK, MAX_MSG_MASK));

    if(activate(THR_NEW_LWP | THR_JOINABLE | THR_INHERIT_SCHED | THR_SUSPENDED, MAX_MSG_THREADCOUNT) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectManager::open] activate error ThreadCount = [%d].", MAX_MSG_THREADCOUNT));
        m_blRun = false;
        return -1;
    }

    m_u4SendQueuePutTime = App_MainConfig::instance()->GetSendQueuePutTime() * 1000;

    resume();

    return 0;
}

int CConnectManager::svc (void)
{
    ACE_Message_Block* mb = NULL;
    ACE_Time_Value xtime;

    while(IsRun())
    {
        mb = NULL;

        if(getq(mb, 0) == -1)
        {
            OUR_DEBUG((LM_ERROR,"[CConnectManager::svc] get error errno = [%d].\n", errno));
            m_blRun = false;
            break;
        }

        if (mb == NULL)
        {
            continue;
        }

        _SendMessage* msg = *((_SendMessage**)mb->base());

        if (! msg)
        {
            continue;
        }

        //处理发送数据
        SendMessage(msg->m_u4ConnectID, msg->m_pBuffPacket, msg->m_u2CommandID, msg->m_u1SendState, msg->m_nEvents, msg->m_tvSend, msg->m_blDelete, msg->m_nMessageID);

        m_SendMessagePool.Delete(msg);

    }

    OUR_DEBUG((LM_INFO,"[CConnectManager::svc] svc finish!\n"));
    return 0;
}

bool CConnectManager::IsRun()
{
    return m_blRun;
}

void CConnectManager::CloseQueue()
{
    this->msg_queue()->deactivate();
}

int CConnectManager::close(u_long)
{
    m_blRun = false;
    OUR_DEBUG((LM_INFO,"[CConnectManager::close] close().\n"));
    return 0;
}

void CConnectManager::SetRecvQueueTimeCost(uint32 u4ConnectID, uint32 u4TimeCost)
{
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(NULL != pConnectHandler)
    {
        pConnectHandler->SetRecvQueueTimeCost(u4TimeCost);
    }
}

void CConnectManager::GetConnectInfo(vecClientConnectInfo& VecClientConnectInfo)
{
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    vector<CConnectHandler*> vecConnectHandler;
    m_objHashConnectList.Get_All_Used(vecConnectHandler);

    for(int i = 0; i < (int)vecConnectHandler.size(); i++)
    {
        CConnectHandler* pConnectHandler = vecConnectHandler[i];

        if(pConnectHandler != NULL)
        {
            VecClientConnectInfo.push_back(pConnectHandler->GetClientInfo());
        }
    }
}

_ClientIPInfo CConnectManager::GetClientIPInfo(uint32 u4ConnectID)
{
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(NULL != pConnectHandler)
    {
        return pConnectHandler->GetClientIPInfo();
    }
    else
    {
        _ClientIPInfo ClientIPInfo;
        return ClientIPInfo;
    }
}

_ClientIPInfo CConnectManager::GetLocalIPInfo(uint32 u4ConnectID)
{
    //ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(NULL != pConnectHandler)
    {
        return pConnectHandler->GetLocalIPInfo();
    }
    else
    {
        _ClientIPInfo ClientIPInfo;
        return ClientIPInfo;
    }
}

bool CConnectManager::PostMessageAll(IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    m_ThreadWriteLock.acquire();
    vector<CConnectHandler*> objvecConnectManager;
    m_objHashConnectList.Get_All_Used(objvecConnectManager);
    m_ThreadWriteLock.release();

    uint32 u4ConnectID = 0;

    for(uint32 i = 0; i < (uint32)objvecConnectManager.size(); i++)
    {
        IBuffPacket* pCurrBuffPacket = App_BuffPacketManager::instance()->Create();

        if(NULL == pCurrBuffPacket)
        {
            OUR_DEBUG((LM_INFO, "[CConnectManager::PostMessage]pCurrBuffPacket is NULL.\n"));

            if(blDelete == true)
            {
                App_BuffPacketManager::instance()->Delete(pBuffPacket);
            }

            return false;
        }

        pCurrBuffPacket->WriteStream(pBuffPacket->GetData(), pBuffPacket->GetPacketLen());

        CConnectHandler* pConnectHandler = objvecConnectManager[i];
        //检查是否超过了单位时间发送数据上限阈值
        bool blState = pConnectHandler->CheckSendMask(pBuffPacket->GetPacketLen());

        if(false == blState)
        {
            //超过了阀值，则关闭连接
            if(blDelete == true)
            {
                App_BuffPacketManager::instance()->Delete(pBuffPacket);
            }

            //服务器主动关闭连接
            pConnectHandler->ServerClose(CLIENT_CLOSE_IMMEDIATLY);
            char szConnectID[10] = {'\0'};
            sprintf_safe(szConnectID, 10, "%d", pConnectHandler->GetConnectID());

            m_objHashConnectList.Del_Hash_Data(szConnectID);
            continue;
        }


        //放入发送队列
        _SendMessage* pSendMessage = m_SendMessagePool.Create();

        ACE_Message_Block* mb = pSendMessage->GetQueueMessage();

        if(NULL != mb)
        {
            if(NULL == pSendMessage)
            {
                OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] new _SendMessage is error.\n"));

                if(blDelete == true)
                {
                    App_BuffPacketManager::instance()->Delete(pBuffPacket);
                }

                return false;
            }

            pSendMessage->m_u4ConnectID = u4ConnectID;
            pSendMessage->m_pBuffPacket = pCurrBuffPacket;
            pSendMessage->m_nEvents     = u1SendType;
            pSendMessage->m_u2CommandID = u2CommandID;
            pSendMessage->m_blDelete    = blDelete;
            pSendMessage->m_u1SendState = u1SendState;
            pSendMessage->m_nMessageID  = nServerID;
            pSendMessage->m_tvSend      = ACE_OS::gettimeofday();

            //判断队列是否是已经最大
            int nQueueCount = (int)msg_queue()->message_count();

            if(nQueueCount >= (int)MAX_MSG_THREADQUEUE)
            {
                OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue is Full nQueueCount = [%d].\n", nQueueCount));

                if(blDelete == true)
                {
                    App_BuffPacketManager::instance()->Delete(pBuffPacket);
                }

                m_SendMessagePool.Delete(pSendMessage);
                return false;
            }

            ACE_Time_Value xtime = ACE_OS::gettimeofday() + ACE_Time_Value(0, MAX_MSG_PUTTIMEOUT);

            if(this->putq(mb, &xtime) == -1)
            {
                OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue putq  error nQueueCount = [%d] errno = [%d].\n", nQueueCount, errno));

                if(blDelete == true)
                {
                    App_BuffPacketManager::instance()->Delete(pBuffPacket);
                }

                m_SendMessagePool.Delete(pSendMessage);
                return false;
            }
        }
        else
        {
            OUR_DEBUG((LM_ERROR,"[CMessageService::PutMessage] mb new error.\n"));

            if(blDelete == true)
            {
                App_BuffPacketManager::instance()->Delete(pBuffPacket);
            }

            return false;
        }
    }

    return true;
}

bool CConnectManager::SetConnectName(uint32 u4ConnectID, const char* pName)
{
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(NULL != pConnectHandler)
    {
        pConnectHandler->SetConnectName(pName);
        return true;
    }
    else
    {
        return false;
    }
}

bool CConnectManager::SetIsLog(uint32 u4ConnectID, bool blIsLog)
{
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);
    CConnectHandler* pConnectHandler = m_objHashConnectList.Get_Hash_Box_Data(szConnectID);

    if(NULL != pConnectHandler)
    {
        pConnectHandler->SetIsLog(blIsLog);
        return true;
    }
    else
    {
        return false;
    }
}

void CConnectManager::GetClientNameInfo(const char* pName, vecClientNameInfo& objClientNameInfo)
{
    vector<CConnectHandler*> vecConnectHandler;
    m_objHashConnectList.Get_All_Used(vecConnectHandler);

    for(int i = 0; i < (int)vecConnectHandler.size(); i++)
    {
        CConnectHandler* pConnectHandler = vecConnectHandler[i];

        if(NULL != pConnectHandler && ACE_OS::strcmp(pConnectHandler->GetConnectName(), pName) == 0)
        {
            _ClientNameInfo ClientNameInfo;
            ClientNameInfo.m_nConnectID = (int)pConnectHandler->GetConnectID();
            sprintf_safe(ClientNameInfo.m_szName, MAX_BUFF_100, "%s", pConnectHandler->GetConnectName());
            sprintf_safe(ClientNameInfo.m_szClientIP, MAX_BUFF_50, "%s", pConnectHandler->GetClientIPInfo().m_szClientIP);
            ClientNameInfo.m_nPort =  pConnectHandler->GetClientIPInfo().m_nPort;

            if(pConnectHandler->GetIsLog() == true)
            {
                ClientNameInfo.m_nLog = 1;
            }
            else
            {
                ClientNameInfo.m_nLog = 0;
            }

            objClientNameInfo.push_back(ClientNameInfo);
        }
    }
}

_CommandData* CConnectManager::GetCommandData( uint16 u2CommandID )
{
    return m_CommandAccount.GetCommandData(u2CommandID);
}

void CConnectManager::Init( uint16 u2Index )
{
    //按照线程初始化统计模块的名字
    char szName[MAX_BUFF_50] = {'\0'};
    sprintf_safe(szName, MAX_BUFF_50, "发送线程(%d)", u2Index);
    m_CommandAccount.InitName(szName, App_MainConfig::instance()->GetMaxCommandCount());

    //初始化统计模块功能
    m_CommandAccount.Init(App_MainConfig::instance()->GetCommandAccount(),
                          App_MainConfig::instance()->GetCommandFlow(),
                          App_MainConfig::instance()->GetPacketTimeOut());

    //初始化发送缓冲
    m_SendCacheManager.Init(App_MainConfig::instance()->GetBlockCount(), App_MainConfig::instance()->GetBlockSize());

    //初始化Hash表
    uint16 u2PoolSize = App_MainConfig::instance()->GetMaxHandlerCount();
    m_objHashConnectList.Init((int)u2PoolSize);
}

uint32 CConnectManager::GetCommandFlowAccount()
{
    return m_CommandAccount.GetFlowOut();
}

EM_Client_Connect_status CConnectManager::GetConnectState(uint32 u4ConnectID)
{
    char szConnectID[10] = {'\0'};
    sprintf_safe(szConnectID, 10, "%d", u4ConnectID);

    if(NULL == m_objHashConnectList.Get_Hash_Box_Data(szConnectID))
    {
        return CLIENT_CONNECT_NO_EXIST;
    }
    else
    {
        return CLIENT_CONNECT_EXIST;
    }
}

//*********************************************************************************

CConnectHandlerPool::CConnectHandlerPool(void)
{
    //ConnectID计数器从1开始
    m_u4CurrMaxCount = 1;
}

CConnectHandlerPool::~CConnectHandlerPool(void)
{
    OUR_DEBUG((LM_INFO, "[CConnectHandlerPool::~CConnectHandlerPool].\n"));
    Close();
    OUR_DEBUG((LM_INFO, "[CConnectHandlerPool::~CConnectHandlerPool]End.\n"));
}

void CConnectHandlerPool::Init(int nObjcetCount)
{
    Close();

    //初始化HashTable
    m_objHashHandleList.Init((int)nObjcetCount);

    for(int i = 0; i < nObjcetCount; i++)
    {
        CConnectHandler* pHandler = new CConnectHandler();

        if(NULL != pHandler)
        {
            //将ID和Handler指针的关系存入hashTable
            char szHandlerID[10] = {'\0'};
            sprintf_safe(szHandlerID, 10, "%d", m_u4CurrMaxCount);
            int nHashPos = m_objHashHandleList.Add_Hash_Data(szHandlerID, pHandler);

            if(-1 != nHashPos)
            {
                pHandler->Init(nHashPos);
            }

            m_u4CurrMaxCount++;
        }
    }
}

void CConnectHandlerPool::Close()
{
    //清理所有已存在的指针
    vector<CConnectHandler*> vecConnectHandler;
    m_objHashHandleList.Get_All_Used(vecConnectHandler);

    for(int i = 0; i < (int)vecConnectHandler.size(); i++)
    {
        CConnectHandler* pHandler = vecConnectHandler[i];
        SAFE_DELETE(pHandler);
    }

    m_u4CurrMaxCount  = 1;
}

int CConnectHandlerPool::GetUsedCount()
{
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

    return m_objHashHandleList.Get_Count() - m_objHashHandleList.Get_Used_Count();
}

int CConnectHandlerPool::GetFreeCount()
{
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

    return m_objHashHandleList.Get_Used_Count();
}

CConnectHandler* CConnectHandlerPool::Create()
{
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

    CConnectHandler* pHandler = NULL;

    //在Hash表中弹出一个已使用的数据
    pHandler = m_objHashHandleList.Pop();

    //没找到空余的
    return pHandler;
}

bool CConnectHandlerPool::Delete(CConnectHandler* pObject)
{
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

    if(NULL == pObject)
    {
        return false;
    }

    char szHandlerID[10] = {'\0'};
    sprintf_safe(szHandlerID, 10, "%d", pObject->GetHandlerID());
    bool blState = m_objHashHandleList.Push(szHandlerID, pObject);

    if(false == blState)
    {
        OUR_DEBUG((LM_INFO, "[CProConnectHandlerPool::Delete]szHandlerID=%s(0x%08x).\n", szHandlerID, pObject));
    }
    else
    {
        //OUR_DEBUG((LM_INFO, "[CProConnectHandlerPool::Delete]szHandlerID=%s(0x%08x) nPos=%d.\n", szHandlerID, pObject, nPos));
    }

    return true;
}

//==============================================================
CConnectManagerGroup::CConnectManagerGroup()
{
    m_objConnnectManagerList = NULL;
    m_u4CurrMaxCount         = 0;
    m_u2ThreadQueueCount     = SENDQUEUECOUNT;
}

CConnectManagerGroup::~CConnectManagerGroup()
{
    OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::~CConnectManagerGroup].\n"));

    Close();
}

void CConnectManagerGroup::Close()
{
    if(NULL != m_objConnnectManagerList)
    {
        for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
        {
            CConnectManager* pConnectManager = m_objConnnectManagerList[i];
            SAFE_DELETE(pConnectManager);
        }
    }

    SAFE_DELETE_ARRAY(m_objConnnectManagerList);
    m_u2ThreadQueueCount = 0;
}

void CConnectManagerGroup::Init(uint16 u2SendQueueCount)
{
    Close();

    m_objConnnectManagerList = new CConnectManager*[u2SendQueueCount];
    memset(m_objConnnectManagerList, 0, sizeof(CConnectManager*)*u2SendQueueCount);

    for(int i = 0; i < (int)u2SendQueueCount; i++)
    {
        CConnectManager* pConnectManager = new CConnectManager();

        if(NULL != pConnectManager)
        {
            //初始化统计器
            pConnectManager->Init((uint16)i);
            //加入数组
            m_objConnnectManagerList[i] = pConnectManager;
            OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::Init]Creat %d SendQueue OK.\n", i));
        }
    }

    m_u2ThreadQueueCount = u2SendQueueCount;
}

uint32 CConnectManagerGroup::GetGroupIndex()
{
    //根据链接获得命中，（简单球形命中算法）
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
    m_u4CurrMaxCount++;
    return m_u4CurrMaxCount;
}

bool CConnectManagerGroup::AddConnect(CConnectHandler* pConnectHandler)
{
    ACE_Guard<ACE_Recursive_Thread_Mutex> WGrard(m_ThreadWriteLock);

    uint32 u4ConnectID = GetGroupIndex();

    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::AddConnect]No find send Queue object.\n"));
        return false;
    }

    //OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::Init]u4ConnectID=%d, u2ThreadIndex=%d.\n", u4ConnectID, u2ThreadIndex));

    return pConnectManager->AddConnect(u4ConnectID, pConnectHandler);
}

bool CConnectManagerGroup::PostMessage(uint32 u4ConnectID, IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager =  m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
        return false;
    }

    //OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]u4ConnectID=%d, u2ThreadIndex=%d.\n", u4ConnectID, u2ThreadIndex));

    return pConnectManager->PostMessage(u4ConnectID, pBuffPacket, u1SendType, u2CommandID, u1SendState, blDelete, nServerID);
}

bool CConnectManagerGroup::PostMessage( uint32 u4ConnectID, const char* pData, uint32 nDataLen, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager =  m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));

        if(blDelete == true)
        {
            SAFE_DELETE_ARRAY(pData);
        }

        return false;
    }

    //OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]u4ConnectID=%d, u2ThreadIndex=%d.\n", u4ConnectID, u2ThreadIndex));
    IBuffPacket* pBuffPacket = App_BuffPacketManager::instance()->Create();

    if(NULL != pBuffPacket)
    {
        pBuffPacket->WriteStream(pData, nDataLen);

        if(blDelete == true)
        {
            SAFE_DELETE_ARRAY(pData);
        }

        return pConnectManager->PostMessage(u4ConnectID, pBuffPacket, u1SendType, u2CommandID, u1SendState, true, nServerID);
    }
    else
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]pBuffPacket is NULL.\n"));

        if(blDelete == true)
        {
            SAFE_DELETE_ARRAY(pData);
        }

        return false;
    }
}

bool CConnectManagerGroup::PostMessage( vector<uint32> vecConnectID, IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    uint32 u4ConnectID = 0;

    for(uint32 i = 0; i < (uint32)vecConnectID.size(); i++)
    {
        //判断命中到哪一个线程组里面去
        u4ConnectID = vecConnectID[i];
        uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

        CConnectManager* pConnectManager =  m_objConnnectManagerList[u2ThreadIndex];

        if(NULL == pConnectManager)
        {
            //OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
            continue;
        }

        //为每一个Connect设置发送对象数据包
        IBuffPacket* pCurrBuffPacket = App_BuffPacketManager::instance()->Create();

        if(NULL == pCurrBuffPacket)
        {
            continue;
        }

        pCurrBuffPacket->WriteStream(pBuffPacket->GetData(), pBuffPacket->GetWriteLen());

        pConnectManager->PostMessage(u4ConnectID, pCurrBuffPacket, u1SendType, u2CommandID, u1SendState, true, nServerID);
    }

    if(true == blDelete)
    {
        App_BuffPacketManager::instance()->Delete(pBuffPacket);
    }

    return true;
}

bool CConnectManagerGroup::PostMessage( vector<uint32> vecConnectID, const char* pData, uint32 nDataLen, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    uint32 u4ConnectID = 0;

    for(uint32 i = 0; i < (uint32)vecConnectID.size(); i++)
    {
        //判断命中到哪一个线程组里面去
        u4ConnectID = vecConnectID[i];
        uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

        CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

        if(NULL == pConnectManager)
        {
            OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
            continue;
        }

        //为每一个Connect设置发送对象数据包
        IBuffPacket* pBuffPacket = App_BuffPacketManager::instance()->Create();

        if(NULL == pBuffPacket)
        {
            continue;
        }

        pBuffPacket->WriteStream(pData, nDataLen);

        pConnectManager->PostMessage(u4ConnectID, pBuffPacket, u1SendType, u2CommandID, u1SendState, true, nServerID);
    }

    if(true == blDelete)
    {
        SAFE_DELETE_ARRAY(pData);
    }

    return true;
}

bool CConnectManagerGroup::CloseConnect(uint32 u4ConnectID, EM_Client_Close_status emStatus)
{

    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
        return false;
    }

    return pConnectManager->CloseConnect(u4ConnectID, emStatus);
}

bool CConnectManagerGroup::CloseConnectByClient(uint32 u4ConnectID)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
        return false;
    }

    return pConnectManager->Close(u4ConnectID);
}

_ClientIPInfo CConnectManagerGroup::GetClientIPInfo(uint32 u4ConnectID)
{
    _ClientIPInfo objClientIPInfo;
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]No find send Queue object.\n"));
        return objClientIPInfo;
    }

    return pConnectManager->GetClientIPInfo(u4ConnectID);
}

_ClientIPInfo CConnectManagerGroup::GetLocalIPInfo(uint32 u4ConnectID)
{
    _ClientIPInfo objClientIPInfo;
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetLocalIPInfo]No find send Queue object.\n"));
        return objClientIPInfo;
    }

    return pConnectManager->GetLocalIPInfo(u4ConnectID);
}


void CConnectManagerGroup::GetConnectInfo(vecClientConnectInfo& VecClientConnectInfo)
{
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL != pConnectManager)
        {
            pConnectManager->GetConnectInfo(VecClientConnectInfo);
        }
    }
}

int CConnectManagerGroup::GetCount()
{
    uint32 u4Count = 0;

    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL != pConnectManager)
        {
            u4Count += pConnectManager->GetCount();
        }
    }

    return u4Count;
}

void CConnectManagerGroup::CloseAll()
{
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL != pConnectManager)
        {
            pConnectManager->CloseAll();
        }
    }
}

bool CConnectManagerGroup::StartTimer()
{
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL != pConnectManager)
        {
            pConnectManager->StartTimer();
        }
    }

    return true;
}

bool CConnectManagerGroup::Close(uint32 u4ConnectID)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]No find send Queue object.\n"));
        return false;
    }

    return pConnectManager->Close(u4ConnectID);
}

bool CConnectManagerGroup::CloseUnLock(uint32 u4ConnectID)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]No find send Queue object.\n"));
        return false;
    }

    return pConnectManager->CloseUnLock(u4ConnectID);
}

const char* CConnectManagerGroup::GetError()
{
    return (char* )"";
}

void CConnectManagerGroup::SetRecvQueueTimeCost(uint32 u4ConnectID, uint32 u4TimeCost)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]No find send Queue object.\n"));
        return;
    }

    pConnectManager->SetRecvQueueTimeCost(u4ConnectID, u4TimeCost);
}

bool CConnectManagerGroup::PostMessageAll( IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    //全部群发
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL == pConnectManager)
        {
            OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
            continue;
        }

        pConnectManager->PostMessageAll(pBuffPacket, u1SendType, u2CommandID, u1SendState, false, nServerID);
    }

    //用完了就删除
    if(true == blDelete)
    {
        App_BuffPacketManager::instance()->Delete(pBuffPacket);
    }

    return true;
}

bool CConnectManagerGroup::PostMessageAll( const char* pData, uint32 nDataLen, uint8 u1SendType, uint16 u2CommandID, uint8 u1SendState, bool blDelete, int nServerID)
{
    IBuffPacket* pBuffPacket = App_BuffPacketManager::instance()->Create();

    if(NULL == pBuffPacket)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessageAll]pBuffPacket is NULL.\n"));

        if(blDelete == true)
        {
            SAFE_DELETE_ARRAY(pData);
        }

        return false;
    }
    else
    {
        pBuffPacket->WriteStream(pData, nDataLen);
    }

    //全部群发
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL == pConnectManager)
        {
            OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
            continue;
        }

        pConnectManager->PostMessageAll(pBuffPacket, u1SendType, u2CommandID, u1SendState, false, nServerID);
    }

    App_BuffPacketManager::instance()->Delete(pBuffPacket);

    //用完了就删除
    if(true == blDelete)
    {
        SAFE_DELETE_ARRAY(pData);
    }

    return true;
}

bool CConnectManagerGroup::SetConnectName(uint32 u4ConnectID, const char* pName)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
        return false;
    }

    return pConnectManager->SetConnectName(u4ConnectID, pName);
}

bool CConnectManagerGroup::SetIsLog(uint32 u4ConnectID, bool blIsLog)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
        return false;
    }

    return pConnectManager->SetIsLog(u4ConnectID, blIsLog);
}

void CConnectManagerGroup::GetClientNameInfo(const char* pName, vecClientNameInfo& objClientNameInfo)
{
    objClientNameInfo.clear();

    //全部查找
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL != pConnectManager)
        {
            pConnectManager->GetClientNameInfo(pName, objClientNameInfo);
        }
    }
}

void CConnectManagerGroup::GetCommandData( uint16 u2CommandID, _CommandData& objCommandData )
{
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL != pConnectManager)
        {
            _CommandData* pCommandData = pConnectManager->GetCommandData(u2CommandID);

            if(pCommandData != NULL)
            {
                objCommandData += (*pCommandData);
            }
        }
    }
}

void CConnectManagerGroup::GetCommandFlowAccount(_CommandFlowAccount& objCommandFlowAccount)
{
    for(uint16 i = 0; i < m_u2ThreadQueueCount; i++)
    {
        CConnectManager* pConnectManager = m_objConnnectManagerList[i];

        if(NULL != pConnectManager)
        {
            uint32 u4FlowOut = pConnectManager->GetCommandFlowAccount();
            objCommandFlowAccount.m_u4FlowOut += u4FlowOut;
        }
    }
}

EM_Client_Connect_status CConnectManagerGroup::GetConnectState(uint32 u4ConnectID)
{
    //判断命中到哪一个线程组里面去
    uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

    CConnectManager* pConnectManager = m_objConnnectManagerList[u2ThreadIndex];

    if(NULL == pConnectManager)
    {
        OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
        return CLIENT_CONNECT_NO_EXIST;
    }

    return pConnectManager->GetConnectState(u4ConnectID);
}
