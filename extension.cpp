#include "extension.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>

CSMExtension g_SMExtension;
SMEXT_LINK(&g_SMExtension);

static bool ParseRestartTime(const char *str, int &outSeconds)
{
    int hour = -1, minute = -1;
    if (sscanf(str, "%d:%d", &hour, &minute) == 2
        && hour >= 0 && hour < 24 && minute >= 0 && minute < 60)
    {
        outSeconds = hour * 3600 + minute * 60;
        return true;
    }
    return false;
}

bool CSMExtension::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
    // 1. Try environment variable (Docker usage)
    const char *envVal = getenv("daily_restart_time");
    if (envVal && strlen(envVal) > 0)
    {
        if (ParseRestartTime(envVal, m_DailyRestartSeconds))
        {
            smutils->LogMessage(myself, "Daily restart time set to %s UTC (from env)", envVal);
        }
        else
        {
            smutils->LogError(myself, "Invalid daily_restart_time env var: '%s' (expected HH:mm)", envVal);
        }
        return true;
    }

    // 2. Fall back to config file: addons/sourcemod/configs/autorestart.cfg
    char cfgPath[PLATFORM_MAX_PATH];
    smutils->BuildPath(Path_SM, cfgPath, sizeof(cfgPath), "configs/autorestart.cfg");

    FILE *fp = fopen(cfgPath, "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            // Strip trailing newline / carriage return
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';

            // Skip blank lines and comments
            if (len == 0 || line[0] == '/' || line[0] == '#')
                continue;

            // Expect "daily_restart_time=HH:mm"
            const char *key = "daily_restart_time=";
            if (strncmp(line, key, strlen(key)) == 0)
            {
                const char *val = line + strlen(key);
                if (ParseRestartTime(val, m_DailyRestartSeconds))
                {
                    smutils->LogMessage(myself, "Daily restart time set to %s UTC (from %s)", val, cfgPath);
                }
                else
                {
                    smutils->LogError(myself, "Invalid daily_restart_time in config: '%s' (expected HH:mm)", val);
                }
                break;
            }
        }
        fclose(fp);
    }

    return true;
}

void CSMExtension::SDK_OnAllLoaded()
{
    m_pTimer = timersys->CreateTimer(this, 10.0f, nullptr, TIMER_FLAG_REPEAT);
}

void CSMExtension::SDK_OnUnload()
{
    if (m_pTimer)
    {
        timersys->KillTimer(m_pTimer);
        m_pTimer = nullptr;
    }
}

void CSMExtension::OnLevelShutdown()
{
    if (m_bRestartNeeded || m_bScheduledRestartNeeded)
    {
        gamehelpers->ServerCommand("quit\n");
    }
}

ResultType CSMExtension::OnTimer(ITimer *pTimer, void *pData)
{
    bool isDailyRestartDue = CheckDailyRestart();

    if (isDailyRestartDue || m_bScheduledRestartNeeded)
    {
        if (isDailyRestartDue && !m_bScheduledRestartNeeded)
        {
            m_bScheduledRestartNeeded = true;

            time_t now = time(nullptr);
            struct tm *utcNow = gmtime(&now);
            m_LastRestartYear = utcNow->tm_year;
            m_LastRestartYday = utcNow->tm_yday;
        }

        if (CountHumanPlayers() == 0)
        {
            gamehelpers->ServerCommand("quit\n");
        }
        else if (!m_bRestartNeeded)
        {
            m_bRestartNeeded = true;
            gamehelpers->ServerCommand("say The server will restart at the next opportunity!\n");
        }
    }

    return Pl_Continue;
}

void CSMExtension::OnTimerEnd(ITimer *pTimer, void *pData)
{
}

bool CSMExtension::CheckDailyRestart()
{
    if (m_DailyRestartSeconds < 0 || m_bScheduledRestartNeeded)
        return false;

    time_t now = time(nullptr);
    struct tm *utcNow = gmtime(&now);

    if (utcNow->tm_year == m_LastRestartYear && utcNow->tm_yday == m_LastRestartYday)
        return false;

    int todaySeconds = utcNow->tm_hour * 3600 + utcNow->tm_min * 60 + utcNow->tm_sec;
    return todaySeconds >= m_DailyRestartSeconds;
}

int CSMExtension::CountHumanPlayers()
{
    int count = 0;
    int maxClients = playerhelpers->GetMaxClients();
    for (int i = 1; i <= maxClients; i++)
    {
        IGamePlayer *pPlayer = playerhelpers->GetGamePlayer(i);
        if (pPlayer && pPlayer->IsInGame() && !pPlayer->IsFakeClient())
            count++;
    }
    return count;
}
