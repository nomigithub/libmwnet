#ifndef __CURL_CURLMANAGER_H__
#define __CURL_CURLMANAGER_H__

#include "CurlRequest.h"
#include <mwnet_mt/base/CountDownLatch.h>
#include <mutex>
#include <map>
#include <list>
#include <event2/event.h>
#include <event2/event_struct.h>

namespace curl
{

using namespace mwnet_mt;

typedef struct _EvLoopInfo
{
	struct event_base* evbase;
	struct event wakeup_event;
	struct event timer_event;
} EvLoopInfo;

/* Information associated with a specific socket */
typedef struct _SockInfo
{
	int fd;
	CURL* easy;
	int action;
	long timeout;
	struct event ev;
	EvLoopInfo *evInfo;
} SockInfo;


/**
 * curlm����Ĺ����ߣ��ɹ�������򵥾��
 */
class CurlManager : noncopyable
{
public:
	enum Option
	{
		kCURLnossl = 0,
		kCURLssl   = 1,
	};

	/**
	 * [CurlManager ���캯��]
	 * @param  loop [epoll���¼�ѭ����]
	 */
	explicit CurlManager(long maxtotalconns=0, 
							long maxhostconns=0, 
							long maxconns=0);
	~CurlManager();

	/**
	 * [initialize ��ʼ������]
	 * @param opt [kCURLnossl����֧��HTTPS kCURLssl��֧��HTTPS]
	 */
	static void initialize(Option opt = kCURLnossl);
	static void uninitialize();
	
	// ��ȡһ���������
	static CurlRequestPtr getRequest(const std::string& url, bool bKeepAlive, int req_type);

	// ����curl�¼�ѭ���������¼�ѭ���ȣ��ú������������᷵�أ���Ҫ�����߳������У�
	int runCurlEvLoop(const std::shared_ptr<CountDownLatch>& latch);
	
	// ��������
	//0:�ɹ� 1:������
	int sendRequest(const CurlRequestPtr& request);

private:
	// ֪ͨ����
	void notifySendRequest();

	// wakeup
	static void onRecvWakeUpNotify(int fd, short evtp, void *arg);
	void handleWakeUpCb();

	// evtimer
	static void onRecvEvTimerNotify(int fd, short evtp, void *arg);
	void handleEvTimerCb();
	
	// ev�¼�����֪ͨ
	static void onRecvEvOptNotify(int fd, short evtp, void *arg);
	void handleEvOptCb(int fd, short evtp);

	// curlm socket�¼��ص�CURLMOPT_SOCKETFUNCTION CURL_POLL_IN/OUT/INOUT/REMOVE
	static int curlmSocketOptCb(CURL* c, int fd, int what, void* userp, void* socketp);
	void handleCurlmSocketOptCb(CURL* c, int fd, int what, void* socketp);

	// curlm��ʱ�ص�����CURLMOPT_TIMERFUNCTION
	static int curlmTimerCb(CURLM* curlm, long ms, void* userp);
	void handleCurlmTimerCb(long ms);

	void addsock(int fd, CURL* c, int action, EvLoopInfo* evInfo);

	/* Assign information to a SockInfo structure */
	void setsock(SockInfo* sinfo, int fd, CURL* c, int action, EvLoopInfo* evInfo);

	/* Clean up the SockInfo structure */
	void remsock(SockInfo* sinfo);
	
	/**
	 * [check_multi_info ��ʵ�����Ƿ��Ѿ����]
	 */
	void check_multi_info();

	// ����Ƿ�����Ҫ���͵�����
	void checkNeedSendRequest();

	// ��������,����������ǻ������û���ֱ���ͷ�
	void recycleRequest(const CurlRequestPtr& request);

	// ��������	
	void sendRequestInLoop(const CurlRequestPtr& request);

	// �������Ƴ�������
	void removeMultiHandle(const CurlRequestPtr& request);

	void afterRequestDone(const CurlRequestPtr& request);
	
private:
	// ����FD
	int wakeupFd_;
	// curl������	
	CURLM* curlm_;
	// ��¼���ڱ���صļ򵥾�����ܸ���
	int runningHandles_;
	// ��¼��һ�μ�صļ򵥾�����ܸ���
	int prevRunningHandles_;
	// ֹͣ���
	bool stopped_;
	// �¼�ѭ����
	EvLoopInfo evInfo_;
	// ��������/����������/��������������
	long MaxTotalConns_;
	long MaxHostConns_;	
	long MaxConns_;
};

} // end namespace curl


#endif // __CURL_CURLMANAGER_H__