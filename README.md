# AmneziaVPN CLI-ext (4.x)

Fork of [amnezia-vpn/amnezia-client](https://github.com/amnezia-vpn/amnezia-client)
with a **native CLI** baked into the same binary: `status`, `servers`,
`connect`, `disconnect`, `watch` (+ `--json`, `--redact`). Built for
scripted control (bars, widgets, automation)

> Original upstream readme: [README.upstream.md](README.upstream.md).
> Upstream docs, website, translations: see the
> [original README](https://github.com/amnezia-vpn/amnezia-client#readme)
> and [docs.amnezia.org](https://docs.amnezia.org).

## Base

- Upstream: **4.8.21.0** at commit
  [`72ee32a`](https://github.com/amnezia-vpn/amnezia-client/commit/72ee32af)
  (branch `cli-backport-4x` here) — deliberately *not* latest dev: this
  tree predates the AWG3 stack rework and matches the last known-good
  release for AWG 1, 1.5, 2 and VLESS.
- Changed vs upstream: CLI plumbing only (`client/cli/`, IPC
  client/server in `amnezia_application`, `cli*` entry points in
  `coreController`, idempotent `connectExplicit`, per-protocol status
  snapshot, packaging script + docs). VPN protocols, privileged service,
  daemon and UI are untouched upstream 4.8.21.0.
- Platform scope: Linux-first. Everything else should compile (same Qt
  APIs), but the CLI is only tested on Linux.

## CLI contract

```bash
AmneziaVPN status [--json] [--redact]      # connection status
AmneziaVPN servers [--json]                # [{index,name,protocol,protoShort,default,current}]
AmneziaVPN connect [index] [--json]        # default server or Nth; idempotent, switches cleanly
AmneziaVPN disconnect [--json]             # idempotent no-op when already down
AmneziaVPN watch [--json] [--redact]       # stream: one JSON object per line on every state change
```

- Commands go to the running GUI instance over `QLocalSocket`
  (`AmneziaVPNInstance`) as line-delimited JSON; the CLI process exits
  immediately with a code (0 ok / 1 failure / 2 usage).
- `connect`/`disconnect` replies mean *accepted*, not *connected*
  (connect is async; poll `status` or `watch` for the result).
- `--redact` masks IPs (`1.2.3.4 → 1.2.*.*`) in `device`/`gateway`.
- `status` with no GUI running falls back to the WireGuard daemon socket.

## Building

Linux-first (other desktop OSes should compile — same Qt APIs — but the
CLI is only tested on Linux; Android/iOS/macOS-NE IPC paths are excluded
by the same guards as upstream).

Requirements: CMake 3.25+, Ninja, GCC, Qt 6.10+ (Core Gui Network Xml
RemoteObjects Quick Svg QuickControls2 Core5Compat Concurrent Widgets),
7z, patchelf, curl, python3.

```bash
git clone --recursive git@github.com:<you>/amnezia-client-cli-ext.git
cd amnezia-client-cli-ext
git checkout cli-backport-4x            # the CLI product branch
cp deploy/.cli-env.example deploy/.cli-env
# ... fill in .cli-env, see below ...
deploy/build_cli_package.sh [--suffix +cli.1]
# → AmneziaVPN_4.8.21.0+cli.1_linux_x64.run
sudo ./AmneziaVPN_4.8.21.0+cli.1_linux_x64.run
```

## Secrets (Premium API)

Public CI / default builds ship **without** Premium gateway keys: everything
except Premium API calls works. For a Premium-capable build, fill
`deploy/.cli-env` (gitignored). The script **refuses** to
build Premium-capable binaries with empty keys (fail-closed), and verifies
the baked keys in the resulting binary.

Key format (critical): single line with literal `\n`, no trailing newline —
sha512 of the PEM bytes derives the proxy-payload AES key, one extra byte
breaks all gateway crypto. Get the values from
`amnezia-client-lite/macos-signed-build.sh` (PROD_\*/DEV_\* vars) or extract
them from an official binary with `strings`.

## Layout (4.x package)

```
/opt/AmneziaVPN/client/bin/AmneziaVPN      # client + CLI
/opt/AmneziaVPN/service/...               # privileged service
/usr/local/{sbin,bin}/AmneziaVPN          # wrapper symlinks (pass "$@")
~/.config/AmneziaVPN.ORG/AmneziaVPN.conf  # config (installer keeps it)
```

The wrapper also appends system Qt plugin dirs after the bundled one, so
native KDE file dialogs find their KIO workers on any distro (Arch, Debian,
Fedora layouts probed at launch). macOS/Windows packaging untouched.

## License

GNU GPL v3.0, same as upstream (see LICENSE). This fork is not affiliated
with or endorsed by the Amnezia project.
