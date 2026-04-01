#ifndef _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_

#include "smsdk_ext.h"
#include <ctime>

class CSMExtension : public SDKExtension, public ITimedEvent
{
public:
    virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);
    virtual void SDK_OnAllLoaded();
    virtual void SDK_OnUnload();
    virtual void OnLevelShutdown();

    // ITimedEvent
    virtual ResultType OnTimer(ITimer *pTimer, void *pData);
    virtual void OnTimerEnd(ITimer *pTimer, void *pData);

private:
    bool CheckDailyRestart();
    int CountHumanPlayers();

    ITimer *m_pTimer = nullptr;
    bool m_bRestartNeeded = false;
    bool m_bScheduledRestartNeeded = false;
    int m_DailyRestartSeconds = -1; // seconds from midnight UTC, -1 = not set
    int m_LastRestartYear = -1;
    int m_LastRestartYday = -1;
};

extern CSMExtension g_SMExtension;

#endif // _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
