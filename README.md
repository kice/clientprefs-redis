# clientprefs-redis

A clientprefs extension for sourcemod using Redis as backend.

This plugin is truly mutilthreaded. 

Unlike the offical clientprefs only use signle thread for query and getting stuck if there are too many cookies (or other SQL queries), this implementation use a lock-free queue ([Thanks to Cameron](https://github.com/cameron314/concurrentqueue)) to minimize delay and a thread pool to minimize delay and maximize QPS.

Most clients are able to get their cookies before they finish loading the maps, even with 64 player and thousands of cookies, which the orginial implementation can take more than 10 minutes to load.

If the server take super long time to load cookies or threaded SQL queries can take days to return, this plugin might be a nice choice.

Thanks to [XNet](https://www.93x.net/) for long term testing, which they have substantial cookies for their plugins. Before using this plugin, it will take up to 10 minutes to load all cookies; now, it just cost less than a minute to finish loading.

I only test this plugin in Windows, but it should work under Linux.

# Installaion

Download the file from [here](https://github.com/kice/clientprefs-redis/releases). Just copy the .dll file to `sourcemod/extensions`, remember to backup the orginial `clientprefs.ext.dll` file. 

**No other change was needed. Existing code can still work, but you might loss all your existing cookies data.** 

# Config Redis connection infomation

Add a new config in `sourcemod/configs/databases.cfg`

```
"clientprefs_redis"
{
    "driver"  "redis"
    "host"    "127.0.0.1"
    "pass"    "foobared233"
}
```

# Want to save existing data?

You can port existing data to the target redis database, but you have to follow the new data format. See the [code](https://github.com/kice/clientprefs-redis/blob/master/query.cpp) for more infomation.

# How to complie

## Windows

### Prerequisites

First complie [Microsoft's native port of redis](https://github.com/MicrosoftArchive/redis). 
Copy the `hiredis.lib` and `Win32_Interop.lib` to `clientprefs-redis/lib/Win32/Release` or `clientprefs-redis/lib/Win32/Debug` folder. 

Or You can use my prebuild library.

### Build Configurations

You need to enable C++17 feature to complie the code.

## Linux

// TODO

# Benchmark

See the code [here](https://github.com/kice/clientprefs-redis/blob/master/addons/sourcemod/scripting/cookiesSpeedTest.sp)

# Also see

[A Hiredis warpper for Sourcemod](https://github.com/kice/sm_hiredis/tree/master)
