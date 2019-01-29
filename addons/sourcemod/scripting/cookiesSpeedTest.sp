#include <sourcemod>

char szLogPath[PLATFORM_MAX_PATH];

float fConnected[MAXPLAYERS+1];
float fPutInServer[MAXPLAYERS+1];

public void OnPluginStart()
{
    BuildPath(Path_SM, szLogPath, sizeof(szLogPath), "logs/cokkies_debug.log");
}

public void OnClientPutInServer(int client)
{
    fPutInServer[client] = GetEngineTime();
}

public void OnClientCookiesCached(int client)
{
    float now = GetEngineTime();

    if (fEngTime_PutInServer[client] <= 0.0) {
        LogToFileEx(szLogPath, "%N Cookies Loaded before PutInServer, %.2fs after connected.", client, now - fConnected[client]);
    } else {
        float delta1 = now - fPutInServer[client];
        float delta2 = now - fConnected[client];
        LogToFileEx(szLogPath, "%N Cookies Loaded %.2fs after connected, %.2f after PutInServer", client, delta1, delta2);
    }
}

public void OnClientConnected(int client)
{
    fConnected[client] = GetEngineTime();
    fPutInServer[client] = 0.0;
}
