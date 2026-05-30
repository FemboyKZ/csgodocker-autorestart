#include "autorestart.h"
#include "discord.h"

#include <eiface.h>
#include <ISourceMod.h>
#include <IPlayerHelpers.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

AutoRestart g_AutoRestart;
SMEXT_LINK(&g_AutoRestart);

SH_DECL_HOOK0_void(IServerGameDLL, LevelShutdown, SH_NOATTRIB, 0);

static const char *kBuildVersionFile = "/watchdog/csgo/latest.txt";
static const char *kLayersDir = "/watchdog/layers";

static std::string Trim(const std::string &s)
{
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
	{
		return "";
	}
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

static std::string ReadFileTrimmed(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::binary);
	if (!in)
	{
		return "";
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return Trim(ss.str());
}

bool AutoRestart::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	const char *buildVer = std::getenv("build_ver");
	if (!buildVer || !*buildVer)
	{
		smutils->Format(error, maxlen, "Environment variable 'build_ver' was not found, this extension is meant to be used with csgodocker!");
		return false;
	}
	m_buildVersion = Trim(buildVer);

	// Snapshot current plugin versions from the watchdog layer latest.txt files.
	m_pluginVersions = ReadPluginVersions();

	// Parse optional daily restart time (UTC, "HH:mm" or "HH:mm:ss").
	const char *dailyStr = std::getenv("daily_restart_time");
	if (dailyStr && *dailyStr)
	{
		int hh = 0, mm = 0, ss = 0;
		int n = std::sscanf(Trim(dailyStr).c_str(), "%d:%d:%d", &hh, &mm, &ss);
		if (n >= 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60 && ss >= 0 && ss < 60)
		{
			m_dailyRestartSeconds = hh * 3600 + mm * 60 + ss;
			m_hasDailyRestart = true;
		}
	}

	// Optional Discord webhook for restart notifications.
	const char *webhook = std::getenv("discord_webhook");
	if (webhook && *webhook)
	{
		m_discordWebhook = Trim(webhook);
	}

	smutils->AddGameFrameHook(&AutoRestart::OnGameFrame);

	g_pSM->LogMessage(myself, "Loaded. build_ver=%s, daily_restart=%s, discord=%s", m_buildVersion.c_str(), m_hasDailyRestart ? "on" : "off",
					  m_discordWebhook.empty() ? "off" : "on");

	return true;
}

void AutoRestart::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(&AutoRestart::OnGameFrame);

#if defined SMEXT_CONF_METAMOD
	if (gamedll)
	{
		SH_REMOVE_HOOK(IServerGameDLL, LevelShutdown, gamedll, SH_MEMBER(this, &AutoRestart::Hook_LevelShutdown), true);
	}
#endif
}

#if defined SMEXT_CONF_METAMOD
bool AutoRestart::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	// engine and gamedll are already resolved by smsdk_ext before this is called.
	SH_ADD_HOOK(IServerGameDLL, LevelShutdown, gamedll, SH_MEMBER(this, &AutoRestart::Hook_LevelShutdown), true);
	return true;
}
#endif

std::map<std::string, std::string> AutoRestart::ReadPluginVersions() const
{
	std::map<std::string, std::string> versions;

#ifndef _WIN32
	DIR *dir = opendir(kLayersDir);
	if (!dir)
	{
		return versions;
	}

	while (struct dirent *ent = readdir(dir))
	{
		std::string name = ent->d_name;
		if (name == "." || name == "..")
		{
			continue;
		}

		std::string full = std::string(kLayersDir) + "/" + name;
		struct stat st;
		if (stat(full.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
		{
			continue;
		}

		std::string latest = ReadFileTrimmed(full + "/latest.txt");
		if (!latest.empty())
		{
			versions[name] = latest;
		}
	}
	closedir(dir);
#endif

	return versions;
}

bool AutoRestart::IsServerOutOfDate()
{
	std::string latestBuild = ReadFileTrimmed(kBuildVersionFile);
	if (!latestBuild.empty() && m_buildVersion != latestBuild)
	{
		return true;
	}

	// Check if any plugin layer has been updated since startup.
	std::map<std::string, std::string> current = ReadPluginVersions();
	for (std::map<std::string, std::string>::const_iterator it = m_pluginVersions.begin(); it != m_pluginVersions.end(); ++it)
	{
		std::map<std::string, std::string>::const_iterator cur = current.find(it->first);
		if (cur != current.end() && cur->second != it->second)
		{
			return true;
		}
	}
	return false;
}

bool AutoRestart::CheckDailyRestart()
{
	if (!m_hasDailyRestart || m_scheduledRestartNeeded)
	{
		return false;
	}

	time_t now = time(NULL);
	int today = static_cast<int>(now / 86400);    // days since epoch (UTC)
	int secOfDay = static_cast<int>(now % 86400); // seconds since UTC midnight

	return today > m_lastDailyRestartDay && secOfDay >= m_dailyRestartSeconds;
}

int AutoRestart::CountHumanPlayers() const
{
	if (!playerhelpers)
	{
		return 0;
	}

	int count = 0;
	int maxClients = playerhelpers->GetMaxClients();
	for (int i = 1; i <= maxClients; i++)
	{
		IGamePlayer *player = playerhelpers->GetGamePlayer(i);
		if (player && player->IsConnected() && !player->IsFakeClient())
		{
			count++;
		}
	}
	return count;
}

void AutoRestart::PrintToChatAll(const char *msg)
{
	if (!engine)
	{
		return;
	}
	char buf[256];
	smutils->Format(buf, sizeof(buf), "say \"%s\"\n", msg);
	engine->ServerCommand(buf);
}

void AutoRestart::CheckAndRestart()
{
	bool isDailyRestartDue = CheckDailyRestart();

	if (!(isDailyRestartDue || m_scheduledRestartNeeded || IsServerOutOfDate()))
	{
		return;
	}

	if (isDailyRestartDue && !m_scheduledRestartNeeded)
	{
		m_scheduledRestartNeeded = true;
		m_lastDailyRestartDay = static_cast<int>(time(NULL) / 86400);
	}

	int numPlayers = CountHumanPlayers();

	// Notify Discord once per restart decision.
	if (!m_discordNotified && !m_discordWebhook.empty())
	{
		m_discordNotified = true;
		const char *reason = (isDailyRestartDue || m_scheduledRestartNeeded) ? "scheduled daily restart" : "server update";
		char buf[256];
		if (numPlayers == 0)
		{
			smutils->Format(buf, sizeof(buf), ":arrows_counterclockwise: AutoRestart: %s - server empty, restarting now.", reason);
		}
		else
		{
			smutils->Format(buf, sizeof(buf), ":arrows_counterclockwise: AutoRestart: %s - %d player%s online, restarting at next map.", reason,
							numPlayers, numPlayers == 1 ? "" : "s");
		}
		Discord_PostWebhook(m_discordWebhook, buf);
	}

	if (numPlayers == 0)
	{
		if (engine)
		{
			engine->ServerCommand("quit\n");
		}
	}
	else if (!m_restartNeeded)
	{
		m_restartNeeded = true;
		PrintToChatAll("The server will restart at the next opportunity!");
	}
}

void AutoRestart::OnGameFrame(bool simulating)
{
	time_t now = time(NULL);
	if (now - g_AutoRestart.m_lastCheckTime < 10)
	{
		return;
	}

	g_AutoRestart.m_lastCheckTime = now;
	g_AutoRestart.CheckAndRestart();
}

void AutoRestart::Hook_LevelShutdown()
{
	if (engine && (m_restartNeeded || m_scheduledRestartNeeded || IsServerOutOfDate()))
	{
		engine->ServerCommand("quit\n");
	}

	RETURN_META(MRES_IGNORED);
}
