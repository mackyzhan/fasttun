#include "KcpTunnel.h"

NAMESPACE_BEG(tun)

//--------------------------------------------------------------------------
static int kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user);

KcpTunnel::~KcpTunnel()
{
	shutdown();
}

bool KcpTunnel::create(uint32 conv)
{
	if (mKcpCb)
		shutdown();

	mConv = conv;
	mKcpCb = ikcp_create(mConv, this);
	if (NULL == mKcpCb)
		return false;

	mKcpCb->output = kcpOutput;
	return true;
}

void KcpTunnel::shutdown()
{
	if (mKcpCb)
	{
		ikcp_release(mKcpCb);
		mKcpCb = NULL;
	}
}

int KcpTunnel::send(const void *data, size_t datalen)
{
	return ikcp_send(mKcpCb, (const char *)data, datalen);
}

bool KcpTunnel::input(const void *data, size_t datalen)
{
	int ret = ikcp_input(mKcpCb, (const char *)data, datalen);
	return 0 == ret;
}

void KcpTunnel::update(uint32 current)
{
	ikcp_update(mKcpCb, current);

	core::MemoryStream buf;
	int oncelen = 1024;
	buf.reserve(oncelen);
	for (;;)
	{
		int recvlen = ikcp_recv(mKcpCb, (char *)(buf.data()+buf.wpos()), oncelen);
		if (recvlen <= 0)
		{
			break;
		}		
		else if (recvlen < oncelen)
		{
			buf.wpos(buf.wpos()+recvlen);
			break;			
		}
		else
		{
			buf.wpos(buf.wpos()+recvlen);
			buf.reserve(buf.length()+oncelen);
		}
	}

	if (buf.length() > 0 && mHandler)
	{
		mHandler->onRecv(this, buf.data(), buf.length());
	}
}

static int kcpOutput(const char *buf, int len, ikcpcb *kcp, void *user)
{
	KcpTunnel *pTunnel = (KcpTunnel *)user;
	if (pTunnel)
	{
		KcpTunnelGroup *pGroup = pTunnel->getGroup();
		if (pGroup)
		{
			int sentlen = pGroup->_send(buf, len);
			if (sentlen != len)
			{
				logErrorLn("kcpoutput() send error! conv="<<pTunnel->getConv()<<" datalen="<<len<<" sentlen="<<sentlen);
			}
		}
	}
	return 0;
}
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
KcpTunnelGroup::~KcpTunnelGroup()
{
}

bool KcpTunnelGroup::initialise(const char *localaddr, const char *remoteaddr)
{
	// assign the local address
	if (!core::getSocketAddress(localaddr, mLocalAddr))
	{
		logErrorLn("KcpTunnelGroup::initialise() localaddr format error! "<<localaddr);
		return false;
	}

	// assign the remote address
	if (!core::getSocketAddress(remoteaddr, mRemoteAddr))
	{
		logErrorLn("KcpTunnelGroup::initialise() remoteaddr format error! "<<remoteaddr);
		return false;
	}

	// create socket
	mFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (mFd < 0)
		return false;

	// set nonblocking
	if (!core::setNonblocking(mFd))
		return false;

	// bind local address
	if (bind(mFd, (SA *)&mLocalAddr, sizeof(mLocalAddr)) < 0)
	{
		logErrorLn("KcpTunnelGroup::initialise() bind local address err! "<<coreStrError());
		return false;
	}

	return true;
}

void KcpTunnelGroup::finalise()
{
	Tunnels::iterator it = mTunnels.begin();
	for (; it != mTunnels.end(); ++it)
	{
		KcpTunnel *pTunnel = it->second;
		if (pTunnel)
		{
			pTunnel->shutdown();
			delete pTunnel;
		}
	}
	mTunnels.clear();
	
	if (mFd >= 0)
	{
		close(mFd);
		mFd = -1;
	}
}

int KcpTunnelGroup::_send(const void *data, size_t datalen)
{
	return sendto(mFd, data, datalen, 0, (SA *)&mRemoteAddr, sizeof(mRemoteAddr));
}

KcpTunnel* KcpTunnelGroup::createTunnel(uint32 conv)
{
	Tunnels::iterator it = mTunnels.find(conv);
	if (it != mTunnels.end())
	{
		logErrorLn("KcpTunnelGroup::createTunnel() tunnul already exist! conv="<<conv);
		return NULL;
	}
	
	KcpTunnel *pTunnel = new KcpTunnel(this);
	if (!pTunnel->create(conv))
	{
		return NULL;
	}

	mTunnels.insert(std::pair<uint32, KcpTunnel *>(conv, pTunnel));
	return pTunnel;
}

void KcpTunnelGroup::destroyTunnel(KcpTunnel *pTunnel)
{	
	uint32 conv = pTunnel->getConv();

	pTunnel->shutdown();
	Tunnels::iterator it = mTunnels.find(conv);
	if (it != mTunnels.end())
	{
		mTunnels.erase(it);
	}
}

void KcpTunnelGroup::update()
{
	if (mFd < 0)
	{
		logErrorLn("KcpTunnelGroup::update() we did not create socket yet!");
		return;
	}

	// recv data from internet
	core::MemoryStream buf;
	int oncelen = 1024;
	buf.reserve(oncelen);
	for (;;)
	{
		sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);
		int recvlen = recvfrom(mFd, (char *)(buf.data()+buf.wpos()), oncelen, 0, (SA *)&addr, &addrlen);
		if (recvlen <= 0)
		{
			break;
		}		
		else if (recvlen < oncelen)
		{
			buf.wpos(buf.wpos()+recvlen);
			break;			
		}
		else
		{
			buf.wpos(buf.wpos()+recvlen);
			buf.reserve(buf.length()+oncelen);
		}
	}

	if (buf.length() > 0)
	{
		bool bAccepted = false;
		Tunnels::iterator it = mTunnels.begin();
		for (; it != mTunnels.end(); ++it)
		{
			KcpTunnel *pTunnel = it->second;
			if (pTunnel && pTunnel->input(buf.data(), buf.length()))
			{
				bAccepted = true;
				break;
			}
		}

		if (!bAccepted)
		{
			logErrorLn("KcpTunnel() got a stream that has no acceptor! datalen="<<buf.length());
			// buf.hexlike();
			// buf.textlike();
		}
	}

	// update all tunnels
	uint32 current = core::getTickCount();
	Tunnels::iterator it = mTunnels.begin();
	for (; it != mTunnels.end(); ++it)
	{
		KcpTunnel *pTunnel = it->second;
		if (pTunnel)
			pTunnel->update(current);
	}
}
//--------------------------------------------------------------------------

NAMESPACE_END // namespace tun