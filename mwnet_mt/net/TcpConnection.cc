// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

#include <mwnet_mt/net/TcpConnection.h>

#include <mwnet_mt/base/Logging.h>
#include <mwnet_mt/base/WeakCallback.h>
#include <mwnet_mt/net/Channel.h>
#include <mwnet_mt/net/EventLoop.h>
#include <mwnet_mt/net/Socket.h>
#include <mwnet_mt/net/SocketsOps.h>

#include <errno.h>

using namespace mwnet_mt;
using namespace mwnet_mt::net;

// ����ʧ�ܴ�����
enum SEND_ERR_CODE
{
	SEND_SUCCESS=0,			// �ɹ�
	SEND_DISCONNECTED=1,	// ���Ӳ�����
	SEND_HIGHWATERMARK=2,	// ��������ͻ�����
	SEND_WRITEERR=3,		// writeʧ��
	SEND_TIMEOUT=4			// �����趨��ʱ����δ����ȥ
};

void mwnet_mt::net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
	LOG_TRACE << conn->localAddress().toIpPort() << " -> "
	        << conn->peerAddress().toIpPort() << " is "
	        << (conn->connected() ? "UP" : "DOWN");
	// do not call conn->forceClose(), because some users want to register message callback only.
}

void mwnet_mt::net::defaultMessageCallback(const TcpConnectionPtr&,
                                        Buffer* buf,
                                        Timestamp)
{
	buf->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop* loop, 
								int sockfd, 
								const InetAddress& localAddr, 
								const InetAddress& peerAddr, 
								size_t nDefRecvBuf,
								size_t nMaxRecvBuf,
								size_t nMaxSendQue)
: loop_(CHECK_NOTNULL(loop)),
state_(kConnecting),
reading_(true),
socket_(new Socket(sockfd)),
channel_(new Channel(loop, sockfd)),
localAddr_(localAddr),
peerAddr_(peerAddr),
highWaterMark_(512),
m_nDefRecvBuf(nDefRecvBuf),
m_nMaxRecvBuf(nMaxRecvBuf),
m_nMaxSendQue(nMaxSendQue),
inputBuffer_(nDefRecvBuf),
sendQueSize_(0)
{ 
	needclose_ = false,

	channel_->setReadCallback(
	  std::bind(&TcpConnection::handleRead, this, _1));
	channel_->setWriteCallback(
	  std::bind(&TcpConnection::handleWrite, this));
	channel_->setCloseCallback(
	  std::bind(&TcpConnection::handleClose, this));
	channel_->setErrorCallback(
	  std::bind(&TcpConnection::handleError, this));
	LOG_DEBUG << "TcpConnection::ctor[" <<  conn_uuid_ << "] at " << this
	        << " fd=" << sockfd;
	socket_->setKeepAlive(false);
}

TcpConnection::~TcpConnection()
{
	LOG_DEBUG << "TcpConnection::dtor[" << conn_uuid_ << "] at " << this
	        << " fd=" << channel_->fd()
	        << " state=" << stateToString();
	//assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const
{
	return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
	char buf[1024];
	buf[0] = '\0';
	socket_->getTcpInfoString(buf, sizeof buf);
	return buf;
}

int TcpConnection::send(const void* data, int len, const boost::any& params, int timeout)
{
	return send(StringPiece(static_cast<const char*>(data), len), params, timeout);
}

int TcpConnection::send(const StringPiece& message, const boost::any& params, int timeout)
{
	int ret = SEND_SUCCESS;

	if (loop_->isInLoopThread())
	{
		// ���Ӳ����û򳬳�����ͻ��壬����ʧ��
		state_==kConnected?ret=SEND_SUCCESS:1;
		sendQueSize_>=m_nMaxSendQue?ret=SEND_HIGHWATERMARK:1;
		if (SEND_SUCCESS == ret) 
		{
			sendInLoop(message, params, timeout);
		}
	}
	else
	{
		// ���Ӳ����û򳬳�����ͻ��壬����ʧ��
		state_==kConnected?ret=SEND_SUCCESS:1;
		sendQueSize_>=m_nMaxSendQue?ret=SEND_HIGHWATERMARK:1;
		if (SEND_SUCCESS == ret)
		{
			void (TcpConnection::*fp)(const StringPiece& message, const boost::any& params, int timeout) = &TcpConnection::sendInLoop;
			loop_->runInLoop(
				  			std::bind(fp,
				            this,     // FIXME
				            message.as_string(),
				            params,
				            timeout));
		}
	}
	
	return ret;
}

// FIXME efficiency!!!
int TcpConnection::send(Buffer* buf, const boost::any& params, int timeout)
{
	int ret = SEND_SUCCESS;

	if (loop_->isInLoopThread())
	{
		// ���Ӳ����û򳬳�����ͻ��壬����ʧ��
		state_==kConnected?ret=SEND_SUCCESS:1;
		sendQueSize_>=m_nMaxSendQue?ret=SEND_HIGHWATERMARK:1;
		if (SEND_SUCCESS == ret)
		{
			sendInLoop(buf->peek(), buf->readableBytes(), params, timeout);
			buf->retrieveAll();
		}
	}
	else
	{
		// ���Ӳ����û򳬳�����ͻ��壬����ʧ��
		state_==kConnected?ret=SEND_SUCCESS:1;
		sendQueSize_>=m_nMaxSendQue?ret=SEND_HIGHWATERMARK:1;
		if (SEND_SUCCESS == ret)
		{
			void (TcpConnection::*fp)(const StringPiece& message, const boost::any& params, int timeout) = &TcpConnection::sendInLoop;
			loop_->runInLoop(
							std::bind(fp,
							this,     // FIXME
							buf->retrieveAllAsString(),
							params,
							timeout));
		}
	}

	return ret;
}

void TcpConnection::sendInLoop(const StringPiece& message, const boost::any& params, int timeout)
{
	sendInLoop(message.data(), message.size(), params, timeout);
}

void TcpConnection::sendInLoop(const void* data, size_t len, const boost::any& params, int timeout)
{
	loop_->assertInLoopThread();

	if (state_ != kConnected)
	{
		// ʧ�ܻص�
		if (writeErrCallback_)
		{
			loop_->queueInLoop(std::bind(writeErrCallback_, shared_from_this(), params, SEND_DISCONNECTED));
		}

		LOG_WARN << "disconnected, give up writing";

		return;
	}  

	ssize_t nwrote = 0;
	size_t remaining = len;
	bool faultError = false;

	// if no thing in output queue, try writing directly
	if (!channel_->isWriting() && sendQueSize_ <= 0)
	{
		nwrote = sockets::write(channel_->fd(), data, len);
		if (nwrote >= 0)
		{
			remaining = len - nwrote;
			if (remaining == 0 && writeCompleteCallback_)
			{
				loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this(), params));
			}
		}
		else // nwrote < 0
		{
			nwrote = 0;
			if (errno != EWOULDBLOCK)
			{
				LOG_SYSERR << "TcpConnection::sendInLoop";
				if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
				{
					faultError = true;

					// ʧ�ܻص�
					if (writeErrCallback_)
					{
						loop_->queueInLoop(std::bind(writeErrCallback_, shared_from_this(), params, SEND_WRITEERR));
					}
				}
			}
		}
	}

	assert(remaining <= len);
	if (!faultError && remaining > 0)
	{
		//����û�з����
		OUTBUFFER_PTR output(new OUTPUT_STRUCT(remaining));
		output->params = params; 
		output->outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
		// �����Ҫ���ͳ�ʱ��ʱ�䣬����붨ʱ��
		if (timeout > 0)
		{
			output->timerid_ = loop_->runAfter(static_cast<int>(timeout)/1000.0, 
												std::bind(&TcpConnection::sendTimeoutCallBack, 
												shared_from_this(), --outputBufferList_.end()));
		}
		outputBufferList_.push_back(output);
		++sendQueSize_;

		// ������ˮλ?
		if (sendQueSize_ >= highWaterMark_ && highWaterMarkCallback_)
		{
			loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), sendQueSize_));
		}

		if (!channel_->isWriting())
		{
			channel_->enableWriting();
		}
	}
}

void TcpConnection::sendTimeoutCallBack(OUTBUFFER_LIST_IT it)
{
	// ʧ�ܻص�
	if (writeErrCallback_)
	{
		loop_->queueInLoop(std::bind(writeErrCallback_, shared_from_this(), (*it)->params, SEND_TIMEOUT));
	}
	(*it)->outputBuffer_.retrieveAll();
	outputBufferList_.erase(it);
	--sendQueSize_;
}

void TcpConnection::shutdown()
{
	// FIXME: use compare and swap
	if (state_ == kConnected)
	{
		setState(kDisconnecting);
		// FIXME: shared_from_this()?
		loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
	}
}

void TcpConnection::shutdownInLoop()
{
	loop_->assertInLoopThread();
	if (!channel_->isWriting())
	{
		// we are not writing
		socket_->shutdownWrite();
	}
}

void TcpConnection::forceClose()
{
	// FIXME: use compare and swap
	if (state_ == kConnected || state_ == kDisconnecting)
	{
		setState(kDisconnecting);
		loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
	}
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
	if (state_ == kConnected || state_ == kDisconnecting)
	{
		setState(kDisconnecting);
		loop_->runAfter(
		    seconds,
		    makeWeakCallback(shared_from_this(),
		                     &TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
	}
}

void TcpConnection::forceCloseInLoop()
{
	loop_->assertInLoopThread();
	if (state_ == kConnected || state_ == kDisconnecting)
	{
		// as if we received 0 byte in handleRead();
		handleClose();
	}
}

const char* TcpConnection::stateToString() const
{
	switch (state_)
	{
		case kDisconnected:
			return "kDisconnected";
		case kConnecting:
			return "kConnecting";
		case kConnected:
			return "kConnected";
		case kDisconnecting:
			return "kDisconnecting";
		default:
			return "unknown state";
	}
}

void TcpConnection::setTcpNoDelay(bool on)
{
	socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
	loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, shared_from_this()));
}

void TcpConnection::startReadInLoop()
{
	loop_->assertInLoopThread();
	if (!reading_ || !channel_->isReading())
	{
		channel_->enableReading();
		reading_ = true;
	}
}

void TcpConnection::stopRead()
{
	loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, shared_from_this()));
}

void TcpConnection::stopReadInLoop()
{
	loop_->assertInLoopThread();
	if (reading_ || channel_->isReading())
	{
		channel_->disableReading();
		reading_ = false;
	}
}

void TcpConnection::connectEstablished()
{
	loop_->assertInLoopThread();
	assert(state_ == kConnecting);
	setState(kConnected);
	channel_->tie(shared_from_this());
	channel_->enableReading();

	if (connectionCallback_)
	{
		connectionCallback_(shared_from_this());
	}
}

void TcpConnection::connectDestroyed()
{
	loop_->assertInLoopThread();
	if (state_ == kConnected)
	{
		setState(kDisconnected);
		channel_->disableAll();

		// �����ӹر�ǰ������δ���͵����ݻص�ʧ��
		callbackAllNoSendOutputBuffer();

		connectionCallback_(shared_from_this());
	}
	channel_->remove();
}

void TcpConnection::connectDestroyedWithLatch(const std::shared_ptr<CountDownLatch>& latch)
{
	loop_->assertInLoopThread();
	if (state_ == kConnected)
	{
		setState(kDisconnected);
		channel_->disableAll();

		// �����ӹر�ǰ������δ���͵����ݻص�ʧ��
		callbackAllNoSendOutputBuffer();

		connectionCallback_(shared_from_this());
	}
	channel_->remove();

	latch->countDown();
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
	loop_->assertInLoopThread();
	int savedErrno = 0;
	ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
	if (n > 0)
	{
		messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);

		inputBuffer_.shrink_if_need(m_nDefRecvBuf);
	}
	else if (n == 0)
	{
		LOG_WARN << "TcpConnection::handleRead readFd=" << n << ",close it,connuid:" << conn_uuid_;
		handleClose();
	}
	else
	{
		errno = savedErrno;
		handleError();
	}
}

void TcpConnection::handleWrite()
{
  loop_->assertInLoopThread();
  if (channel_->isWriting())
  {
  	OUTBUFFER_LIST_IT it = outputBufferList_.begin();
	if (it != outputBufferList_.end())
	{
	  	Buffer &outputBuffer = (*it)->outputBuffer_;
	  
	    ssize_t n = sockets::write(channel_->fd(),
	                               outputBuffer.peek(),
	                               outputBuffer.readableBytes());
	    if (n > 0)
	    {
			outputBuffer.retrieve(n);
			if (0 == outputBuffer.readableBytes())
			{
				// �����ʱ��
				loop_->cancel((*it)->timerid_);
								
				// �ص����
				if (writeCompleteCallback_)
				{
					loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this(), (*it)->params));
				}

				outputBufferList_.erase(it);

				// list�����һ���������������Ժ�ſ��Թر�ͨ����д����
				if (--sendQueSize_ <= 0)
				{
					channel_->disableWriting();
					if (state_ == kDisconnecting)
					{
						shutdownInLoop();
					}
				}
			}
	    }
	    else
	    {
	    	// �����ʱ��
			loop_->cancel((*it)->timerid_);
			
	    	// ʧ�ܻص�
			if (writeErrCallback_)
			{
				loop_->queueInLoop(std::bind(writeErrCallback_, shared_from_this(), (*it)->params, SEND_WRITEERR));
			}
			
			outputBuffer.retrieveAll();
			outputBufferList_.erase(it);

			// list�����һ���������������Ժ�ſ��Թر�ͨ����д����
			if (--sendQueSize_ <= 0)
			{
				channel_->disableWriting();
				if (state_ == kDisconnecting)
				{
					shutdownInLoop();
				}
			}
			LOG_SYSERR << "TcpConnection::handleWrite fail:" << conn_uuid_;
	    }
	}
  }
  else
  {
    LOG_TRACE << "Connection fd = " << channel_->fd()
              << " is down, no more writing";
  }
}

// ������δ��������ʧ�ܻص�
void TcpConnection::callbackAllNoSendOutputBuffer()
{
	return;
	//��δ��������ݻص�ʧ��
	OUTBUFFER_LIST_IT it = outputBufferList_.begin();
	for (;it != outputBufferList_.end();)
	{
		// �����ʱ��
		loop_->cancel((*it)->timerid_);
				
		// ʧ�ܻص�
		if (writeErrCallback_)
		{
			loop_->queueInLoop(std::bind(writeErrCallback_, shared_from_this(), (*it)->params, SEND_DISCONNECTED));
		}

		(*it)->outputBuffer_.retrieveAll();
		outputBufferList_.erase(it++);
		--sendQueSize_;
	}
}	

void TcpConnection::handleClose()
{
	loop_->assertInLoopThread();
	LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
	assert(state_ == kConnected || state_ == kDisconnecting);
	// we don't close fd, leave it to dtor, so we can find leaks easily.
	setState(kDisconnected);
	channel_->disableAll();

	// �����ӹر�ǰ������δ���͵����ݻص�ʧ��
	callbackAllNoSendOutputBuffer();

	TcpConnectionPtr guardThis(shared_from_this());
	connectionCallback_(guardThis);

	// must be the last line
	closeCallback_(guardThis);
}

void TcpConnection::handleClose2()
{
// 	loop_->assertInLoopThread();
// 	// LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
// 	assert(state_ == kConnected || state_ == kDisconnecting);
// 	// we don't close fd, leave it to dtor, so we can find leaks easily.
 	setState(kDisconnected);
 	channel_->disableAll();

	// �����ӹر�ǰ������δ���͵����ݻص�ʧ��
	callbackAllNoSendOutputBuffer();

	connectionCallback_(shared_from_this());
	// must be the last line
	// closeCallback_(shared_from_this());
}
void TcpConnection::handleError()
{
	int err = sockets::getSocketError(channel_->fd());
	LOG_ERROR << "TcpConnection::handleError [" << conn_uuid_
	        << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}
