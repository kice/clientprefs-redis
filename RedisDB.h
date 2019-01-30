#pragma once

#include "TQueue.h"
#include "ASyncRedis.h"

#include <vector>

#include <IDBDriver.h>

class RedisReply
{
public:
    RedisReply(RedisReply && t)
    {
        this->type = t.type;
        this->integer = t.integer;
        this->elements = t.elements;

        if (!t.str.empty()) {
            this->str = std::move(t.str);
        }

        if (!t.reply_array.empty()) {
            this->reply_array = std::move(t.reply_array);
        }
    }

    RedisReply(redisReply *reply = nullptr)
    {
        if (reply == nullptr) {
            return;
        }

        type = reply->type;
        integer = reply->integer;

        if (reply->len > 0 && reply->str != nullptr) {
            str = reply->str;
        }

        if (reply->type == REDIS_REPLY_ARRAY && reply->element != nullptr) {
            for (size_t i = 0; i < reply->elements; ++i) {
                reply_array.emplace_back(reply->element[i]);
            }
        }
    }

    int type = 0;
    long long integer = 0;
    size_t elements = 0;

    std::string str;
    std::vector<RedisReply> reply_array;
};

class RedisDB
{
public:
    static RedisDB *GetRedisDB(std::string host, int port, int maxTimeout, std::string pass)
    {
        if (host.empty()) {
            return nullptr;
        }

        auto db = new RedisDB();
        if (db->Connect(host, port, maxTimeout, pass) && db->Alive()) {
            return db;
        }

        delete db;
        return nullptr;
    }

    RedisDB()
    {
        this->ctx = nullptr;
    }

    RedisDB(std::string host, int port, int maxTimeout, std::string pass)
    {
        RedisDB();
        Connect(host, port, maxTimeout, pass);
    }

    ~RedisDB()
    {
        Close();
    }

    bool Connect(std::string host, int port, int maxTimeout, std::string pass)
    {
        struct timeval timeout = { maxTimeout, 0 };

        if (maxTimeout > 0)
            timeout.tv_sec = maxTimeout;
        else timeout.tv_sec = 30;

        auto ctx = redisConnectWithTimeout(host.c_str(), port, timeout);

        if (ctx == nullptr || ctx->err) {
            if (ctx) {
                printf("Connection error: %s\n", ctx->errstr);
                redisFree(ctx);
            } else {
                printf("Connection error: can't allocate redis context\n");
            }
            return false;
        }

        this->ctx = ctx;

        if (pass.empty()) {
            return true;
        }

        auto reply = this->Run("AUTH %s", pass.c_str());
        if (reply.type != REDIS_REPLY_STATUS || reply.str != "OK") {
            printf("Redis Password error: %s\n", reply.str.c_str());
            redisFree(ctx);

            this->ctx = nullptr;
            return false;
        }

        return true;
    }

    int ConnectAsync(std::string host, int port, std::string pass)
    {
        if (!async.Connect(host.c_str(), port)) {
            return async.GetError();
        }

        async.WaitForConnected();
        async.Command(("AUTH " + pass).c_str(), [this](const ASyncRedis *c, void *r, void *privdata) {
            redisReply *reply = (redisReply *)r;
            if (reply->type == REDIS_REPLY_STATUS && c->GetError() == REDIS_OK) {
                this->canAsync = true;
            } else {
                this->canAsync = false;
            }
        }, nullptr);

        return async.GetError();
    }

    bool SELECT_DBID(int dbid)
    {
        if (this->ctx == nullptr) {
            return false;
        }

        auto reply = this->Run("SELECT %d", dbid);
        if (reply.type != REDIS_REPLY_STATUS || reply.str != "OK") {
            return false;
        }

        if (canAsync) {
            char buf[128];
            sprintf(buf, "SELECT %d", dbid);
            async.Command(buf);
        }
        return true;
    }

    bool Alive()
    {
        if (this->ctx == nullptr) {
            return false;
        }

        auto reply = this->Run("PING");
        if (reply.type != REDIS_REPLY_STATUS || ctx->err != REDIS_OK) {
            return false;
        }

        if (canAsync) {
            return async.GetError() == REDIS_OK;
        }
        return true;
    }

    bool Command(const char *format, ...)
    {
        if (this->ctx == nullptr) {
            return false;
        }

        va_list ap;
        va_start(ap, format);
        auto res = redisvAppendCommand(ctx, format, ap);
        va_end(ap);
        return res == REDIS_OK;
    }

    bool CommandAsync(const char *format, ...)
    {
        if (!canAsync) {
            return false;
        }

        char buf[1024];
        va_list arglist;
        va_start(arglist, format);
        _vsnprintf(buf, 1024, format, arglist);
        va_end(arglist);

        return async.Command(buf);
    }

    bool Execute(const char *format, ...)
    {
        if (this->ctx == nullptr) {
            return false;
        }

        va_list ap;
        va_start(ap, format);
        redisReply *res = (redisReply *)redisvCommand(ctx, format, ap);
        va_end(ap);

        if (res == nullptr) {
            return false;
        }

        bool success = res->type != REDIS_REPLY_ERROR && ctx->err == REDIS_OK;
        freeReplyObject(res);

        return success;
    }

    RedisReply Run(const char *format, ...)
    {
        if (this->ctx == nullptr) {
            return RedisReply();
        }

        va_list ap;
        va_start(ap, format);
        auto reply = redisvCommand(ctx, format, ap);
        va_end(ap);

        auto res = RedisReply((redisReply *)reply);

        if (reply != nullptr) {
            freeReplyObject(reply);
        }

        return res;
    }

    RedisReply GetReply(bool block = false)
    {
        if (this->ctx == nullptr) {
            return RedisReply();
        }

        redisReply *reply;
        redisGetReply(ctx, (void **)&reply);

        auto res = RedisReply(reply);
        if (reply != nullptr) {
            freeReplyObject(reply);
        }

        return std::move(res);
    }

    std::vector<RedisReply> GetReplies(int count)
    {
        std::vector<RedisReply> res;
        if (this->ctx == nullptr) {
            return{};
        }

        while (res.size() != count) {
            auto reply = GetReply(false);
            if (reply.type == 0) {
                continue;
            }

            res.emplace_back(std::move(reply));
        }

        return res;
    }

    bool Close()
    {
        if (this->ctx != nullptr) {
            redisFree(this->ctx);
        }
        return true;
    }

    int GetErrorCode() const
    {
        return ctx->err;
    }

    const char *GetErrorStr() const
    {
        return ctx->errstr;
    }

    const char *GetASyncErrorStr() const
    {
        return async.GetErrorStr();
    }

    //void LockForFullAtomicOperation()
    //{
    //    lock.Lock();
    //}

    //void UnlockFromFullAtomicOperation()
    //{
    //    lock.Unlock();
    //}

    bool CanAsync() const
    {
        return canAsync;
    }

private:
    // std::mutex lock;
    redisContext *ctx;

    bool canAsync = false;
    ASyncRedis async;
};
