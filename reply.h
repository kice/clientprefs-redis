#pragma once

#include <string>
#include <vector>

namespace async_redis
{
class reply
{
public:
    enum type
    {
        invalid,
        string,
        arrays,
        integer,
        nil,
        status,
        error,
    };

    reply(void *raw_reply);

    //reply(reply &&other) noexcept;
    //reply &operator=(reply &&other) noexcept;

    operator bool() const;

    ~reply() = default;

    type Type() const;

    bool Ok() const;
    const char * Status() const;

    const std::vector<reply> &GetArray() const;
    int64_t GetInt() const;
    const std::string &GetString() const;

    bool IsVaild() const
    {
        return reply_type != type::invalid;
    }

    bool IsString() const
    {
        return reply_type == type::string;
    }

    bool IsArrays() const
    {
        return reply_type == type::arrays;
    }

    bool IsInt() const
    {
        return reply_type == type::integer;
    }

    bool IsNIL() const
    {
        return reply_type == type::nil;
    }

    bool IsStatus() const
    {
        return reply_type == type::status;
    }

    bool IsError() const
    {
        return reply_type == type::error;
    }

private:
    type reply_type;

    std::vector<reply> rows;
    std::string str_val;
    int64_t int_val;
};
}
