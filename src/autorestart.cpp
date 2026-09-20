#include "autorestart.h"
#include "discord.h"

#include <inetchannelinfo.h>
#include "tier0/dbg.h"
#include "tier0/platform.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

AutoRestartPlugin g_AutoRestartPlugin;

IVEngineServer *engine = nullptr;
IServerGameDLL *server = nullptr;
IServerGameClients *gameclients = nullptr;
CGlobalVars *gpGlobals = nullptr;

PLUGIN_EXPOSE(AutoRestartPlugin, g_AutoRestartPlugin);

#ifndef AUTORESTART_KHOOK
SH_DECL_HOOK1_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool);
SH_DECL_HOOK3_void(IServerGameDLL, ServerActivate, SH_NOATTRIB, 0, edict_t *, int, int);
SH_DECL_HOOK1_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, edict_t *);
SH_DECL_HOOK1_void(IServerGameDLL, ServerHibernationUpdate, SH_NOATTRIB, 0, bool);
#endif

static const char *kBuildVersionFile = "/watchdog/csgo/latest.txt";
static const char *kLayersDir = "/watchdog/layers";
static const double kVersionCheckInterval = 60.0; // seconds, GameFrame version poll
static const int kWatcherIntervalSeconds = 30;    // background poll cadence while hibernating
static const int kQuitTimeoutSeconds = 60;

// Engine shutdown can wedge after plugins unload (another plugin's thread, Steam, etc.),
// leaving a dead server that csgodocker never relaunches. Force exit 0 so its loop restarts us.
// The thread only touches its own stack, and the .so is linked nodelete, so plugin unload is safe.
static void ArmQuitWatchdog()
{
	static std::atomic<bool> armed {false};
	if (armed.exchange(true))
	{
		return;
	}
	std::thread(
		[]
		{
			std::this_thread::sleep_for(std::chrono::seconds(kQuitTimeoutSeconds));
			static const char msg[] = "[AutoRestart] Shutdown stalled, forcing exit.\n";
			std::fwrite(msg, 1, sizeof(msg) - 1, stderr);
			std::fflush(stderr);
			std::_Exit(0);
		})
		.detach();
}

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

AutoRestartPlugin::AutoRestartPlugin()
#ifdef AUTORESTART_KHOOK
	: m_GameFrame(&IServerGameDLL::GameFrame, this, nullptr, &AutoRestartPlugin::Hook_GameFrame),
	  m_ServerActivate(&IServerGameDLL::ServerActivate, this, nullptr, &AutoRestartPlugin::Hook_ServerActivate),
	  m_ClientDisconnect(&IServerGameClients::ClientDisconnect, this, nullptr, &AutoRestartPlugin::Hook_ClientDisconnect),
	  m_ServerHibernationUpdate(&IServerGameDLL::ServerHibernationUpdate, this, nullptr, &AutoRestartPlugin::Hook_ServerHibernationUpdate)
#endif
{
}

bool AutoRestartPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_ANY(GetServerFactory, server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	gpGlobals = ismm->GetCGlobals();

	const char *buildVer = std::getenv("build_ver");
	if (!buildVer || !*buildVer)
	{
		ismm->Format(error, maxlen, "Environment variable 'build_ver' was not found, this plugin is meant to be used with csgodocker!");
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

			time_t now = time(nullptr);
			int today = static_cast<int>(now / 86400);
			int secOfDay = static_cast<int>(now % 86400);
			m_lastDailyRestartDay = (secOfDay >= m_dailyRestartSeconds) ? today : today - 1;
		}
	}

	// Optional Discord webhook for restart notifications.
	const char *webhook = std::getenv("discord_webhook");
	if (webhook && *webhook)
	{
		m_discordWebhook = Trim(webhook);
	}

	// Optional server name shown in the Discord notification.
	const char *serverName = std::getenv("server_name");
	if (serverName && *serverName)
	{
		m_serverName = Trim(serverName);
	}

	// On a late load the boot map's ServerActivate already fired,
	// count it as seen so the next map change isn't mistaken for the initial boot.
	if (late)
	{
		m_activateCount = 1;
		Msg("[AutoRestart] Late load detected; %d player(s) currently connected.\n", CountHumanPlayers());
	}

#ifdef AUTORESTART_KHOOK
	m_GameFrame.Add(server);
	m_ServerActivate.Add(server);
	m_ClientDisconnect.Add(gameclients);
	m_ServerHibernationUpdate.Add(server);
#else
	SH_ADD_HOOK(IServerGameDLL, GameFrame, server, SH_MEMBER(this, &AutoRestartPlugin::Hook_GameFrame), true);
	SH_ADD_HOOK(IServerGameDLL, ServerActivate, server, SH_MEMBER(this, &AutoRestartPlugin::Hook_ServerActivate), true);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_MEMBER(this, &AutoRestartPlugin::Hook_ClientDisconnect), true);
	SH_ADD_HOOK(IServerGameDLL, ServerHibernationUpdate, server, SH_MEMBER(this, &AutoRestartPlugin::Hook_ServerHibernationUpdate), true);
#endif

	// 0.0 forces a check on the first frame
	m_lastCheckTime = 0.0;
	m_lastVersionCheckTime = -kVersionCheckInterval;

	// Watch for updates while the server hibernates, where GameFrame is frozen.
	m_watcherThread = std::thread(&AutoRestartPlugin::WatcherLoop, this);

	Msg("[AutoRestart] Loaded. build_ver=%s, daily_restart=%s, discord=%s\n", m_buildVersion.c_str(), m_hasDailyRestart ? "on" : "off",
		m_discordWebhook.empty() ? "off" : "on");

	return true;
}

bool AutoRestartPlugin::Unload(char *error, size_t maxlen)
{
	// Stop the watcher thread before tearing down hooks.
	{
		std::lock_guard<std::mutex> lock(m_watcherMutex);
		m_stopWatcher = true;
	}
	m_watcherCv.notify_all();
	if (m_watcherThread.joinable())
	{
		m_watcherThread.join();
	}

#ifdef AUTORESTART_KHOOK
	m_GameFrame.Remove(server);
	m_ServerActivate.Remove(server);
	m_ClientDisconnect.Remove(gameclients);
	m_ServerHibernationUpdate.Remove(server);
#else
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, server, SH_MEMBER(this, &AutoRestartPlugin::Hook_GameFrame), true);
	SH_REMOVE_HOOK(IServerGameDLL, ServerActivate, server, SH_MEMBER(this, &AutoRestartPlugin::Hook_ServerActivate), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, gameclients, SH_MEMBER(this, &AutoRestartPlugin::Hook_ClientDisconnect), true);
	SH_REMOVE_HOOK(IServerGameDLL, ServerHibernationUpdate, server, SH_MEMBER(this, &AutoRestartPlugin::Hook_ServerHibernationUpdate), true);
#endif
	return true;
}

std::map<std::string, std::string> AutoRestartPlugin::ReadPluginVersions() const
{
	std::map<std::string, std::string> versions;

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

	return versions;
}

bool AutoRestartPlugin::VersionFileUnchanged(const std::string &path)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
	{
		// Can't stat
		return false;
	}
	auto it = m_versionMtimes.find(path);
	if (it != m_versionMtimes.end() && it->second.tv_sec == st.st_mtim.tv_sec && it->second.tv_nsec == st.st_mtim.tv_nsec)
	{
		return true;
	}
	m_versionMtimes[path] = st.st_mtim;
	return false;
}

bool AutoRestartPlugin::IsServerOutOfDate()
{
	// Build version: only re-read when the file's mtime has moved.
	if (!VersionFileUnchanged(kBuildVersionFile))
	{
		std::string latestBuild = ReadFileTrimmed(kBuildVersionFile);
		if (!latestBuild.empty() && m_buildVersion != latestBuild)
		{
			return true;
		}
	}

	// Plugin layers: iterate the names captured at startup and
	// skip any whose latest.txt mtime is unchanged since we last looked.
	for (const auto &[name, startupVer] : m_pluginVersions)
	{
		std::string path = std::string(kLayersDir) + "/" + name + "/latest.txt";
		if (VersionFileUnchanged(path))
		{
			continue;
		}
		std::string current = ReadFileTrimmed(path);
		if (!current.empty() && current != startupVer)
		{
			return true;
		}
	}
	return false;
}

bool AutoRestartPlugin::IsOutOfDateSnapshot() const
{
	std::string latestBuild = ReadFileTrimmed(kBuildVersionFile);
	if (!latestBuild.empty() && m_buildVersion != latestBuild)
	{
		return true;
	}

	for (const auto &[name, startupVer] : m_pluginVersions)
	{
		std::string current = ReadFileTrimmed(std::string(kLayersDir) + "/" + name + "/latest.txt");
		if (!current.empty() && current != startupVer)
		{
			return true;
		}
	}
	return false;
}

bool AutoRestartPlugin::CheckDailyRestart() const
{
	if (!m_hasDailyRestart || m_scheduledRestartNeeded)
	{
		return false;
	}

	time_t now = time(nullptr);
	int today = static_cast<int>(now / 86400);    // days since epoch (UTC)
	int secOfDay = static_cast<int>(now % 86400); // seconds since UTC midnight

	return today > m_lastDailyRestartDay && secOfDay >= m_dailyRestartSeconds;
}

int AutoRestartPlugin::CountHumanPlayers() const
{
	// Bots have no net channel, so only real (incl. still-connecting) clients count.
	int count = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		if (engine->GetPlayerNetInfo(i) != nullptr)
		{
			count++;
		}
	}
	return count;
}

void AutoRestartPlugin::PrintToChatAll(const char *msg)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "say \"%s\"\n", msg);
	engine->ServerCommand(buf);
}

void AutoRestartPlugin::QuitNow(const char *why)
{
	Msg("[AutoRestart] %s, shutting down server.\n", why);
	ArmQuitWatchdog();
	engine->ServerCommand("quit\n");
}

void AutoRestartPlugin::CheckAndRestart()
{
	bool isDailyRestartDue = CheckDailyRestart();

	if (!m_outOfDate)
	{
		double now = Plat_FloatTime();
		if (now - m_lastVersionCheckTime >= kVersionCheckInterval)
		{
			m_lastVersionCheckTime = now;
			m_outOfDate = IsServerOutOfDate();
		}
	}

	if (!(isDailyRestartDue || m_scheduledRestartNeeded || m_outOfDate))
	{
		return;
	}

	if (isDailyRestartDue && !m_scheduledRestartNeeded)
	{
		m_scheduledRestartNeeded = true;
		m_lastDailyRestartDay = static_cast<int>(time(nullptr) / 86400);
	}

	int numPlayers = CountHumanPlayers();

	bool scheduled = isDailyRestartDue || m_scheduledRestartNeeded;
	const char *reason = scheduled ? "Scheduled daily restart" : "Server update";

	// Notify Discord once per restart decision.
	if (!m_discordNotified && !m_discordWebhook.empty())
	{
		m_discordNotified = true;
		int color = scheduled ? 0x3498DB : 0xE67E22; // blue for daily, orange for update
		char desc[256];
		if (numPlayers == 0)
		{
			snprintf(desc, sizeof(desc), "%s - server empty, restarting now.", reason);
		}
		else
		{
			snprintf(desc, sizeof(desc), "%s - %d player%s online, restarting once empty or at next map change.", reason, numPlayers,
					 numPlayers == 1 ? "" : "s");
		}
		const char *title = m_serverName.empty() ? "AutoRestart" : m_serverName.c_str();
		Discord_PostEmbed(m_discordWebhook, title, desc, color);
	}

	if (numPlayers == 0)
	{
		if (!m_quitPending)
		{
			double delay = m_discordWebhook.empty() ? 0.0 : 5.0;
			m_quitAtTime = Plat_FloatTime() + delay;
			m_quitPending = true;
			Msg("[AutoRestart] %s: server empty, quitting in %.0fs.\n", reason, delay);
		}
	}
	else if (!m_restartNeeded)
	{
		m_restartNeeded = true;
		Msg("[AutoRestart] %s: %d player(s) online, will restart once empty or at next map change.\n", reason, numPlayers);
		PrintToChatAll("The server will restart once the server is empty or at the next map change!");
	}
}

void AutoRestartPlugin::OnGameFrame()
{
	double now = Plat_FloatTime();

	if (m_quitPending && now >= m_quitAtTime)
	{
		QuitNow("Deferred quit firing");
		return;
	}

	if (now - m_lastCheckTime < 10.0)
	{
		return;
	}

	m_lastCheckTime = now;
	CheckAndRestart();
}

void AutoRestartPlugin::OnServerActivate()
{
	// The first ServerActivate call is the initial boot map; ignore it so we don't quit immediately.
	// Subsequent calls are map changes.
	m_activateCount++;
	if (m_activateCount <= 1)
	{
		return;
	}

	if (!m_outOfDate)
	{
		m_outOfDate = IsServerOutOfDate();
	}

	if (m_restartNeeded || m_scheduledRestartNeeded || m_outOfDate)
	{
		QuitNow("Restart pending at map change");
	}
}

void AutoRestartPlugin::OnClientDisconnect(edict_t *pEntity)
{
	if (!m_outOfDate)
	{
		m_outOfDate = IsServerOutOfDate();
	}

	if (!(m_restartNeeded || m_scheduledRestartNeeded || m_outOfDate))
	{
		return;
	}

	int leaving = pEntity ? static_cast<int>(pEntity - gpGlobals->pEdicts) : -1;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		if (i != leaving && engine->GetPlayerNetInfo(i) != nullptr)
		{
			return;
		}
	}

	QuitNow("Last player left with restart pending");
}

#ifdef AUTORESTART_KHOOK
KHook::Return<void> AutoRestartPlugin::Hook_GameFrame(IServerGameDLL *, bool simulating)
{
	OnGameFrame();
	return {KHook::Action::Ignore};
}

KHook::Return<void> AutoRestartPlugin::Hook_ServerActivate(IServerGameDLL *, edict_t *pEdictList, int edictCount, int clientMax)
{
	OnServerActivate();
	return {KHook::Action::Ignore};
}

KHook::Return<void> AutoRestartPlugin::Hook_ClientDisconnect(IServerGameClients *, edict_t *pEntity)
{
	OnClientDisconnect(pEntity);
	return {KHook::Action::Ignore};
}

KHook::Return<void> AutoRestartPlugin::Hook_ServerHibernationUpdate(IServerGameDLL *, bool bHibernating)
{
	m_hibernating = bHibernating;
	return {KHook::Action::Ignore};
}
#else
void AutoRestartPlugin::Hook_GameFrame(bool simulating)
{
	OnGameFrame();
	RETURN_META(MRES_IGNORED);
}

void AutoRestartPlugin::Hook_ServerActivate(edict_t *pEdictList, int edictCount, int clientMax)
{
	OnServerActivate();
	RETURN_META(MRES_IGNORED);
}

void AutoRestartPlugin::Hook_ClientDisconnect(edict_t *pEntity)
{
	OnClientDisconnect(pEntity);
	RETURN_META(MRES_IGNORED);
}

void AutoRestartPlugin::Hook_ServerHibernationUpdate(bool bHibernating)
{
	m_hibernating = bHibernating;
	RETURN_META(MRES_IGNORED);
}
#endif

void AutoRestartPlugin::WatcherLoop()
{
	for (;;)
	{
		{
			std::unique_lock<std::mutex> lock(m_watcherMutex);
			m_watcherCv.wait_for(lock, std::chrono::seconds(kWatcherIntervalSeconds), [this] { return m_stopWatcher.load(); });
			if (m_stopWatcher.load())
			{
				return;
			}
		}

		// A hibernating server is empty, so any pending or due restart can happen right away.
		if (m_hibernating && (m_quitPending || m_scheduledRestartNeeded || CheckDailyRestart() || IsOutOfDateSnapshot()))
		{
			Msg("[AutoRestart] Restart due while hibernating, restarting idle server.\n");
			ArmQuitWatchdog();
			kill(getpid(), SIGTERM);
			return;
		}
	}
}
