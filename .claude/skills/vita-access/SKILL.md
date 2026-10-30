---
name: vita-access
description: Use whenever you need to put a build on the test Vita, pull its testing log, or close/launch the app on it. Covers the VITA_IP setting, tools/vita.sh, and the vitacompanion FTP and command ports.
---

# Reaching the test Vita

The test Vita runs [vitacompanion](https://github.com/devnoname120/vitacompanion) (README is the source for everything below).

## Rules first

- The CEO may be using the Vita. Never launch, quit, reboot, screen-off or delete anything on it unless that is needed for the test at hand and you can say why. Uploading a file to `ux0:/vpk/` is harmless; the CEO installs it himself from VitaShell.
- Testing logs are seen as binary by BSD grep. Always `grep -a`, or a missing match proves nothing.

## Address

`VITA_IP` must be set (`export VITA_IP=<address>`). The address is not stored in the repo. If it is missing, or the Vita does not answer within 3 seconds, `tools/vita.sh` exits non-zero with a message saying so. The Vita must be awake with vitacompanion loaded.

## Ports

- FTP: `1337`. Paths look like `ux0:/somedir/file`.
- Commands: `1338`, plain TCP, one command per line (`echo reboot | nc <ip> 1338`). Several commands can be chained with `;`.

## The helper: `tools/vita.sh`

```
tools/vita.sh upload <local.vpk> [remote-name]   # to ux0:/vpk/<remote-name>
tools/vita.sh ls [dir]                           # list ux0:/<dir>, default vpk
tools/vita.sh log <local-path>                   # pull ux0:/data/vita-chiaki/vitarps5-testing.log
tools/vita.sh cmd <command...>                   # send to the command port
```

Give VPKs a name that says what they test (e.g. `VitaRPS5-468-two-finger-hold.vpk`) so the CEO can tell them apart. After `upload`, `ls` and compare the size with `wc -c` on the local file. Then tell the CEO the name to install.

Pull the log with `tools/vita.sh log <scratchpad>/vita.log`, then `grep -a` / `tail -a` it. The log only exists after a build from `./tools/build.sh --env testing` has run on the Vita.

`./tools/build.sh deploy <vita_ip>` also uploads over FTP 1337, but it puts `build/vitarps5.vpk` in `ux0:/` (not `ux0:/vpk/`) and still prints a VitaShell message. Prefer `tools/vita.sh upload`.

## vitacompanion commands (only those the README lists)

| Command | Arguments | Effect |
|---|---|---|
| `help` | none | help screen |
| `launch` | `<TITLEID>` | launch an app |
| `quit` | `<TITLEID>` or `all` | quit an app, or all running apps |
| `reboot` | none | reboot |
| `screen` | `on` / `off` | screen on or off |
| `nosleep` | `on` / `off` / `status` | suspend prevention (on by default) |
| `version` | none | module version |
| `promote` | `<directory>` | promote an extracted app directory (not a VPK) |
| `press` / `release` | button, stick or touch target | synthetic input |
| `wait` | `<n>ms` or `<n>s` | pause between chained commands |

Our title ID is `VRPS50001` (`VITA_TITLEID` in `vita/CMakeLists.txt`). Close and relaunch:
`tools/vita.sh cmd "quit VRPS50001"` then `tools/vita.sh cmd "launch VRPS50001"` (only with a reason, see rules).

## Can an installed app be updated by uploading only `eboot.bin`?

Not documented. The README lists no way to update or replace an installed app; the only install-related command is `promote`, which takes an extracted app directory and "does not extract or install VPK files". The FTP server accepts uploads to any path, so putting `eboot.bin` into `ux0:app/VRPS50001/` may work in practice, but nothing in the README says so, and it has not been verified here. Treat the VPK upload plus the CEO installing it as the supported route.
