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
            m_database->GetErrorString(),
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
    m_database = (async_redis::client *)db;
}

bool TQueryOp::BindParamsAndRun()
{
    switch (m_type) {
    case Query_InsertCookie:
    {
        std::string safe_name = m_params.cookie->name;
        std::string safe_desc = m_params.cookie->description;

        // check if we have that cookie
        {
            auto reply = m_database->Command({ "GET", "cookies.id." + safe_name }).get();
            if (reply->IsString()) {
                m_database->Append({ "SADD", "cookies.list", safe_name }).Commit();
                m_insertId = std::stoi(reply->GetString());
                return true;
            }
        }

        std::hash<std::string> h;
        m_insertId = h(safe_name) & 0x7FFFFFFF;

        auto &client = *m_database;
        client.Append({ "SADD", "cookies.list", safe_name })
            .Append({ "SET", "cookies.access." + safe_name, std::to_string((int)m_params.cookie->access) })
            .Append({ "SET", "cookies.desc." + safe_name, safe_desc })
            .Append({ "SET", "cookies.id." + safe_name, std::to_string(m_insertId) })
            .Commit();
        return true;
    }

    case Query_SelectData:
    {
        m_results.clear();
        std::string steamId = m_params.steamId;

        // Try to use cached Lua query first
        auto cookies = m_database->Command({ "EVALSHA", GET_CLIENT_COOKIES_SHA, "1", steamId }).get();
        if (cookies && cookies->Ok()) {
            if (!cookies->IsArrays()) {
                // nothing here
                return true;
            }

            for (const auto &cookieData : cookies->GetArray()) {
                if (!cookieData.IsArrays() || cookieData.GetArray().size() != 4) {
                    break;
                }

                const auto &cookie = cookieData.GetArray();

                CookieAccess acce;
                try {
                    acce = (CookieAccess)std::stoi(cookie[CookieAcce].GetString());
                } catch (const std::exception&) {
                    acce = CookieAccess_Public;
                }

                if (cookie[CookieValue].IsString()) {
                    m_results.push_back({
                        Cookie(
                            cookie[CookieName].GetString().c_str(),
                            cookie[CookieDesc].GetString().c_str(),
                            acce),
                            cookie[CookieValue].GetString().c_str()
                        });
                }
            }
            return true;
        }

        // Use normal and slow method, if you have too many cookies (>= 10M), you should use the lua method
        auto keys = m_database->Command({ "KEYS", "cookies.id.*" }).get();
        if (!keys || !keys->IsArrays()) {
            return true;
        }

        for (const auto &item : keys->GetArray()) {
            std::string cookieName = item.GetString().substr(11);

            auto cookie_id = m_database->Command({ "GET", item.GetString() }).get();
            if (!cookie_id->IsString()) {
                g_pSM->LogError(myself, "Expect %s to be REDIS_REPLY_STRING but got %d", item.GetString().c_str(), cookie_id->Type());
                continue;
            }

            std::string key_name = steamId + "." + cookie_id->GetString();
            std::string key_desc = "cookies.desc." + cookieName;
            std::string key_id = "cookies.access." + cookieName;

            auto _value = m_database->Command({ "GET", key_name }, false);
            auto _desc = m_database->Command({ "GET", key_desc }, false);
            auto _access = m_database->Command({ "GET", key_id }, false);
            m_database->Commit();

            auto value = _value.get();
            auto desc = _desc.get();
            auto access = _access.get();

            if (value->IsString()) {
                CookieAccess acce;
                try {
                    acce = (CookieAccess)std::stoi(access->GetString());
                } catch (const std::exception&) {
                    acce = CookieAccess_Public;
                }

                m_results.push_back({
                    Cookie(
                        cookieName.c_str(),
                        desc->GetString().c_str(),
                        acce
                    ), value->GetString()
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

        m_database->Append({ "SET", safe_id, safe_val, "EX", "1209600" }).Commit(); // Set this key expire in 2 weeks
        return true;
    }

    case Query_SelectId:
    {
        std::string safe_name = m_params.steamId;

        std::hash<std::string> h;
        int id = h(safe_name) & 0x7FFFFFFF;

        auto rep = m_database->Command({ "GET", "cookies.id." + safe_name }).get();
        if (rep->IsString()) {
            return false;
        }

        m_insertId = std::stoi(rep->GetString());
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
