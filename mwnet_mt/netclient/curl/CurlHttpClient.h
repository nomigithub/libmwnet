#ifndef MWNET_MT_CURL_HTTPCLIENT_H
#define MWNET_MT_CURL_HTTPCLIENT_H

#include <stdint.h>
#include <stdio.h>
#include <boost/any.hpp>
#include <string>
#include <memory>
#include <atomic>

namespace MWNET_MT
{
namespace CURL_HTTPCLIENT
{
//content-type
enum HTTP_CONTENT_TYPE { CONTENT_TYPE_XML, CONTENT_TYPE_JSON, CONTENT_TYPE_URLENCODE, CONTENT_TYPE_UNKNOWN };
//POST/GET ...
enum HTTP_REQUEST_TYPE { HTTP_REQUEST_POST, HTTP_REQUEST_GET, HTTP_REQUEST_UNKNOWN };
//HTTP VERSION
enum HTTP_VERSION { HTTP_1_0, HTTP_1_1, HTTP_2_0 };

class HttpRequest
{
public:
	HttpRequest(const std::string& strUrl,						/*
																����url,�����get����,���ǰ����������ڵ�����������;
																�����POST����,��������url,��:http://ip/domain:port/v1/func
																*/
			    HTTP_VERSION http_ver=HTTP_1_1,	 				/*http����汾,1.0,1.1,2.0 etc.*/
				HTTP_REQUEST_TYPE req_type=HTTP_REQUEST_POST,	/*����ʽ:post/get*/
				bool bKeepAlive=false							/*������/������*/
				);
	~HttpRequest();
public:

	//����HTTP����body����
	//�ڲ��õ���swap���������Բ���û�м�const
	//����ֵ:void
	void	SetBody(std::string& strBody);

	//��������ͷ 
	//���ж��ͷ����Ҫ���ã����ö�μ���
	//����ֵ:void
	void	SetHeader(const std::string& strField, const std::string& strValue);
	
	//����User-Agent  
	//�粻���ã��Զ�ȡĬ��ֵ��Ĭ��ֵΪ:curl�汾��(curl_version()�����ķ���ֵ)
	//Ҳ����ʹ��SetHeader����
	//����ֵ:void
	void	SetUserAgent(const std::string& strUserAgent);

	//����content-type
	//Ҳ����ʹ��SetHeader����
	void	SetContentType(const std::string& strContentType);

	//���ó�ʱʱ��
	//conn_timeout:���ӳ�ʱʱ��
	//entire_timeout:��������ĳ�ʱ��
	void	SetTimeOut(int conn_timeout=2, int entire_timeout=5);

	//����keepalive����ά��ʱ��ͱ���̽����ʱ��(��λ��)
	void 	SetKeepAliveTime(int keep_idle=30, int keep_intvl=10);

	//����dns�����������泬ʱʱ��,����ָ�������ʱ��,Ĭ��ͬһ����60s��ֻ����һ��
	void 	SetDnsCacheTimeOut(int cache_timeout=60);
	
	//��ȡ���������Ωһreq_uuid
	uint64_t GetReqUUID() const;
	
///////////////////////////////////////////////////////////////////////////////////////
	//�����ڲ�����,���ã�
	///////////////////////////////////////////////////
	void	SetContext(const boost::any& context);
	const boost::any& GetContext() const;
///////////////////////////////////////////////////
private:
	boost::any any_;
	bool keep_alive_;
	HTTP_REQUEST_TYPE req_type_;
	HTTP_VERSION http_ver_;
};

class HttpResponse
{
public:
	HttpResponse(int nRspCode,int nErrCode,
					const std::string& strErrDesc, 
					const std::string& strContentType, 
					std::string& strHeader, 
					std::string& strBody, 
					const std::string& strEffectiveUrl,
					const std::string& strRedirectUrl,
					uint32_t total,uint32_t connect,uint32_t namelookup_time,
					uint64_t rsp_time,uint64_t req_time,uint64_t req_inque_time,uint64_t req_uuid);
	~HttpResponse();
public:
	//��ȡ״̬�룬200��400��404 etc.
	int    GetRspCode() const;

	//��ȡ�������
	int	   GetErrCode() const;

	//��ȡ��������
	const std::string& GetErrDesc() const;
	
	//��ȡconetnettype���磺text/json��application/x-www-form-urlencoded��text/xml
	const std::string& GetContentType() const;

	//��ȡ��Ӧ���İ�ͷ
	const std::string& GetHeader() const;

	//��ȡ��Ӧ���İ���
	const std::string& GetBody() const;

	//��ȡ��Ӧ��Ӧ���������ЧURL��ַ
	const std::string& GetEffectiveUrl() const;

	//��ȡ��Ӧ��Ӧ��������ض���URL��ַ
	const std::string& GetRedirectUrl() const;

	//��ȡ��ʱ(��λ:΢��)
	//total:�ܺ�ʱ,connect:���Ӻ�ʱ,nameloopup:����������ʱ
	void   GetTimeConsuming(uint32_t& total, uint32_t& connect, uint32_t& namelookup) const;

	//��ȡ��Ӧ��Ӧ�����������ʱ(��λ:΢��)
	uint32_t   GetReqTotalConsuming() const;

	//��ȡ��Ӧ��Ӧ���������Ӻ�ʱ(��λ:΢��)
	uint32_t   GetReqConnectConsuming() const;

	//��ȡ��Ӧ��Ӧ����������������ʱ(��λ:΢��)
	uint32_t   GetReqNameloopupConsuming() const;
	
	//��ȡ��Ӧ���ص�ʱ��(��ȷ��΢��)
	uint64_t   GetRspRecvTime() const;

	//��ȡ�û�Ӧ��Ӧ���������ʱ��(��ȷ��΢��)
	uint64_t   GetReqInQueTime() const;

	//��ȡ�û�Ӧ��Ӧ����������󷢳�ʱ��(��ȷ��΢��)
	uint64_t   GetReqSendTime() const;

	//��ȡ�û�Ӧ��Ӧ������UUID
	uint64_t   GetReqUUID() const;
private:
	int m_nRspCode;
	int m_nErrCode;
	std::string m_strErrDesc;
	std::string m_strContentType;
	std::string m_strHeader;
	std::string m_strBody;
	std::string m_strEffectiveUrl;
	std::string m_strRedirectUrl;
	uint32_t total_time_;
	uint32_t connect_time_;
	uint32_t namelookup_time_;
	uint64_t rsp_time_;
	uint64_t req_time_;
	uint64_t req_inque_time_;
	uint64_t req_uuid_;
};

typedef std::shared_ptr<HttpRequest> HttpRequestPtr;
typedef std::shared_ptr<HttpResponse> HttpResponsePtr;

/**************************************************************************************
* Function      : pfunc_onmsg_cb
* Description   : �ص����������Ӧ��ص��¼��ص�(��Ϣ��Ӧ����ʱ)
* Input         : [IN] void* pInvoker						�ϲ���õ���ָ��
* Input         : [IN] uint64_t req_uuid					SendHttpRequest�����Ĳ���HttpRequestPtr�ĳ�Ա����GetReqUUID��ֵ
* Input         : [IN] const boost::any& params				SendHttpRequest�����Ĳ���params
* Input         : [IN] const HttpResponsePtr& response		HttpResponse��Ӧ��
**************************************************************************************/
typedef int(*pfunc_onmsg_cb)(void* pInvoker, uint64_t req_uuid, const boost::any& params, const HttpResponsePtr& response);

class CurlHttpClient
{
public:
	CurlHttpClient();
	~CurlHttpClient();

public:
	//��ʼ����������curlhttpclient���������ڣ�ֻ�ɵ���һ��
	bool InitHttpClient(void* pInvoker,				/*�ϲ���õ���ָ��*/
						pfunc_onmsg_cb pFnOnMsg, 	/*���Ӧ��ص��¼��ص�(recv)*/
						bool enable_ssl=false,		/*�Ƿ����ssl.�粻��Ҫhttps,���Բ�����*/
						int nIoThrNum=4,			/*IO�����߳���*/
						int nMaxReqQueSize=20000,   /*�����������������Ĵ���������,������ֵ�᷵��ʧ��*/
						int nMaxTotalConns=10000,	/*�ܵ���������Ĳ���������������������������᷵��ʧ��*/	
						int nMaxHostConns=20		/*����host��������Ĳ���������������������������᷵��ʧ��*/
						);

	//��ȡHttpRequest
	HttpRequestPtr GetHttpRequest(const std::string& strUrl,     					/*
																					����url,�����get����,���ǰ����������ڵ�����������;
															    					�����POST����,��������url,��:http://ip/domain:port/v1/func
															    					*/
									HTTP_VERSION http_ver=HTTP_1_1,	 				/*http����汾,1.0,1.1,2.0 etc.*/
									HTTP_REQUEST_TYPE req_type=HTTP_REQUEST_POST,	/*����ʽ:post/get*/
									bool bKeepAlive=false							/*������/������*/
									);

	//����http����
	int  SendHttpRequest(const boost::any& params, 			/*ÿ������Я������������,�ص�����ʱ�᷵��*/
							const HttpRequestPtr& request);	/*������http��������,ÿ������ǰ����GetHttpRequest,�����*/
	
	// ��ȡ����Ӧ����
	int  GetWaitRspCnt();

	void ExitHttpClient();
private:
	void Start();
	void DoneCallBack(const boost::any& _any, int errCode, const char* errDesc);	
private:
	void* m_pInvoker;
	// �ص�����
	pfunc_onmsg_cb m_pfunc_onmsg_cb;
	// mainloop
	boost::any main_loop_;
	// ioloop
	boost::any io_loop_;
	// latch
	boost::any latch_;
	// httpclis
	std::vector<boost::any> httpclis_;
	// ��������
	std::atomic<uint64_t> total_req_;
	// ����Ӧ������
	std::atomic<int> wait_rsp_;
	int MaxTotalConns_;
	int MaxHostConns_;
	int IoThrNum_;
	int MaxReqQueSize_;
};

}
}

#endif