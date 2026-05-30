# CSGODocker AutoRestart

A native **SourceMod extension** (CS:GO / SourceMod 1.12) that auto-restarts a server when a game or plugin update is detected, or at a configured daily time.

Intended to be used with [CSGODocker](https://github.com/FemboyKZ/csgodocker), will not work on its own.

## Behaviour

Every 10 seconds the extension checks whether a restart is warranted:

- **Out of date** - `/watchdog/csgo/latest.txt` differs from the `build_ver` env var,
  or any `/watchdog/layers/*/latest.txt` changed since the extension loaded.
- **Daily restart** - the current UTC time has passed `daily_restart_time` (once per day).
- **Already scheduled** - a daily restart was previously triggered.

When a restart is warranted:

- If there are **no players**, it runs `quit` immediately.
- Otherwise it prints a chat warning once and waits; it then runs `quit` on the next `LevelShutdown` (map change).

## Configuration

All read from the environment (set by CSGODocker):

| Env var              | Required | Description                                                         |
| -------------------- | -------- | ------------------------------------------------------------------- |
| `build_ver`          | yes      | Current server build version (provided by csgodocker).              |
| `daily_restart_time` | no       | UTC time `HH:mm` (or `HH:mm:ss`) for a daily restart.               |
| `discord_webhook`    | no       | Discord webhook URL; a message posted once per restart decision.    |

## Building

Targets 32-bit (CS:GO is i386). Requires the submodules (SourceMod, Metamod:Source 1.12, HL2SDK-CSGO):

```sh
git submodule update --init --recursive
```

### Docker (recommended)

```sh
docker compose up --build
```

Result (copied into `./output`):

```text
output/addons/sourcemod/extensions/autorestart.ext.so
```

Drop the contents of `output/addons/` into the server's `csgo/addons/` (this is what the CSGODocker `autorestart` layer ships).

### Local (AMBuild)

Needs Python 3, [AMBuild](https://github.com/alliedmodders/ambuild), and a 32-bit
toolchain (`g++-multilib` on Linux, or the x86 MSVC tools on Windows):

```sh
mkdir build && cd build
python3 ../configure.py \
  --targets=x86 \
  --enable-optimize
ambuild
```

The packaged output is in `build/package/addons/sourcemod/extensions/`.
