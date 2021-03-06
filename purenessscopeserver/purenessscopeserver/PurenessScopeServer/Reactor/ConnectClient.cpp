#include "ConnectClient.h"
#include "ClientReConnectManager.h"

CConnectClient::CConnectClient(void)
{
    m_pCurrMessage      = NULL;
    m_nIOCount          = 1;
    m_nServerID         = 0;
    m_u4SendSize        = 0;
    m_u4SendCount       = 0;
    m_u4RecvSize        = 0;
    m_u4RecvCount       = 0;
    m_u4CostTime        = 0;
    m_u4MaxPacketSize   = MAX_MSG_PACKETLENGTH;
    m_ems2s             = S2S_NEED_CALLBACK;

    m_emRecvState       = SERVER_RECV_INIT;
}

CConnectClient::~CConnectClient(void)
{
}

bool CConnectClient::Close()
{
    m_ThreadLock.acquire();

    if (m_nIOCount > 0)
    {
        m_nIOCount--;
    }

    m_ThreadLock.release();

    //从反应器注销事件
    if (m_nIOCount == 0)
    {
        //msg_queue()->deactivate();
        shutdown();
        OUR_DEBUG((LM_ERROR, "[CConnectClient::Close]Close(%s:%d) OK.\n", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number()));

        //删除链接对象
        App_ClientReConnectManager::instance()->CloseByClient(m_nServerID);

        //回归用过的指针
        delete this;
        return true;
    }

    return false;
}

void CConnectClient::ClientClose(EM_s2s& ems2s)
{
    m_ems2s = ems2s;
    //msg_queue()->deactivate();

    //如果对象已经在外面释放，则不需要再次回调
    if(ems2s == S2S_INNEED_CALLBACK)
    {
        SetClientMessage(NULL);
    }

    shutdown();
}

int CConnectClient::open(void* p)
{
    //从配置文件获取数据
    m_u4MaxPacketSize  = App_MainConfig::instance()->GetRecvBuffSize();

    if (p != NULL)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectClient::open]p is not NULL.\n"));
    }

    ACE_Time_Value nowait(MAX_MSG_PACKETTIMEOUT);
    m_nIOCount = 1;
    int nRet = Super::open();

    if (nRet != 0)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectClient::open]ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open() error [%d].\n", nRet));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectClient::open]ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open() error [%d].", nRet);
        return -1;
    }

    //设置链接为非阻塞模式
    if (this->peer().enable(ACE_NONBLOCK) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]this->peer().enable  = ACE_NONBLOCK error.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectClient::open]this->peer().enable  = ACE_NONBLOCK error.");
        return -1;
    }

    //获得远程链接地址和端口
    if (this->peer().get_remote_addr(m_addrRemote) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]this->peer().get_remote_addr error.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectClient::open]this->peer().get_remote_addr error.");
        return -1;
    }

    m_u4SendSize        = 0;
    m_u4SendCount       = 0;
    m_u4RecvSize        = 0;
    m_u4RecvCount       = 0;
    m_u4CostTime        = 0;
    m_atvBegin          = ACE_OS::gettimeofday();
    m_u4CurrSize        = 0;
    //申请当前的MessageBlock
    m_pCurrMessage = App_MessageBlockManager::instance()->Create(App_MainConfig::instance()->GetConnectServerRecvBuffer());

    if (m_pCurrMessage == NULL)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectClient::RecvClinetPacket] pmb new is NULL.\n"));
        return -1;
    }

    App_ClientReConnectManager::instance()->SetHandler(m_nServerID, this);
    m_pClientMessage = App_ClientReConnectManager::instance()->GetClientMessage(m_nServerID);
    OUR_DEBUG((LM_INFO, "[CConnectClient::open] Connection from [%s:%d]\n", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number()));

    this->reactor()->register_handler(this, ACE_Event_Handler::READ_MASK);

    return 0;
}

int CConnectClient::handle_input(ACE_HANDLE fd)
{
    if (fd == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectClient::handle_input]fd == ACE_INVALID_HANDLE.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input]fd == ACE_INVALID_HANDLE.");

        if (NULL != m_pClientMessage)
        {
            _ClientIPInfo objServerIPInfo;
            sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
            objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

            if(S2S_NEED_CALLBACK == m_ems2s)
            {
                m_pClientMessage->ConnectError((int)ACE_OS::last_error(), objServerIPInfo);
            }
        }

        return -1;
    }

    //判断缓冲是否为NULL
    if (m_pCurrMessage == NULL)
    {
        m_u4CurrSize = 0;
        OUR_DEBUG((LM_ERROR, "[CConnectClient::handle_input]m_pCurrMessage == NULL.\n"));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectClient::handle_input]m_pCurrMessage == NULL.");

        if (NULL != m_pClientMessage)
        {
            _ClientIPInfo objServerIPInfo;
            sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
            objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

            if(S2S_NEED_CALLBACK == m_ems2s)
            {
                m_pClientMessage->ConnectError((int)ACE_OS::last_error(), objServerIPInfo);
            }
        }

        return -1;
    }

    if(App_MainConfig::instance()->GetNetworkMode() != (uint8)NETWORKMODE_RE_EPOLL_ET)
    {
        return RecvData();
    }
    else
    {
        return RecvData_et();
    }
}

int CConnectClient::RecvData()
{
    ACE_Time_Value nowait(MAX_MSG_PACKETTIMEOUT);

    int nCurrCount = (uint32)m_pCurrMessage->size();

    if (nCurrCount < 0)
    {
        //如果剩余字节为负，说明程序出了问题
        OUR_DEBUG((LM_ERROR, "[CConnectClient::handle_input][%d] nCurrCount < 0 m_u4CurrSize = %d.\n", GetServerID(), m_u4CurrSize));
        m_u4CurrSize = 0;

        if (NULL != m_pClientMessage)
        {
            _ClientIPInfo objServerIPInfo;
            sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
            objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

            if(S2S_NEED_CALLBACK == m_ems2s)
            {
                m_pClientMessage->ConnectError((int)ACE_OS::last_error(), objServerIPInfo);
            }
        }

        return -1;
    }

    int nDataLen = this->peer().recv(m_pCurrMessage->wr_ptr(), nCurrCount, MSG_NOSIGNAL, &nowait);

    if (nDataLen <= 0)
    {
        m_u4CurrSize = 0;
        uint32 u4Error = (uint32)errno;
        OUR_DEBUG((LM_ERROR, "[CConnectClient::handle_input] ConnectID = %d, recv data is error nDataLen = [%d] errno = [%d].\n", GetServerID(), nDataLen, u4Error));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectClient::handle_input] ConnectID = %d, recv data is error[%d].\n", GetServerID(), nDataLen);

        if (NULL != m_pClientMessage)
        {
            _ClientIPInfo objServerIPInfo;
            sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
            objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

            if(S2S_NEED_CALLBACK == m_ems2s)
            {
                m_pClientMessage->ConnectError((int)ACE_OS::last_error(), objServerIPInfo);
            }
        }

        return -1;
    }

    //如果是DEBUG状态，记录当前接受包的二进制数据
    if (App_MainConfig::instance()->GetDebug() == DEBUG_ON)
    {
        char szDebugData[MAX_BUFF_1024] = {'\0'};
        char szLog[10]  = {'\0'};
        int  nDebugSize = 0;
        bool blblMore   = false;

        if (nDataLen >= MAX_BUFF_200)
        {
            nDebugSize = MAX_BUFF_200;
            blblMore   = true;
        }
        else
        {
            nDebugSize = nDataLen;
        }

        char* pData = m_pCurrMessage->wr_ptr();

        for (int i = 0; i < nDebugSize; i++)
        {
            sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
            sprintf_safe(szDebugData + 5 * i, MAX_BUFF_1024 - 5 * i, "%s", szLog);
        }

        if (blblMore == true)
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_SERVERRECV, "[%s:%d]%s.(数据包过长只记录前200字节)", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
        else
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_SERVERRECV, "[%s:%d]%s.", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
    }

    m_pCurrMessage->wr_ptr(nDataLen);

    if (NULL != m_pClientMessage)
    {
        //接收数据，返回给逻辑层，自己不处理整包完整性判定
        _ClientIPInfo objServerIPInfo;
        sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
        objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

        uint16 u2CommandID             = 0;
        ACE_Message_Block* pRecvFinish = NULL;

        m_atvRecv     = ACE_OS::gettimeofday();
        m_emRecvState = SERVER_RECV_BEGIN;

        while(true)
        {
            bool blRet = m_pClientMessage->Recv_Format_data(m_pCurrMessage, App_MessageBlockManager::instance(), u2CommandID, pRecvFinish);

            if(true == blRet)
            {
                if(App_MainConfig::instance()->GetConnectServerRunType() == 0)
                {
                    //调用数据包处理
                    m_pClientMessage->RecvData(u2CommandID, pRecvFinish, objServerIPInfo);
                    //回收处理包
                    App_MessageBlockManager::instance()->Close(pRecvFinish);
                }
                else
                {
                    //异步消息处理
                    _Server_Message_Info* pServer_Message_Info = App_ServerMessageInfoPool::instance()->Create();
                    pServer_Message_Info->m_pClientMessage  = m_pClientMessage;
                    pServer_Message_Info->m_objServerIPInfo = objServerIPInfo;
                    pServer_Message_Info->m_pRecvFinish     = pRecvFinish;
                    pServer_Message_Info->m_u2CommandID     = u2CommandID;
                    App_ServerMessageTask::instance()->PutMessage(pServer_Message_Info);
                }
            }
            else
            {
                break;
            }
        }

        m_emRecvState = SERVER_RECV_END;
    }

    m_pCurrMessage->reset();

    return 0;
}

int CConnectClient::RecvData_et()
{
    int nCurrCount = (uint32)m_pCurrMessage->size();

    if (nCurrCount < 0)
    {
        //如果剩余字节为负，说明程序出了问题
        OUR_DEBUG((LM_ERROR, "[CConnectClient::handle_input][%d] nCurrCount < 0 m_u4CurrSize = %d.\n", GetServerID(), m_u4CurrSize));
        m_u4CurrSize = 0;

        if (NULL != m_pClientMessage)
        {
            _ClientIPInfo objServerIPInfo;
            sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
            objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

            if(S2S_NEED_CALLBACK == m_ems2s)
            {
                m_pClientMessage->ConnectError((int)ACE_OS::last_error(), objServerIPInfo);
            }
        }

        return -1;
    }

    while(true)
    {
        int nDataLen = this->peer().recv(m_pCurrMessage->wr_ptr(), nCurrCount, MSG_NOSIGNAL);

        if (nDataLen <= 0)
        {
            m_u4CurrSize = 0;
            uint32 u4Error = (uint32)errno;

            if(nDataLen == -1 && u4Error == EAGAIN)
            {
                break;
            }

            OUR_DEBUG((LM_ERROR, "[CConnectClient::handle_input] ConnectID = %d, recv data is error nDataLen = [%d] errno = [%d].\n", GetServerID(), nDataLen, u4Error));
            sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectClient::handle_input] ConnectID = %d, recv data is error[%d].\n", GetServerID(), nDataLen);

            if (NULL != m_pClientMessage)
            {
                _ClientIPInfo objServerIPInfo;
                sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
                objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

                if(S2S_NEED_CALLBACK == m_ems2s)
                {
                    m_pClientMessage->ConnectError((int)ACE_OS::last_error(), objServerIPInfo);
                }
            }

            return -1;
        }

        //如果是DEBUG状态，记录当前接受包的二进制数据
        if (App_MainConfig::instance()->GetDebug() == DEBUG_ON)
        {
            char szDebugData[MAX_BUFF_1024] = {'\0'};
            char szLog[10]  = {'\0'};
            int  nDebugSize = 0;
            bool blblMore   = false;

            if (nDataLen >= MAX_BUFF_200)
            {
                nDebugSize = MAX_BUFF_200;
                blblMore   = true;
            }
            else
            {
                nDebugSize = nDataLen;
            }

            char* pData = m_pCurrMessage->wr_ptr();

            for (int i = 0; i < nDebugSize; i++)
            {
                sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
                sprintf_safe(szDebugData + 5 * i, MAX_BUFF_1024 - 5 * i, "%s", szLog);
            }

            if (blblMore == true)
            {
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_SERVERRECV, "[%s:%d]%s.(数据包过长只记录前200字节)", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
            }
            else
            {
                AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_SERVERRECV, "[%s:%d]%s.", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
            }
        }

        m_pCurrMessage->wr_ptr(nDataLen);

        if (NULL != m_pClientMessage)
        {
            //接收数据，返回给逻辑层，自己不处理整包完整性判定
            _ClientIPInfo objServerIPInfo;
            sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
            objServerIPInfo.m_nPort = m_addrRemote.get_port_number();

            uint16 u2CommandID             = 0;
            ACE_Message_Block* pRecvFinish = NULL;

            m_atvRecv     = ACE_OS::gettimeofday();
            m_emRecvState = SERVER_RECV_BEGIN;

            while(true)
            {
                bool blRet = m_pClientMessage->Recv_Format_data(m_pCurrMessage, App_MessageBlockManager::instance(), u2CommandID, pRecvFinish);

                if(true == blRet)
                {
                    if(App_MainConfig::instance()->GetConnectServerRunType() == 0)
                    {
                        //调用数据包处理
                        m_pClientMessage->RecvData(u2CommandID, pRecvFinish, objServerIPInfo);
                        //回收处理包
                        App_MessageBlockManager::instance()->Close(pRecvFinish);
                    }
                    else
                    {
                        //异步消息处理
                        _Server_Message_Info* pServer_Message_Info = new _Server_Message_Info();
                        pServer_Message_Info->m_pClientMessage  = m_pClientMessage;
                        pServer_Message_Info->m_objServerIPInfo = objServerIPInfo;
                        pServer_Message_Info->m_pRecvFinish     = pRecvFinish;
                        pServer_Message_Info->m_u2CommandID     = u2CommandID;
                        App_ServerMessageTask::instance()->PutMessage(pServer_Message_Info);
                    }
                }
                else
                {
                    break;
                }
            }

            m_emRecvState = SERVER_RECV_END;
        }

        m_pCurrMessage->reset();
    }

    return 0;
}

int CConnectClient::handle_close(ACE_HANDLE h, ACE_Reactor_Mask mask)
{
    if (h == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_DEBUG, "[CConnectClient::handle_close] h is NULL mask=%d.\n", GetServerID(), (int)mask));
    }

    Close();
    return 0;
}

void CConnectClient::SetClientMessage(IClientMessage* pClientMessage)
{
    m_pClientMessage = pClientMessage;
}

void CConnectClient::SetServerID(int nServerID)
{
    m_nServerID = nServerID;
}

int CConnectClient::GetServerID()
{
    return m_nServerID;
}

bool CConnectClient::SendData(ACE_Message_Block* pmblk)
{
    //如果是DEBUG状态，记录当前接受包的二进制数据
    if (App_MainConfig::instance()->GetDebug() == DEBUG_ON)
    {
        char szDebugData[MAX_BUFF_1024] = {'\0'};
        char szLog[10]  = {'\0'};
        int  nDebugSize = 0;
        bool blblMore   = false;

        if (pmblk->length() >= MAX_BUFF_200)
        {
            nDebugSize = MAX_BUFF_200;
            blblMore   = true;
        }
        else
        {
            nDebugSize = (int)pmblk->length();
        }

        char* pData = pmblk->rd_ptr();

        for (int i = 0; i < nDebugSize; i++)
        {
            sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
            sprintf_safe(szDebugData + 5 * i, MAX_BUFF_1024 - 5 * i, "%s", szLog);
        }

        if (blblMore == true)
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[%s:%d]%s.(数据包过长只记录前200字节)", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
        else
        {
            AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[%s:%d]%s.", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
        }
    }

    ACE_Time_Value     nowait(MAX_MSG_PACKETTIMEOUT);

    if (NULL == pmblk)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectClient::SendData] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetServerID()));
        return false;
    }

    if (get_handle() == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectClient::SendData] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetServerID()));
        sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectClient::SendData] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetServerID());
        App_MessageBlockManager::instance()->Close(pmblk);
        return false;
    }

    char* pData = pmblk->rd_ptr();

    if (NULL == pData)
    {
        OUR_DEBUG((LM_ERROR, "[CConnectClient::SendData] ConnectID = %d, pData is NULL.\n", GetServerID()));
        App_MessageBlockManager::instance()->Close(pmblk);
        return false;
    }

    int nSendLen = (int)pmblk->length();   //发送数据的总长度
    int nIsSendSize = 0;

    //循环发送，直到数据发送完成。
    while (true)
    {
        if (nSendLen <= 0)
        {
            OUR_DEBUG((LM_ERROR, "[CConnectClient::SendData] ConnectID = %d, nCurrSendSize error is %d.\n", GetServerID(), nSendLen));
            App_MessageBlockManager::instance()->Close(pmblk);
            return false;
        }

        int nDataLen = this->peer().send(pmblk->rd_ptr(), nSendLen - nIsSendSize, &nowait);

        if (nDataLen <= 0)
        {
            /*
            if (nErr == EWOULDBLOCK)
            {
                //如果发送堵塞，则等10毫秒后再发送。
                ACE_Time_Value tvSleep(0, 10 * MAX_BUFF_1000);
                ACE_OS::sleep(tvSleep);
                continue;
            }
            */

            _ClientIPInfo objServerIPInfo;
            sprintf_safe(objServerIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
            objServerIPInfo.m_nPort = m_addrRemote.get_port_number();
            m_pClientMessage->ConnectError((int)ACE_OS::last_error(), objServerIPInfo);

            OUR_DEBUG((LM_ERROR, "[CConnectClient::SendData] ConnectID = %d, error = %d.\n", GetServerID(), errno));
            App_MessageBlockManager::instance()->Close(pmblk);
            return false;
        }
        else if (nDataLen + nIsSendSize >= nSendLen)  //当数据包全部发送完毕，清空。
        {
            //OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_output] ConnectID = %d, send (%d) OK.\n", GetConnectID(), msg_queue()->is_empty()));
            App_MessageBlockManager::instance()->Close(pmblk);
            m_u4SendSize += (uint32)nSendLen;
            m_u4SendCount++;
            return true;
        }
        else
        {
            pmblk->rd_ptr(nDataLen);
            nIsSendSize      += nDataLen;
            continue;
        }
    }

    return true;
}

_ClientConnectInfo CConnectClient::GetClientConnectInfo()
{
    _ClientConnectInfo ClientConnectInfo;
    ClientConnectInfo.m_blValid       = true;
    ClientConnectInfo.m_addrRemote    = m_addrRemote;
    ClientConnectInfo.m_u4AliveTime   = (uint32)(ACE_OS::gettimeofday().sec() - m_atvBegin.sec());
    ClientConnectInfo.m_u4AllRecvSize = m_u4RecvSize;
    ClientConnectInfo.m_u4RecvCount   = m_u4RecvCount;
    ClientConnectInfo.m_u4AllSendSize = m_u4SendSize;
    ClientConnectInfo.m_u4SendCount   = m_u4SendCount;
    ClientConnectInfo.m_u4ConnectID   = m_nServerID;
    ClientConnectInfo.m_u4BeginTime   = (uint32)m_atvBegin.sec();
    return ClientConnectInfo;
}

bool CConnectClient::GetTimeout(ACE_Time_Value tvNow)
{
    ACE_Time_Value tvIntval(tvNow - m_atvRecv);

    if(m_emRecvState == SERVER_RECV_BEGIN && tvIntval.sec() > SERVER_RECV_TIMEOUT)
    {
        //接收数据处理已经超时，在这里打印出来
        OUR_DEBUG((LM_DEBUG,"[CConnectClient::GetTimeout]***(%d)recv dispose is timeout(%d)!***.\n", m_nServerID, tvIntval.sec()));
        return false;
    }
    else
    {
        return true;
    }
}


