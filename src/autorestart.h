/**
 * AutoRestart - Metamod:Source plugin (CS:GO, MM:S 1.12 and 2.0)
 *
 * Restarts the server when a game/plugin update is detected
 * (via csgodocker's /watchdog version files) or at a configured daily time.
 * Meant to be used with csgodocker.
 */

#pragma once

#include <ISmmPlugin.h>
#include <eiface.h>

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <thread>

// MM:S 2.0 (plugin API 18) replaced SourceHook with KHook; 1.12 is API 16.
#if METAMOD_PLAPI_VERSION >= 18
#define AUTORESTART_KHOOK
#endif

class AutoRestartPlugin : public ISmmPlugin
{
public:
	AutoRestartPlugin();

	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
	bool Unload(char *error, size_t maxlen) override;

public: // hooks
#ifdef AUTORESTART_KHOOK
	KHook::Return<void> Hook_GameFrame(IServerGameDLL *, bool simulating);
	KHook::Return<void> Hook_ServerActivate(IServerGameDLL *, edict_t *pEdictList, int edictCount, int clientMax);
	KHook::Return<void> Hook_ClientDisconnect(IServerGameClients *, edict_t *pEntity);
	KHook::Return<void> Hook_ServerHibernationUpdate(IServerGameDLL *, bool bHibernating);
#else
	void Hook_GameFrame(bool simulating);
	void Hook_ServerActivate(edict_t *pEdictList, int edictCount, int clientMax);
	void Hook_ClientDisconnect(edict_t *pEntity);
	void Hook_ServerHibernationUpdate(bool bHibernating);
#endif

public: // ISmmPlugin metadata
	const char *GetAuthor() override
	{
		return "jvnipers";
	}

	const char *GetName() override
	{
		return "Auto Restart";
	}

	const char *GetDescription() override
	{
		return "Auto restart the server when a game/plugin update is detected, or at a configured daily time. Meant to be used with csgodocker.";
	}

	const char *GetURL() override
	{
		return "https://github.com/FemboyKZ/csgodocker-autorestart";
	}

	const char *GetLicense() override
	{
		return "AGPL-3.0";
	}

	const char *GetVersion() override
	{
		return "2.0.0";
	}

	const char *GetDate() override
	{
		return __DATE__;
	}

	const char *GetLogTag() override
	{
		return "AutoRestart";
	}

private:
	// Engine-agnostic hook bodies, called from the KHook/SourceHook wrappers.
	void OnGameFrame();
	void OnServerActivate();
	void OnClientDisconnect(edict_t *pEntity);

	void CheckAndRestart();
	bool IsServerOutOfDate();
	bool CheckDailyRestart() const;
	int CountHumanPlayers() const;
	void PrintToChatAll(const char *msg);
	void QuitNow(const char *why);

	std::map<std::string, std::string> ReadPluginVersions() const;

	// True if the version file at path is unchanged (by mtime) since last checked;
	// records the current mtime as a side effect.
	// Lets IsServerOutOfDate() skip re-reading files that haven't moved.
	bool VersionFileUnchanged(const std::string &path);

	// While the server hibernates GameFrame is frozen, so the normal restart path can't run.
	// This thread polls for a pending/due restart and, while hibernating,
	// signals the process to shut down for relaunch. It never calls into the engine.
	void WatcherLoop();

	// Thread-safe out-of-date check:
	// reads only the immutable startup snapshot (m_buildVersion, m_pluginVersions) plus the files, never the mtime cache.
	bool IsOutOfDateSnapshot() const;

	std::string m_buildVersion;
	std::map<std::string, std::string> m_pluginVersions; // snapshot taken at load

	std::string m_discordWebhook;   // optional Discord webhook URL (env: discord_webhook)
	std::string m_serverName;       // optional server name for notifications (env: server_name)
	bool m_discordNotified = false; // ensures we post to Discord only once per restart decision

	bool m_restartNeeded = false;
	std::atomic<bool> m_scheduledRestartNeeded {false};

	bool m_outOfDate = false;
	double m_lastVersionCheckTime = 0.0;             // Plat_FloatTime() of last version-file poll
	std::map<std::string, timespec> m_versionMtimes; // path -> last-seen mtime

	bool m_hasDailyRestart = false;
	int m_dailyRestartSeconds = 0;               // seconds since UTC midnight
	std::atomic<int> m_lastDailyRestartDay {-1}; // days since unix epoch (UTC) of last daily restart

	double m_lastCheckTime = 0.0; // Plat_FloatTime() of last 10s tick
	int m_activateCount = 0;      // number of ServerActivate calls seen (first == initial boot map)

	// Empty-server quit is delayed so the async Discord webhook has time to flush.
	std::atomic<bool> m_quitPending {false};
	double m_quitAtTime = 0.0; // Plat_FloatTime() at which to issue the deferred quit

	// Background watcher state. m_hibernating is the engine's hibernation signal (set from Hook_ServerHibernationUpdate);
	// the thread only acts while it's true.
	std::atomic<bool> m_hibernating {false};
	std::atomic<bool> m_stopWatcher {false};
	std::thread m_watcherThread;
	std::mutex m_watcherMutex;
	std::condition_variable m_watcherCv;

#ifdef AUTORESTART_KHOOK
	KHook::Virtual<IServerGameDLL, void, bool> m_GameFrame;
	KHook::Virtual<IServerGameDLL, void, edict_t *, int, int> m_ServerActivate;
	KHook::Virtual<IServerGameClients, void, edict_t *> m_ClientDisconnect;
	KHook::Virtual<IServerGameDLL, void, bool> m_ServerHibernationUpdate;
#endif
};

extern AutoRestartPlugin g_AutoRestartPlugin;

PLUGIN_GLOBALVARS();
