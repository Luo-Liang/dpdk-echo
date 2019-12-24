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
#include <unordered_set>
#include <unordered_map>
#include <deque>

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
	int worldSize;
	std::shared_ptr<std::thread> worker;

	std::unordered_set<std::string> done;
	std::unordered_set<std::string> dbgSubmissions;

	std::deque<std::string> workQueue;
	const int TIME_INTERVAL = 256000; //100ms
	//strictly only allows sequential polling.
	void RoutineLoop()
	{
		while (true)
		{
			std::string workName = "";
			mutex.lock();
			if (workQueue.size() != 0)
			{
				workName = workQueue.front();
				workQueue.pop_front();
			}
			mutex.unlock();
			if(workName != "")
			{
				auto str = CxxxxStringFormat("[%s][Barrier]%s", prefix.c_str(), workName.c_str());
				//auto replyInc = redisCommand(pContext, "INCR %s", str.c_str());
				//assert(replyInc); // << pContext->errstr;
				//CHECK(reply) << pContext->errstr;
				while (true)
				{
					//100ms.
					//64 -> 1.6ms/req
					//try to see how many we have now.
					auto reply = redisCommand(pContext, "GET %s", str.c_str());
					assert(reply); // << pContext->errstr;
					auto pReply = (redisReply *)reply;
					assert(pReply->type == REDIS_REPLY_STRING);
					if (atoi(pReply->str) == worldSize)
					{
						mutex.lock();
						assert(done.find(workName) == done.end());
						done.insert(workName);
						mutex.unlock();
						break;
					}
					usleep(TIME_INTERVAL);
				}
			}
			usleep(TIME_INTERVAL);
		}
	}

public:
	void GetIpPort(std::string *str, uint *port)
	{
		*str = IP;
		*port = Port;
	}

	NonblockingSingleBarrier(string ip, uint port, string pref, int ws) : IP(ip), Port(port), prefix(pref), worldSize(ws)
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

	void SubmitBarrier(std::string workName)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		assert(dbgSubmissions.find(workName) == dbgSubmissions.end());
		dbgSubmissions.insert(workName);
		workQueue.push_front(workName);
		auto str = CxxxxStringFormat("[%s][Barrier]%s", prefix.c_str(), workName.c_str());
		auto replyInc = redisCommand(pContext, "INCR %s", str.c_str());
		assert(replyInc); // << pContext->errstr;
	}

	bool NonBlockingQueryBarrier(std::string name)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		//done if name is clear.
		assert(dbgSubmissions.find(name) != dbgSubmissions.end());
		return done.find(name) != done.end();
	}

	void SynchronousBarrier(std::string _name)
	{
		SubmitBarrier(_name);
		while(NonBlockingQueryBarrier(_name) == false)
		{
			usleep(TIME_INTERVAL);
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
			usleep(TIME_INTERVAL);
		}
		assert(result.size());
		return result;
	}


  	void ____dbg_push_beacon____(string keyName, string beacon)
	{
		std::lock_guard<std::recursive_mutex> lock(mutex);
		auto name = CxxxxStringFormat("[%s]%s", prefix.c_str(), keyName.c_str());
		auto reply = redisCommand(pContext, "LPUSH %s \"%s\"", name.c_str(), beacon.c_str());
		assert(reply); // << pContext->errstr;
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
