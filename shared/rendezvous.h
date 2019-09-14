#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <memory>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <assert.h>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>

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

void ParseHostPortPrefixWorldSizeRank(std::string combo, std::string &host, uint16_t &port, std::string &prefix, int &ws, int &rank)
{
	//host,port,prefix,worldsize,rank
	auto redisVec = CxxxxStringSplit(combo, ':');
	assert(redisVec.size() == 5);
	host = redisVec[0];
	port = atoi(redisVec[1].c_str());
	prefix = redisVec[2];
	ws = atoi(redisVec[3].c_str());
	rank = atoi(redisVec[4].c_str());
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

class NonblockingSingleBarrier
{
	string IP;
	uint Port;
	redisContext *pContext;
	string prefix;
	std::recursive_mutex mutex;
	std::string name;
	int worldSize;
	std::shared_ptr<std::thread> worker;

	void RoutineLoop()
	{
		while (true)
		{
			mutex.lock();
			auto workName = name;
			mutex.unlock();
			while (workName != "")
			{
				auto str = CxxxxStringFormat("[%s][Barrier]%s", prefix.c_str(), workName.c_str());
				//auto replyInc = redisCommand(pContext, "INCR %s", str.c_str());
				//assert(replyInc); // << pContext->errstr;
				//CHECK(reply) << pContext->errstr;
				bool refresh = false;
				while (true)
				{
					//100ms.
					//64 -> 1.6ms/req
					usleep(100000);
					//try to see how many we have now.
					auto reply = redisCommand(pContext, "GET %s", str.c_str());
					assert(reply); // << pContext->errstr;
					auto pReply = (redisReply *)reply;
					assert(pReply->type == REDIS_REPLY_STRING);
					if (atoi(pReply->str) == worldSize)
					{
						mutex.lock();
						if (workName == name)
						{
							name = "";
							mutex.unlock();
						}
						else
						{
						        refresh = true;
							mutex.unlock();
						        break;
						}
					}
				}
				if(refresh)
				  {
				    break;
				  }
			}
			usleep(100000);
		}
	}

public:
	void GetIpPort(std::string *str, uint *port)
	{
		*str = IP;
		*port = Port;
	}

	NonblockingSingleBarrier(string ip, uint port, string pref = "PLINK") : IP(ip), Port(port), prefix(pref)
	{
		worker = std::make_shared<std::thread>(&NonblockingSingleBarrier::RoutineLoop, this);
	}

	~NonblockingSingleBarrier()
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

	bool SubmitBarrier(std::string workName, int ws)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		assert(name == "");
		worldSize = ws;
		name = workName;
		auto str = CxxxxStringFormat("[%s][Barrier]%s", prefix.c_str(), name.c_str());
		auto replyInc = redisCommand(pContext, "INCR %s", str.c_str());
		assert(replyInc); // << pContext->errstr;
	}

	bool NonBlockingQueryBarrier()
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		//done if name is clear.
		return name == "";
	}

	void SynchronousBarrier(std::string _name, int participants)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		auto str = CxxxxStringFormat("[%s][Barrier]%s", prefix.c_str(), _name.c_str());
		auto replyInc = redisCommand(pContext, "INCR %s", str.c_str());
		assert(replyInc); // << pContext->errstr;
		//CHECK(reply) << pContext->errstr;
		while (true)
		{
			usleep(50000);
			//try to see how many we have now.
			auto reply = redisCommand(pContext, "GET %s", str.c_str());
			assert(reply); // << pContext->errstr;
			auto pReply = (redisReply *)reply;
			assert(pReply->type == REDIS_REPLY_STRING);
			if (atoi(pReply->str) == participants)
			{
				break;
			}
		}
	}

	void PushKey(std::string keyName, std::string value)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		auto name = CxxxxStringFormat("[%s]%s", prefix.c_str(), keyName.c_str());
		auto reply = redisCommand(pContext, "SET %s %s", name.c_str(), value.c_str());
		assert(reply); // << pContext->errstr;
	}

	std::string waitForKey(std::string keyName)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		auto name = CxxxxStringFormat("[%s]%s", prefix.c_str(), keyName.c_str());
		std::string result;
		while (true)
		{
			auto reply = redisCommand(pContext, "GET %s", name.c_str());
			assert(reply); //<< pContext->errstr;
			auto pReply = (redisReply *)reply;
			if (pReply->len != 0)
			{
				result = std::string(pReply->str);
				break;
			}
			usleep(50000);
		}
		assert(result.size());
		return result;
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
