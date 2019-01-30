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

#include "query.h"

#define CookieName  0
#define CookieDesc  1
#define CookieAcce  2
#define CookieValue 3

#include <functional>
#include <string>

 // Only run on main thread
void TQueryOp::RunThinkPart()
{
    switch (m_type) {
    case Query_InsertCookie:
    {
        g_CookieManager.InsertCookieCallback(m_pCookie, m_insertId);
        break;
    }

    case Query_SelectData:
    {
        g_CookieManager.ClientConnectCallback(m_serial, m_results);
        break;
    }

    case Query_SelectId:
    {
        g_CookieManager.SelectIdCallback(m_pCookie, m_insertId);
        break;
    }

    case Query_Connect:
    {
        return;
    }

    default:
    {
        break;
    }
    }
}

void TQueryOp::RunThreadPart()
{
    if (m_type == Query_Connect) {
        g_ClientPrefs.DatabaseConnect();
        return;
    }

    // assert(m_database != NULL);
    /* I don't think this is needed anymore... keeping for now. */
    // m_database->LockForFullAtomicOperation();
    if (!BindParamsAndRun()) {
        g_pSM->LogError(myself,
            "Failed Redis Query, Error: \"%s\" (Query id %i - serial %i)",
            m_database->GetErrorStr(),
            m_type,
            m_serial);
    }
    // m_database->UnlockFromFullAtomicOperation();
}

//IDBDriver *TQueryOp::GetDriver()
//{
//	return m_driver;
//}

IdentityToken_t *TQueryOp::GetOwner()
{
    return myself->GetIdentity();
}

void TQueryOp::Destroy()
{
    delete this;
}

TQueryOp::TQueryOp(enum querytype type, int serial)
{
    m_type = type;
    m_serial = serial;
    m_database = NULL;
    // m_driver = NULL;
    m_insertId = -1;
    // m_pResult = NULL;
}

TQueryOp::TQueryOp(enum querytype type, Cookie *cookie)
{
    m_type = type;
    m_pCookie = cookie;
    m_database = NULL;
    // m_driver = NULL;
    m_insertId = -1;
    // m_pResult = NULL;
    m_serial = 0;
}

void TQueryOp::SetDatabase(void *db)
{
    m_database = (RedisDB *)db;
}

bool TQueryOp::BindParamsAndRun()
{
    switch (m_type) {
    case Query_InsertCookie:
    {
        std::string safe_name = m_params.cookie->name;
        std::string safe_desc = m_params.cookie->description;

        std::hash<std::string> h;
        int id = h(safe_name) & 0x7FFFFFFF;
        assert(id > 0);

        auto rep = m_database->Run("GET %s", "cookies.id." + safe_name); // check if we have that cookie
        if (rep.type == REDIS_REPLY_STRING) {
            m_database->Execute("SADD cookies.list %s", safe_name.c_str());
            m_insertId = std::stoi(rep.str);
            return true;
        }

        std::string key_name = "cookies.access." + safe_name;
        std::string key_desc = "cookies.desc." + safe_name;
        std::string key_id = "cookies.id." + safe_name;

        if (m_database->CanAsync()) {
            m_database->CommandAsync("SADD cookies.list %s", safe_name.c_str());
            m_database->CommandAsync("SET %s %d", key_name.c_str(), (int)m_params.cookie->access); // access
            m_database->CommandAsync("SET %s %s", key_desc.c_str(), safe_desc.c_str()); // description
            m_database->CommandAsync("SET %s %d", key_id.c_str(), id); // id
            m_insertId = id;
            return true;
        }

        if (!m_database->Execute("SADD cookies.list %s", safe_name.c_str())) {
            g_pSM->LogError(myself, "Cannot add \"%s\" to cookies.list", safe_name.c_str());
        }
        m_database->Command("SET %s %d", key_name.c_str(), (int)m_params.cookie->access); // access
        m_database->Command("SET %s %s", key_desc.c_str(), safe_desc.c_str()); // description
        m_database->Command("SET %s %d", key_id.c_str(), id); // id

        auto replies = m_database->GetReplies(3);
        if (replies.size() != 3) {
            g_pSM->LogError(myself, "Expect 3 replies but got %d", replies.size());
        }

        bool bsucc = true;
        for (const auto &reply : replies) {
            if (reply.type != REDIS_REPLY_STATUS) {
                g_pSM->LogError(myself, "Expect REDIS_REPLY_STATUS reply but got %d", reply.type);
            } else if (reply.str != "OK") {
                bsucc = false;
            }
        }

        m_insertId = id;
        return bsucc;
    }

    case Query_SelectData:
    {
        m_results.clear();
        std::string steamId = m_params.steamId;

        auto luaquery = [&] {
            // Try to use cached Lua query first
            auto cookies = m_database->Run("EVALSHA %s %d %s", GET_CLIENT_COOKIES_SHA, 1, steamId.c_str());
            if (cookies.type == REDIS_REPLY_ARRAY && cookies.reply_array.size() > 0) {
                for (const auto &cookieData : cookies.reply_array) {
                    if (cookieData.type != REDIS_REPLY_ARRAY || cookieData.reply_array.size() != 4) {
                        break;
                    }

                    const auto &cookie = cookieData.reply_array;

                    CookieAccess acce;
                    try {
                        acce = (CookieAccess)std::stoi(cookie[CookieAcce].str);
                    } catch (const std::exception&) {
                        acce = CookieAccess_Public;
                    }

                    m_results.push_back({
                        Cookie(
                            cookie[CookieName].str.data(),
                            cookie[CookieDesc].str.data(),
                            acce
                        ),
                        cookie[CookieValue].str.data()
                        });
                }
            }
        };

        if (m_results.size()) {
            return true;
        }

        // Use normal and slow method
        auto keys = m_database->Run("KEYS cookies.id.*");
        for (const auto &item : keys.reply_array) {
            std::string cookieName = item.str.substr(11);

            auto cookie_id = m_database->Run("GET %s", item.str.c_str());
            if (cookie_id.type != REDIS_REPLY_STRING) {
                g_pSM->LogError(myself, "Expect %s to be REDIS_REPLY_STRING but got %d", item.str.c_str(), cookie_id.type);
            }

            std::string key_name = steamId + "." + cookie_id.str;
            std::string key_desc = "cookies.desc." + cookieName;
            std::string key_id = "cookies.access." + cookieName;

            auto value = m_database->Run("GET %s", key_name.c_str());
            auto desc = m_database->Run("GET %s", key_desc.c_str());
            auto access = m_database->Run("GET %s", key_id.c_str());

            if (value.type == REDIS_REPLY_STRING) {
                CookieAccess acce;
                try {
                    acce = (CookieAccess)std::stoi(access.str);
                } catch (const std::exception&) {
                    acce = CookieAccess_Public;
                }

                m_results.push_back({
                    Cookie(
                        cookieName.data(),
                        desc.str.data(),
                        acce
                    ), value.str
                    });
            }
        }

        return true;
    }

    case Query_InsertData:
    {
        std::string safe_id = m_params.steamId;
        std::string safe_val = m_params.data->value;

        int cookieId = m_params.cookieId;
        safe_id = safe_id + "." + std::to_string(cookieId);

        bool res = true;
        if (m_database->CanAsync()) {
            res &= m_database->CommandAsync("SET %s %s", safe_id.c_str(), safe_val.c_str());
            res &= m_database->CommandAsync("EXPIRE %s %d", safe_id.c_str(), 14 * 24 * 3600); // Set this key expire in 2 weeks
            return res;
        }

        res &= m_database->Execute("SET %s %s", safe_id.c_str(), safe_val.c_str());
        res &= m_database->Execute("EXPIRE %s %d", safe_id.c_str(), 14 * 24 * 3600); // Set this key expire in 2 weeks
        return res;
    }

    case Query_SelectId:
    {
        std::string safe_name = m_params.steamId;

        std::hash<std::string> h;
        int id = h(safe_name) & 0x7FFFFFFF;

        auto rep = m_database->Run("GET %s", ("cookies.id." + safe_name).c_str());
        if (rep.type != REDIS_REPLY_STRING) {
            return false;
        }

        m_insertId = std::stoi(rep.str);
        if (id != m_insertId) {
            g_pSM->LogError(myself, "Cookies ID does not match %d vs %d", id, m_insertId);
        }

        return true;
    }
    }

    return false;
}

querytype TQueryOp::PullQueryType()
{
    return m_type;
}

int TQueryOp::PullQuerySerial()
{
    return m_serial;
}

ParamData::~ParamData()
{
    if (data) {
        /* Data is only ever passed in a client disconnect query and always needs to be deleted */
        delete data;
        data = NULL;
    }
}

ParamData::ParamData()
{
    cookie = NULL;
    data = NULL;
    steamId[0] = '\0';
    cookieId = 0;
}
