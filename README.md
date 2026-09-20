# CSGODocker AutoRestart

A **Metamod:Source plugin** (CS:GO, MM:S 1.12 and 2.0) that auto-restarts a server when a game or plugin update is detected, or at a configured daily time.

Intended to be used with [csgodocker](https://github.com/FemboyKZ/csgodocker), will not work on its own.
The logic is shared with [cs2docker-autorestart](https://github.com/FemboyKZ/cs2docker-autorestart).

## Behaviour

Every 10 seconds (version files at most every 60 seconds) the plugin checks whether a restart is warranted:

- **Out of date** - `/watchdog/csgo/latest.txt` differs from the `build_ver` env var,
  or any `/watchdog/layers/*/latest.txt` changed since the plugin loaded.
- **Daily restart** - the current UTC time has passed `daily_restart_time` (once per day).

When a restart is warranted:

- If there are **no players**, it runs `quit` (after 5 seconds if a Discord webhook is set, so the message gets out).
- Otherwise it prints a chat warning once, then quits when the last player leaves or at the next map change.
- While the server **hibernates** (`sv_hibernate_when_empty`), `GameFrame` stops running,
  so a background thread polls every 30 seconds and sends the process `SIGTERM` if a restart is pending or due.
- If shutdown hangs for 60 seconds, the process is force-exited so csgodocker relaunches it.

## Configuration

All read from the environment (set by CSGODocker):

| Env var              | Required | Description                                                        |
| -------------------- | -------- | ------------------------------------------------------------------ |
| `build_ver`          | yes      | Current server build version (provided by csgodocker).             |
| `daily_restart_time` | no       | UTC time `HH:mm` (or `HH:mm:ss`) for a daily restart.              |
| `discord_webhook`    | no       | Discord webhook URL; an embed is posted once per restart decision. |
| `server_name`        | no       | Title for the Discord embed.                                       |

## Building

MM:S 2.0 replaced SourceHook with KHook, so there is one build per Metamod:Source version.
The source is shared; `METAMOD_PLAPI_VERSION` picks the hook API. Linux x86 only.

```sh
git submodule update --init --recursive
docker compose up --build
```

Result:

```text
output/mm-1.12/addons/autorestart/bin/autorestart.so
output/mm-1.12/addons/metamod/autorestart.vdf
output/mm-2.0/addons/autorestart/bin/autorestart.so
output/mm-2.0/addons/metamod/autorestart.vdf
```

Use the package matching the server's Metamod:Source version and drop its `addons/` into `csgo/addons/`.
