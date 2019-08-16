#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <memory>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <assert.h>
#include <sstream>
using namespace std;


std::vector<std::string> CxxxxStringSplit(const std::string &s, char delimiter)
{
	std::vector<std::string> tokens;
	std::string token;
	std::istringstream tokenStream(s);
	while (std::getline(tokenStream, token, delimiter))
	{
		tokens.push_back(token);
	}
	return tokens;
}

void ParseHostPortPrefixWorldSize(std::string combo, std::string &host, uint &port, std::string &prefix, int ws)
{
	auto redisVec = CxxxxStringSplit(combo, ':');
	assert(redisVec.size() == 3);
	host = redisVec[0];
	port = atoi(redisVec[1].c_str());
	prefix = redisVec[2];
	ws = atoi(redisVec[3].c_str());
	assert(port != (uint)(-1));
}


template <typename... Args>
static std::string CxxxxStringFormat(const char *format, Args... args)
{
	int length = std::snprintf(nullptr, 0, format, args...);
	assert(length >= 0);

	char *buf = new char[length + 1];
	std::snprintf(buf, length + 1, format, args...);

	std::string str(buf);
	delete[] buf;
	return std::move(str);
}


class PHubRendezvous
{
	string IP;
	uint Port;
	redisContext *pContext;
	string prefix;
	std::recursive_mutex mutex;

  public:
	void GetIpPort(std::string* str, uint* port)
	{
		*str = IP;
		*port = Port;
	}

	PHubRendezvous(string ip, uint port, string pref = "PLINK") : IP(ip), Port(port), prefix(pref)
	{
	}

	~PHubRendezvous()
	{
		redisFree(pContext);
		pContext = NULL;
	}

	void Connect()
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		pContext = redisConnect(IP.c_str(), (int)Port);
		assert(pContext != NULL);
		assert(pContext->err == 0); // << pContext->errstr;
		//clean up old dbs.
		//CHECK(redisCommand(pContext, "FLUSHALL"));
	}

	void SynchronousBarrier(std::string name, int participants)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		auto str = CxxxxStringFormat("[%s][Barrier]%s", prefix.c_str(), name.c_str());
		auto replyInc = redisCommand(pContext, "INCR %s", str.c_str());
		assert(replyInc); // << pContext->errstr;
		//CHECK(reply) << pContext->errstr;
		while (true)
		{
			usleep(50000);
			//try to see how many we have now.
			auto reply = redisCommand(pContext, "GET %s", str.c_str());
			assert(reply);// << pContext->errstr;
			auto pReply = (redisReply *)reply;
			assert(pReply->type == REDIS_REPLY_STRING);
			if (atoi(pReply->str) == participants)
			{
				break;
			}
		}
	}

	void Shutdown()
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		if (pContext != NULL)
		{
			redisFree(pContext);
			pContext = NULL;
		}
	}
};
