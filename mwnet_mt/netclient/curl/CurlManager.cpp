#include "CurlManager.h"
#include "RequestManager.h"
#include <mwnet_mt/base/Atomic.h>
#include <mwnet_mt/base/Logging.h>
#include <mwnet_mt/net/SocketsOps.h>
#include <assert.h>
#include <signal.h>
#include <sys/eventfd.h>

using namespace curl;
// ������Ϣ��ͳ����
mwnet_mt::AtomicUint64 totalCount_;

void CurlManager::initialize(Option opt)
{
	curl_global_init(opt == kCURLnossl ? CURL_GLOBAL_NOTHING : CURL_GLOBAL_SSL);
	//curl_global_init(CURL_GLOBAL_ALL);
}

void CurlManager::uninitialize()
{
	curl_global_cleanup();
}

void CurlManager::addEvLoop(void* p, int fd, int what)
{
	// �����¼�ѭ��
	curl_evmgr_->addEvLoop(p, fd, what,
							std::bind(&CurlManager::onReadEventCallBack, this, fd),
							std::bind(&CurlManager::onWriteEventCallBack, this, fd));
	// ����what��ֵ,ע��Ҫ��Ӧ���¼�
	curl_evmgr_->optEvLoop(p, fd, what);
}

void CurlManager::optEvLoop(void* p, int fd, int what)
{
	// ����what��ֵ,ע��Ҫ��Ӧ���¼�
	curl_evmgr_->optEvLoop(p, fd, what);
}

void CurlManager::delEvLoop(void* p, int fd, int what)
{
	// �Ƴ��¼�ѭ��
	curl_evmgr_->delEvLoop(p, fd, what);
}

int CurlManager::curlmSocketOptCbInLoop(CURL* c, int fd, int what, void* socketp)
{
	const char *whatstr[]={ "none", "IN", "OUT", "INOUT", "REMOVE" };
	LOG_DEBUG << "CurlManager::curlmSocketOptCbInLoop [" << this << "] [" << socketp << "] fd=" << fd
			  << " what=" << whatstr[what];

	// �¼��Ƴ�
	if (what == CURL_POLL_REMOVE) 
	{
		// ����������Ϊ���ڲ��׽��ֽ������
		curl_multi_assign(curlm_, fd, NULL);
		// ����loop,�Ƴ��¼�ѭ��
		loop_->queueInLoop(std::bind(&CurlManager::delEvLoop, this, socketp, fd, what));
	}
	else 
	{
		if (!socketp)
		{
			// ����EV
			void* p = curl_evmgr_->getNewEv(fd);
			// ����������Ϊ���ڲ��׽������ӹ���
			CURLMcode rc = curl_multi_assign(curlm_, fd, p);
			if (0 == rc)
			{
				LOG_DEBUG << "curl_multi_assign:" << fd 
						<< "-" << p
						<< "-" << rc 
						<< "-" << curl_multi_strerror(rc);
				
				// ����loop,�����¼�ѭ��
				loop_->queueInLoop(std::bind(&CurlManager::addEvLoop, this, p, fd, what));
			}
			else
			{
				LOG_DEBUG << "curl_multi_assign:" << fd 
						<< "-" << p
						<< "-" << rc 
						<< "-" << curl_multi_strerror(rc);
				
				curl_evmgr_->delEv(p);
			}
		}
		else
		{
			// ����loop,����what��ֵ,ע��Ҫ��Ӧ���¼�
			loop_->queueInLoop(std::bind(&CurlManager::optEvLoop, this, socketp, fd, what));
		}
	}
	
	return 0;
}

int CurlManager::curlmSocketOptCb(CURL* c, int fd, int what, void* userp, void* socketp)
{
	CurlManager* cm = static_cast<CurlManager*>(userp);
	
	if (NULL == cm || fd <= 0) return 0;
	
	cm->curlmSocketOptCbInLoop(c, fd, what, socketp);
		
	return 0;
}

int CurlManager::curlmTimerCb(CURLM* curlm, long ms, void* userp)
{	
	LOG_DEBUG << "curlmTimerCb::CurlManager:" << userp << " ms:" << ms << " ms";
	
	CurlManager* cm = static_cast<CurlManager*>(userp);
	if (NULL != cm)
	{		
		// ms>0 ms=-1�����ô���
		if (ms == 0)
		{
			// loop��ִ��			
			cm->loop_->runInLoop(std::bind(&CurlManager::curlmTimerCbInLoop, cm, 0L));
		}
		else if (ms > 0)
		{
			// ����timer
			cm->loop_->queueInLoop(std::bind(&CurlManager::createTimer, cm, ms));
		}
		else
		{	
			// ȡ��timer
			cm->loop_->queueInLoop(std::bind(&CurlManager::cancelTimer, cm));
		}
	}
	
	return 0;
}

void CurlManager::cancelTimer()
{
	if (timerid_.getSeq() > 0) loop_->cancel(timerid_);
}

void CurlManager::createTimer(long ms)
{
	// ��ȡ��timer
	if (timerid_.getSeq() > 0) loop_->cancel(timerid_);
	
	// �ٴ���
	timerid_ = loop_->runAfter(static_cast<int>(ms)/1000.0, std::bind(&CurlManager::curlmTimerCbInLoop, this, ms));
}

void CurlManager::curlmTimerCbInLoop(long ms)
{
	LOG_DEBUG << "CurlManager::curlmTimerCbInLoop:" << ms << "ms";

	if (0 == ms)
	{
		// ��ȡ������timer
		if (timerid_.getSeq() > 0) loop_->cancel(timerid_);
	
		curl_multi_socket_action(curlm_, CURL_SOCKET_TIMEOUT, 0, &runningHandles_);
	}
	else
	{
		curl_multi_socket_action(curlm_, CURL_SOCKET_TIMEOUT, 0, &runningHandles_);
		check_multi_info();
	}
	
}

int createEventfd()
{
	int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (evtfd < 0)
	{
		LOG_SYSERR << "Failed in eventfd";
		abort();
	}
	return evtfd;
}

CurlManager::CurlManager(EventLoop* loop)
	: loop_(loop),
	  curl_evmgr_(new CurlEvMgr(loop)),
	  wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(loop, wakeupFd_)),
	  curlm_(CHECK_NOTNULL(curl_multi_init())),
	  runningHandles_(0),
	  prevRunningHandles_(0)
{
	curl_multi_setopt(curlm_, CURLMOPT_SOCKETFUNCTION, &CurlManager::curlmSocketOptCb);
	curl_multi_setopt(curlm_, CURLMOPT_SOCKETDATA, this);
	curl_multi_setopt(curlm_, CURLMOPT_TIMERFUNCTION, &CurlManager::curlmTimerCb);
	curl_multi_setopt(curlm_, CURLMOPT_TIMERDATA, this);

	// curl 7.30���ϰ汾��֧��
	//if (maxtotalconns > 0) curl_multi_setopt(curlm_, CURLMOPT_MAX_TOTAL_CONNECTIONS, maxtotalconns);
	//if (maxhostconns > 0) curl_multi_setopt(curlm_, CURLMOPT_MAX_HOST_CONNECTIONS, maxhostconns);
	//if (maxconns > 0) curl_multi_setopt(curlm_, CURLMOPT_MAXCONNECTS, maxconns);

	// ���û��ѻص�
	wakeupChannel_->setReadCallback(std::bind(&CurlManager::onRecvWakeUpNotify, this));
	loop_->runInLoop(std::bind(&Channel::enableReading, get_pointer(wakeupChannel_)));
}

CurlManager::~CurlManager()
{
	curl_multi_cleanup(curlm_);
}

CurlRequestPtr CurlManager::getRequest(const std::string& url, bool bKeepAlive, int req_type, int http_ver)
{
	uint64_t req_uuid = totalCount_.incrementAndGet();

	// �ȴӻ���վ���ã��޿��õ���new
	CurlRequestPtr p = HttpRecycleRequest::GetInstance().get();
	
	if (nullptr == p)
	{
		p.reset(new CurlRequest(url, req_uuid, bKeepAlive, req_type, http_ver));
	}
	else
	{
		p->initCurlRequest(url, req_uuid, bKeepAlive, req_type, http_ver);
	}

	LOG_DEBUG << "getRequest requuid:" << req_uuid;
		
	return p;
}

void CurlManager::notifySendRequest()
{
	uint64_t one = 1;
	sockets::write(wakeupFd_, &one, sizeof one);
}

void CurlManager::onRecvWakeUpNotify()
{
	uint64_t one = 1;
	sockets::read(wakeupFd_, &one, sizeof one);
	
	// ���Ѻ󣬴Ӵ���������ȡ���ݷ���
	checkNeedSendRequest();
}

// ��������
int CurlManager::sendRequest(const CurlRequestPtr& request)
{
	int nRet = 0;

	request->req_inque_time_ = CurlRequest::now();
	
	// �����������
	if (!HttpWaitRequest::GetInstance().add(request))
	{
		nRet = 1;
	}
	
	// ֪ͨ����
	notifySendRequest();

	return nRet;
}

void CurlManager::sendRequestInLoop(const CurlRequestPtr& request)
{
	// �ȼ��뷢����
	HttpRequesting::GetInstance().add(request);
	// ��������
	request->request(curlm_, loop_);
}

void CurlManager::onReadEventCallBack(int fd)
{
	LOG_DEBUG << "onReadEventCallBack";

	curl_multi_socket_action(curlm_, fd, CURL_POLL_IN, &runningHandles_);
	check_multi_info();
}

void CurlManager::onWriteEventCallBack(int fd)
{
	LOG_DEBUG << "onWriteEventCallBack";
	curl_multi_socket_action(curlm_, fd, CURL_POLL_OUT, &runningHandles_);
	check_multi_info();
}

void CurlManager::check_multi_info()
{
	LOG_DEBUG << "check_multi_info::prevRunningHandles_ = " << prevRunningHandles_ << ", runningHandles_ = " << runningHandles_;
	if (prevRunningHandles_ > runningHandles_ || runningHandles_ == 0)
	{
		CURLMsg* msg = NULL;
		int left = 0;
		while ((msg = curl_multi_info_read(curlm_, &left)) != NULL)
		{
			if (msg->msg == CURLMSG_DONE)
			{
				CURL* c = msg->easy_handle;
				CURLcode retCode = msg->data.result;
				
				CurlRequestPtr request = HttpRequesting::GetInstance().find(c);
				if (request && request->getCurl() == c)
				{
					LOG_DEBUG <<"check_multi_info::" << request.get() << " ========>>>>done:" << curl_easy_strerror(retCode);
					request->done(retCode, curl_easy_strerror(retCode));

					afterRequestDone(request);
				}
			}
		}
		
		// ����Ƿ�����Ҫ���͵�����
		checkNeedSendRequest();
	}
	prevRunningHandles_ = runningHandles_;
}


void CurlManager::afterRequestDone(const CurlRequestPtr& request)
{
	// �������Ƴ�������
	removeMultiHandle(request);
	// ��������,����������ǻ������û���ֱ���ͷ�
	recycleRequest(request);
}

// �������Ƴ�������
void CurlManager::removeMultiHandle(const CurlRequestPtr& request)
{
	request->removeMultiHandle(curlm_);
}

// ��������,����������ǻ������û���ֱ���ͷ�
void CurlManager::recycleRequest(const CurlRequestPtr& request)
{
	// ������ӷ����ж����Ƴ�
	HttpRequesting::GetInstance().remove(request);

	// ��������
	HttpRecycleRequest::GetInstance().recycle(request);
	// �����ӣ��������գ������ã���
	//.............
}

// ����Ƿ�����Ҫ���͵�����
void CurlManager::checkNeedSendRequest()
{
	CurlRequestPtr request = nullptr;
	// ����Ƿ�����Ҫ���͵�����....
	while (!HttpRequesting::GetInstance().isFull() 
		&& (request=HttpWaitRequest::GetInstance().get()) != nullptr)
	{
		sendRequestInLoop(request);
	}
}