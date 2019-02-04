#include "reply.h"

#include <hiredis/hiredis.h>

#include <windows.h>

namespace async_redis
{
reply::reply(void *raw_reply)
{
    reply_type = type::invalid;
    if (raw_reply == nullptr) {
        return;
    }

    redisReply *r = (redisReply *)raw_reply;
    reply_type = (type)r->type;

    int_val = r->integer;

    if (r->len && r->str) {
        str_val = std::string(r->str);
    }

    if (reply_type == type::arrays && r->elements && r->element) {
        for (size_t i = 0; i < r->elements; ++i) {
            rows.emplace_back(r->element[i]);
        }
    }
}

//reply::reply(reply &&other) noexcept
//{
//    reply_type = other.reply_type;
//    rows = std::move(other.rows);
//    str_val = std::move(other.str_val);
//    int_val = other.int_val;
//}
//
//reply &reply::operator=(reply &&other) noexcept
//{
//    if (this != &other) {
//        reply_type = other.reply_type;
//        rows = std::move(other.rows);
//        str_val = std::move(other.str_val);
//        int_val = other.int_val;
//    }
//    return *this;
//}

reply::operator bool() const
{
    return reply_type == type::invalid;
}

reply::type reply::Type() const
{
    return reply_type;
}

bool reply::Ok() const
{
    return reply_type != type::invalid && reply_type != type::error;
}

const char *reply::Status() const
{
    if (reply_type == type::invalid) {
        return "reply is invalid";
    }

    if (reply_type == type::status || reply_type == type::error) {
        return str_val.c_str();
    }

    return nullptr;
}

const std::vector<reply> &reply::GetArray() const
{
    if (reply_type != type::arrays) {
        throw std::invalid_argument("Redis reply type does not match");
    }
    return rows;
}

int64_t reply::GetInt() const
{
    if (reply_type != type::integer) {
        throw std::invalid_argument("Redis reply type does not match");
    }
    return int_val;
}

const std::string &reply::GetString() const
{
    if (reply_type != type::string) {
        throw std::invalid_argument("Redis reply type does not match");
    }
    return str_val;
}
}
