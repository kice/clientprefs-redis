#pragma once

#include "concurrentqueue.h"
#include "blockingconcurrentqueue.h"

#include <thread>
#include <vector>
#include <functional>
#include <future>
#include <atomic>

#include "reply.h"

struct redisContext;

namespace async_redis
{
class client
{
public:
    /**
     * Create a new redis client
     *
     * @param piped_cache   The number of command put to pipeline before commit
     *                      0: manully commit, 1: disable pipeline (commit when available)
     *
     * @param pipeline_timeout  The maximum time to spend waiting for pipeline to be filled in ms
     *                          0: wait for piped_cache number of command to be filled.
     *                          DONT recommend set time to 0, since the worker will wait forever
     *                          until receive enough command (if you use future you will get a deadlock)
     *
     */
    client(size_t piped_cache = 0, uint32_t pipeline_timeout = 0);
    ~client();

    bool Connect(const std::string &host, int port, uint32_t timeout_ms);

    bool IsConnected() const;

    void Disconnect();

    // bool: if successfully sent and receive, reply: actual reply content
    typedef std::function<void(reply *)> reply_callback;

    client &Append(const std::vector<std::string> &redis_cmd, const reply_callback &callback = nullptr);

    std::future<std::unique_ptr<reply>> Command(const std::vector<std::string> &redis_cmd, bool commit = true)
    {
        auto prms = std::make_shared<std::promise<std::unique_ptr<reply>>>();
        Append(redis_cmd, [prms](auto r) { prms->set_value(std::unique_ptr<reply>(r)); });
        if (commit) {
            Commit();
        }
        return prms->get_future();
    }

    // This function will do nothing when auto pipeline enabled
    // If pipeline was enable, it will force worker to commit existing command
    client &Commit();

    int GetError() const;
    const char *GetErrorString() const;

private:
    struct command_request
    {
        std::string command;
        reply_callback callback;
    };

    static std::string formatCommand(const std::vector<std::string> &redis_cmd);

    moodycamel::BlockingConcurrentQueue<command_request> m_commands;

    moodycamel::details::mpmc_sema::LightweightSemaphore pipeline_sem;

    size_t cache_size;
    uint32_t pipe_timeout;

    std::atomic<bool> flush_pipeline;
    std::atomic<bool> stopped;
    std::thread worker;

    redisContext *ctx;
};
}
