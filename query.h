/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Client Preferences Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#ifndef _INCLUDE_SOURCEMOD_CLIENTPREFS_QUERY_H_
#define _INCLUDE_SOURCEMOD_CLIENTPREFS_QUERY_H_

#include "extension.h"
#include "cookie.h"
#include <sh_string.h>

#include <map>
#include <vector>
#include <string>

#define GET_CLIENT_COOKIES R"(local a={}for b,c in ipairs(redis.call('SMEMBERS','cookies.list'))do local d=redis.call('GET','cookies.id.'..c)if d==false then redis.call('SREM','cookies.list',c)else table.insert(a,{c,redis.call('GET',string.format('cookies.desc.%s',c)),redis.call('GET',string.format('cookies.access.%s',c)),redis.call('GET',string.format('%s.%s',KEYS[1],d))})end end;return a)"
#define GET_CLIENT_COOKIES_SHA "2e80a3ad95e151c8466c622a382586fab9d9f9b1"

 /*
-- local cookies = {}

-- for idx, name in ipairs(redis.call('SMEMBERS', 'cookies.list')) do
--     local id = redis.call('GET', 'cookies.id.' .. name)
--     if id == false then
--         redis.call('SREM', 'cookies.list', name)
--     else
--         table.insert(cookies, {
--             name,
--             redis.call('GET', string.format('cookies.desc.%s', name)),
--             redis.call('GET', string.format('cookies.access.%s', name)),
--             redis.call('GET', string.format('%s.%s', KEYS[1], id))
--         })
--     end
-- end

-- return cookies
 */

enum querytype
{
    Query_InsertCookie = 0,
    Query_SelectData,
    Query_InsertData,
    Query_SelectId,
    Query_Connect,
};

struct Cookie;
struct CookieData;
#define MAX_NAME_LENGTH 30

/* This stores all the info required for our param binding until the thread is executed */
struct ParamData
{
    ParamData();

    ~ParamData();

    /* Contains a name, description and access for InsertCookie queries */
    Cookie *cookie;
    /* A clients steamid - Used for most queries - Doubles as storage for the cookie name*/
    char steamId[MAX_NAME_LENGTH];

    int cookieId;
    CookieData *data;
};

class TQueryOp : public IThreadQuery
{
public:
    TQueryOp(enum querytype type, int serial);
    TQueryOp(enum querytype type, Cookie *cookie);
    ~TQueryOp() {}

    // IDBDriver *GetDriver();
    IdentityToken_t *GetOwner();

    void SetDatabase(void *db);

    void Destroy();

    void RunThreadPart();
    /* Thread has been cancelled due to driver unloading. Nothing else to do? */
    void CancelThinkPart() {}
    void RunThinkPart();

    bool BindParamsAndRun();

    /* Params to be bound */
    ParamData m_params;

    inline RedisDB *GetDB()
    {
        return m_database;
    }

public:
    querytype PullQueryType();
    int PullQuerySerial();

private:
    RedisDB *m_database;
    // IDBDriver *m_driver;
    // IQuery *m_pResult;
    std::vector<std::pair<Cookie, std::string>> m_results;

    /* Query type */
    enum querytype m_type;

    /* Data to be passed to the callback */
    int m_serial;
    int m_insertId;
    Cookie *m_pCookie;
};

#endif // _INCLUDE_SOURCEMOD_CLIENTPREFS_QUERY_H_
