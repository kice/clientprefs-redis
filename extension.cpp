/**
 * vim: set ts=4 sw=4 tw=99 noet:
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

#include <sourcemod_version.h>
#include "extension.h"

#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <intrin.h>

using namespace ke;

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

ClientPrefs g_ClientPrefs;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_ClientPrefs);

HandleType_t g_CookieType = 0;
CookieTypeHandler g_CookieTypeHandler;

HandleType_t g_CookieIterator = 0;
CookieIteratorHandler g_CookieIteratorHandler;
DbDriver g_DriverType;

long volatile worker_exit = 0;

static void FrameHook(bool simulating)
{
    g_ClientPrefs.RunFrame();
}

bool ClientPrefs::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
    auto DBInfo = dbi->FindDatabaseConf("clientprefs_redis");

    if (DBInfo->driver && DBInfo->driver[0] != '\0') {
        if (strcmp(DBInfo->driver, "redis") != 0) {
            ke::SafeStrcpy(error, maxlength, "Only support redis as database");
            return false;
        }
    }

    struct sockaddr_in sa;
    if (inet_pton(AF_INET, DBInfo->host, &(sa.sin_addr)) == 0) {
        ke::SafeSprintf(error, maxlength, "Invalid IP address \"%s\"", DBInfo->host);
        return false;
    }

    host = DBInfo->host;
    port = DBInfo->port == 0 ? 6379 : DBInfo->port;
    maxTimeout = DBInfo->maxTimeout;

    if (DBInfo->pass != nullptr && DBInfo->pass[0] != '\x0') {
        pass = DBInfo->pass;
    }

    if (DBInfo->database == nullptr || DBInfo->database[0] == '\x0') {
        dbid = 0;
    } else {
        dbid = atoi(DBInfo->database);
    }

    tqq = new TQueue();

    const char *num_worker = smutils->GetCoreConfigValue("RedisQueryThread");
    if (num_worker == NULL) {
        worker = 4;
    } else {
        worker = atoi(num_worker);
    }

    bool noasync = false;
    const char *_noasync = smutils->GetCoreConfigValue("RedisDBNoASync");
    if (_noasync) {
        noasync = atoi(_noasync) != 0;
    }

    smutils->LogMessage(myself, "Connecting to %s:%d %s password using %d query thread(s).",
        host.c_str(), port, pass.empty() ? "without" : "with", worker);

    for (int i = 0; i < worker; ++i) {
        std::thread([this, i, noasync] {
            RedisDB *db = nullptr;
            auto connectDb = [&]() {
                while (db == nullptr) {
                    Sleep(3000);

                    std::unique_lock<std::mutex> _(connectLock);
                    db = RedisDB::GetRedisDB(host, port, maxTimeout, pass);
                    if (noasync || !db) {
                        continue;
                    }

                    if (int err = db->ConnectAsync(host, port, pass)) {
                        fprintf(stderr, "[%d] Cannot use async connection: %d (error: %s).\n",
                            i, err, db->GetASyncErrorStr());
                    } else {
                        // Async connection is available
                        fprintf(stderr, "[%d] Connected to %s:%d with non-blocking context.\n",
                            i, host.c_str(), port);
                    }
                }
            };

            {
                connectDb();
                auto reply = db->Run("SCRIPT LOAD %s", GET_CLIENT_COOKIES);
                if (reply.str != GET_CLIENT_COOKIES_SHA) {
                    fprintf(stderr, "Lua script sha dose not match: except:%s actual:%s",
                        GET_CLIENT_COOKIES_SHA, reply.str.c_str());
                }
            }

            while (true) {
                auto op = tqq->GetQuery();
                if (op == nullptr) {
                    _InterlockedDecrement(&worker_exit);
                    break;
                }

                if (db == nullptr || !db->Alive()) {
                    delete db;
                    db = nullptr;
                    connectDb();
                }

                db->SELECT_DBID(dbid);
                op->SetDatabase(db);
                op->RunThreadPart();
                tqq->PutResult(op);
            }

            delete db;
            db = nullptr;
        }).detach();
    }
    // dbi->AddDependency(myself, Driver);

    sharesys->AddNatives(myself, g_ClientPrefNatives);
    sharesys->RegisterLibrary(myself, "clientprefs");
    identity = sharesys->CreateIdentity(sharesys->CreateIdentType("ClientPrefs"), this);
    g_CookieManager.cookieDataLoadedForward = forwards->CreateForward("OnClientCookiesCached", ET_Ignore, 1, NULL, Param_Cell);

    g_CookieType = handlesys->CreateType("Cookie",
        &g_CookieTypeHandler,
        0,
        NULL,
        NULL,
        myself->GetIdentity(),
        NULL);

    g_CookieIterator = handlesys->CreateType("CookieIterator",
        &g_CookieIteratorHandler,
        0,
        NULL,
        NULL,
        myself->GetIdentity(),
        NULL);

    IMenuStyle *style = menus->GetDefaultStyle();
    g_CookieManager.clientMenu = style->CreateMenu(&g_Handler, identity);
    g_CookieManager.clientMenu->SetDefaultTitle("Client Settings:");

    plsys->AddPluginsListener(&g_CookieManager);

    phrases = translator->CreatePhraseCollection();
    phrases->AddPhraseFile("clientprefs.phrases");
    phrases->AddPhraseFile("common.phrases");

    if (late) {
        CatchLateLoadClients();
    }

    return true;
}

void ClientPrefs::SDK_OnAllLoaded()
{
    playerhelpers->AddClientListener(&g_CookieManager);
    g_pSM->AddGameFrameHook(FrameHook);
}

void ClientPrefs::SDK_OnUnload()
{
    g_pSM->RemoveGameFrameHook(FrameHook);

    for (int i = 0; i < worker; ++i) {
        tqq->AddToThreadQueue(nullptr, 0);
    }

    while (auto res = _InterlockedCompareExchange(&worker_exit, 0, 0)) {
        Sleep(100);
    }

    delete tqq;
}

bool ClientPrefs::QueryInterfaceDrop(SMInterface *pInterface)
{
    // if ((void *)pInterface == (void *)(Database->GetDriver())) {
    if ((void *)pInterface) {
        return false;
    }

    return true;
}

//void ClientPrefs::NotifyInterfaceDrop(SMInterface *pInterface)
//{
//	if (Database && (void *)pInterface == (void *)(Database->GetDriver()))
//		Database = NULL;
//}

void ClientPrefs::SDK_OnDependenciesDropped()
{
    // At this point, we're guaranteed that DBI has flushed the worker thread
    // for us, so no cookies should have outstanding queries.
    g_CookieManager.Unload();

    handlesys->RemoveType(g_CookieType, myself->GetIdentity());
    handlesys->RemoveType(g_CookieIterator, myself->GetIdentity());

    // Database = NULL;

    if (g_CookieManager.cookieDataLoadedForward != NULL) {
        forwards->ReleaseForward(g_CookieManager.cookieDataLoadedForward);
        g_CookieManager.cookieDataLoadedForward = NULL;
    }

    if (g_CookieManager.clientMenu != NULL) {
        Handle_t menuHandle = g_CookieManager.clientMenu->GetHandle();

        if (menuHandle != BAD_HANDLE) {
            HandleSecurity sec = HandleSecurity(identity, identity);
            HandleError err = handlesys->FreeHandle(menuHandle, &sec);
            if (HandleError_None != err) {
                g_pSM->LogError(myself, "Error %d when attempting to free client menu handle", err);
            }
        }

        g_CookieManager.clientMenu = NULL;
    }

    if (phrases != NULL) {
        phrases->Destroy();
        phrases = NULL;
    }

    plsys->RemovePluginsListener(&g_CookieManager);
    playerhelpers->RemoveClientListener(&g_CookieManager);
}

void ClientPrefs::OnCoreMapStart(edict_t *pEdictList, int edictCount, int clientMax)
{
    AttemptReconnection();
}

void ClientPrefs::AttemptReconnection()
{
    CatchLateLoadClients(); /* DB reconnection, we should check if we missed anyone... */
}

void ClientPrefs::DatabaseConnect()
{
}

bool ClientPrefs::AddQueryToQueue(TQueryOp *query, int prio)
{
    tqq->AddToThreadQueue(query, prio);
    return true;
}

void ClientPrefs::RunFrame()
{
    auto op = tqq->GetResult();
    if (op == nullptr) {
        return;
    }

    op->RunThinkPart();
    op->Destroy();
}

const char *GetPlayerCompatAuthId(IGamePlayer *pPlayer)
{
    /* For legacy reasons, OnClientAuthorized gives the Steam2 id here if using Steam auth */
    const char *steamId = pPlayer->GetSteam2Id();
    return steamId ? steamId : pPlayer->GetAuthString();
}

void ClientPrefs::CatchLateLoadClients()
{
    IGamePlayer *pPlayer;
    for (int i = playerhelpers->GetMaxClients() + 1; --i > 0;) {
        if (g_CookieManager.AreClientCookiesPending(i) || g_CookieManager.AreClientCookiesCached(i)) {
            continue;
        }

        pPlayer = playerhelpers->GetGamePlayer(i);

        if (!pPlayer || !pPlayer->IsAuthorized()) {
            continue;
        }

        g_CookieManager.OnClientAuthorized(i, GetPlayerCompatAuthId(pPlayer));
    }
}

void ClientPrefs::ClearQueryCache(int serial)
{
    AutoLock lock(&queryLock);
    for (size_t iter = 0; iter < cachedQueries.length(); ++iter) {
        TQueryOp *op = cachedQueries[iter];
        if (op && op->PullQueryType() == Query_SelectData && op->PullQuerySerial() == serial) {
            op->Destroy();
            cachedQueries.remove(iter--);
        }
    }
}

bool Translate(char *buffer,
    size_t maxlength,
    const char *format,
    unsigned int numparams,
    size_t *pOutLength,
    ...)
{
    va_list ap;
    unsigned int i;
    const char *fail_phrase;
    void *params[MAX_TRANSLATE_PARAMS];

    if (numparams > MAX_TRANSLATE_PARAMS) {
        assert(false);
        return false;
    }

    va_start(ap, pOutLength);
    for (i = 0; i < numparams; i++) {
        params[i] = va_arg(ap, void *);
    }
    va_end(ap);

    if (!g_ClientPrefs.phrases->FormatString(buffer,
        maxlength,
        format,
        params,
        numparams,
        pOutLength,
        &fail_phrase)) {
        if (fail_phrase != NULL) {
            g_pSM->LogError(myself, "[SM] Could not find core phrase: %s", fail_phrase);
        } else {
            g_pSM->LogError(myself, "[SM] Unknown fatal error while translating a core phrase.");
        }

        return false;
    }

    return true;
}

char * UTIL_strncpy(char * destination, const char * source, size_t num)
{
    if (source == NULL) {
        destination[0] = '\0';
        return destination;
    }

    size_t req = strlen(source);
    if (!req) {
        destination[0] = '\0';
        return destination;
    } else if (req >= num) {
        req = num - 1;
    }

    strncpy(destination, source, req);
    destination[req] = '\0';
    return destination;
}

IdentityToken_t *ClientPrefs::GetIdentity() const
{
    return identity;
}

const char *ClientPrefs::GetExtensionVerString()
{
    return SOURCEMOD_VERSION;
}

const char *ClientPrefs::GetExtensionDateString()
{
    return SOURCEMOD_BUILD_TIME;
}

ClientPrefs::ClientPrefs()
{
    // Driver = NULL;
    databaseLoading = false;
    phrases = NULL;
    // DBInfo = NULL;

    identity = NULL;
}
