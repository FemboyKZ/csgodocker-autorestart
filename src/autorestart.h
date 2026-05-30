/**
 * AutoRestart - SourceMod extension (CSGO / SM 1.12)
 *
 * Restarts the server when a game/plugin update is detected
 * (via csgodocker's /watchdog version files) or
 * at a configured daily time. Meant to be used with csgodocker.
 */

#pragma once

#include "smsdk_ext.h"

#include <ctime>
#include <map>
#include <string>

class AutoRestart : public SDKExtension
{
public:
	bool SDK_OnLoad(char *error, size_t maxlen, bool late) override;
	void SDK_OnUnload() override;

#if defined SMEXT_CONF_METAMOD
	bool SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
#endif
	void Hook_LevelShutdown();

	static void OnGameFrame(bool simulating);

private:
	void CheckAndRestart();
	bool IsServerOutOfDate();
	bool CheckDailyRestart();
	int CountHumanPlayers() const;
	void PrintToChatAll(const char *msg);
	std::map<std::string, std::string> ReadPluginVersions() const;

	std::string m_buildVersion;
	std::map<std::string, std::string> m_pluginVersions; // snapshot taken at load

	std::string m_discordWebhook;   // optional Discord webhook URL (env: discord_webhook)
	bool m_discordNotified = false; // post to Discord only once per restart decision

	bool m_restartNeeded = false;
	bool m_scheduledRestartNeeded = false;

	bool m_hasDailyRestart = false;
	int m_dailyRestartSeconds = 0;  // seconds since UTC midnight
	int m_lastDailyRestartDay = -1; // days since unix epoch (UTC) of last daily restart

	time_t m_lastCheckTime = 0; // wall-clock seconds of last 10s tick
};

extern AutoRestart g_AutoRestart;
