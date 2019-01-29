#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <tuple>
#include <functional>

#include "concurrentqueue.h"

#include <hiredis/hiredis.h>
#include <hiredis/async.h>
extern "C" {
#include "adapters/ae.h"
}

class ASyncRedis
{
public:
    using CommandCallback = std::function<void(const ASyncRedis *c, void *r, void *privdata)>;

    ASyncRedis(int intval = 1000, int cache_size = 32);
    ~ASyncRedis();

    bool Connect(const std::string &ip, int port);
    bool ConnectBind(const std::string &ip, int port, const std::string &source_addr);

    bool WaitForConnected();
    bool WaitForDisconnected();

    bool Command(const std::string &cmd);
    bool Command(const std::string &cmd, CommandCallback cb, void *data);

    virtual void ConnectCallback(int status);
    virtual void DisconnectCallback(int status);

    int EventMain();

    const redisAsyncContext* GetContext() const
    {
        return ac;
    }

    int GetError() const
    {
        return ac->err;
    }

    const char *GetErrorStr() const
    {
        return ac->errstr;
    }

private:
    using QueryOp = std::tuple<std::string, void *>;
    using QueryOps = moodycamel::ConcurrentQueue<QueryOp>;

    bool Attach();

    int interval;
    int cache_size;

    aeEventLoop *loop;
    redisAsyncContext *ac;
    std::thread evMain;

    std::mutex lock;
    std::condition_variable cv_connect;
    std::condition_variable cv_disconnect;

    bool connected;
    bool disconnected;

    QueryOps cmdlist;
};
