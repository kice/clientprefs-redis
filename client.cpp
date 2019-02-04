extern "C" {
#ifdef _WIN32
#define WIN32_INTEROP_APIS_H
#define NO_QFORKIMPL
#include <Win32_Interop/win32fixes.h>

#pragma comment(lib, "hiredis.lib")
#pragma comment(lib, "Win32_Interop.lib")
#endif
}

#include <hiredis/hiredis.h>

#include <sstream>
#include <chrono>
using namespace std::chrono_literals;

#include "client.h"

namespace async_redis
{
client::client(size_t _piped_cache, uint32_t pipeline_timeout) :
    cache_size(_piped_cache), pipe_timeout(pipeline_timeout), ctx(nullptr)
{
    stopped = true;
}

client::~client()
{
    Disconnect();
}

bool client::Connect(const std::string & host, int port, uint32_t timeout_ms)
{
    if (IsConnected()) {
        Disconnect();
    }

    if (timeout_ms > 0) {
        struct timeval timeout;
        timeout.tv_sec = (timeout_ms / 1000);
        timeout.tv_usec = ((timeout_ms - (timeout.tv_sec * 1000)) * 1000);
        ctx = redisConnectWithTimeout(host.c_str(), port, timeout);
    } else {
        ctx = redisConnect(host.c_str(), port);
    }

    if (ctx == nullptr || ctx->err) {
        if (ctx) {
            redisFree(ctx);
            ctx = nullptr;
        }
        return false;
    }

    stopped = false;
    worker = std::thread([this] {
        std::vector<command_request> reqs;
        while (!stopped) {
            if (!ctx || ctx->err) {
                stopped = true;
                return;
            }

            auto send = [&] {
                size_t queue_size = m_commands.size_approx();
                if (!queue_size) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    return;
                }
                queue_size = cache_size < queue_size ? queue_size : cache_size;

                reqs.clear();
                reqs.resize(queue_size);

                size_t dequeued = 0;
                if (pipe_timeout) {
                    dequeued = m_commands.wait_dequeue_bulk_timed(reqs.begin(), reqs.size(), std::chrono::milliseconds(pipe_timeout));
                } else {
                    do {
                        dequeued += m_commands.wait_dequeue_bulk_timed(reqs.begin(), reqs.size() - dequeued, std::chrono::milliseconds(100));
                    } while (!stopped && dequeued < queue_size && !flush_pipeline);
                }

                if (!dequeued) {
                    return;
                }

                flush_pipeline = false;

                std::vector<bool> appended;
                for (size_t i = 0; i < dequeued; ++i) {
                    auto &req = reqs[i];
                    appended.emplace_back(
                        redisAppendFormattedCommand(ctx, req.command.c_str(), req.command.size()) == REDIS_OK
                    );
                }

                for (size_t i = 0; i < dequeued; ++i) {
                    auto &req = reqs[i];
                    redisReply *rawReply = nullptr;

                    bool success = appended[i];
                    if (success) {
                        success &= redisGetReply(ctx, (void **)&rawReply) == REDIS_OK;
                    }

                    if (req.callback) {
                        if (success) {
                            req.callback(new reply(rawReply));
                        } else {
                            req.callback(nullptr);
                        }
                    }

                    if (rawReply) {
                        freeReplyObject(rawReply);
                    }
                }
            };

            if (cache_size == 0) {
                if (!pipeline_sem.wait(100 * 1000)) {
                    continue;
                }
            }

            {
                send();
            }
        }
    });

    return true;
}

bool client::IsConnected() const
{
    return ctx && ctx->err == 0 && !stopped;
}

void client::Disconnect()
{
    stopped = true;

    if (worker.joinable()) {
        worker.join();
    }

    if (ctx) {
        redisFree(ctx);
        ctx = nullptr;
    }
}

client & client::Append(const std::vector<std::string> &redis_cmd, const reply_callback & callback)
{
    m_commands.enqueue({ formatCommand(redis_cmd), callback });
    return *this;
}

client & client::Commit()
{
    flush_pipeline = true;
    if (IsConnected() && cache_size == 0) {
        pipeline_sem.signal();
    }
    return *this;
}

int client::GetError() const
{
    if (ctx) {
        return ctx->err;
    }

    return -1;
}

const char *client::GetErrorString() const
{
    if (ctx) {
        return ctx->errstr;
    }
    return nullptr;
}

inline std::string client::formatCommand(const std::vector<std::string>& redis_cmd)
{
    std::stringstream ss;
    ss << "*" << redis_cmd.size() << "\r\n";
    for (const auto &cmd_part : redis_cmd) {
        ss << "$" << cmd_part.length() << "\r\n" << cmd_part << "\r\n";
    }
    return ss.str();
}
}
