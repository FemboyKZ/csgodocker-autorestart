/**
 * AutoRestart - SourceMod extension (CSGO / SM 1.12)
 *
 * Restarts the server when a game/plugin update is detected (via csgodocker's
 * /watchdog version files) or at a configured daily time. Meant for csgodocker.
 */

#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

/* Basic information exposed publicly */
#define SMEXT_CONF_NAME			"AutoRestart"
#define SMEXT_CONF_DESCRIPTION	"Auto restart the server when a game/plugin update is detected (csgodocker)"
#define SMEXT_CONF_VERSION		"1.0.0"
#define SMEXT_CONF_AUTHOR		"jvnipers"
#define SMEXT_CONF_URL			"https://github.com/FemboyKZ/csgodocker-autorestart"
#define SMEXT_CONF_LOGTAG		"AUTORESTART"
#define SMEXT_CONF_LICENSE		"AGPL-3.0"
#define SMEXT_CONF_DATESTRING	__DATE__

/**
 * @brief Exposes plugin's main interface.
 */
#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;

/**
 * @brief Sets whether or not this plugin requires Metamod.
 * We need engine/gamedll interfaces and a LevelShutdown hook, so yes.
 */
#define SMEXT_CONF_METAMOD

/** Enable interfaces we use. */
#define SMEXT_ENABLE_PLAYERHELPERS

#endif // _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
