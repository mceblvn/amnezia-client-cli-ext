# AmneziaVPN CLI-ext (5.x)

Fork of [amnezia-vpn/amnezia-client](https://github.com/amnezia-vpn/amnezia-client)
with a **native CLI baked into the same GUI binary**: `status`, `servers`,
`connect`, `disconnect`, `watch` — plus `--json` and `--redact`. Built for
scripted control: status bars, widgets, hotkeys, automation.

> Upstream readme and docs: [amnezia-vpn/amnezia-client](https://github.com/amnezia-vpn/amnezia-client#readme),
> [docs.amnezia.org](https://docs.amnezia.org).

## Base

- Upstream **5.1.0.6**, branch `cli-5x`, cut from `dev`.
- **Linux only.** Built, packaged and tested on Linux. The CLI is not built
  or packaged for macOS and Windows here, and `deploy/build_cli_package.sh`
  is Linux-only.
- Changed vs upstream: CLI plumbing only — `client/cli/` (IPC protocol and
  status collector), IPC server plus option parsing in `amneziaApplication`,
  `cli*` entry points in `coreController`, a status snapshot in
  `vpnConnection`, and the packaging wrapper. VPN protocols, the privileged
  service, the daemon and the UI are untouched upstream.
- Reports whatever the client knows: **AmneziaWG 3.1** (and 1.5/2),
  WireGuard, XRay/VLESS, OpenVPN, native configs.

## CLI

```bash
AmneziaVPN status [--json] [--redact]      # what is connected right now
AmneziaVPN servers [--json]                # configured servers, GUI order
AmneziaVPN connect [index] [--json]        # default server, or the Nth
AmneziaVPN disconnect [--json]             # idempotent
AmneziaVPN watch [--json] [--redact]       # one JSON line per state change
```

`status`:

```
connected: yes
  device:  10.13.13.2
  gateway: 203.0.113.7
  rx/tx:   12450816 / 1048576 bytes
  since:   Mon Nov 10 09:14:22 2026
```

`servers --json`:

```json
{
    "servers": [
        {
            "index": 1,
            "name": "Amnezia Server",
            "protocol": "AmneziaWG (version 3.1)",
            "protoShort": "AWG 3.1",
            "default": true,
            "current": true
        }
    ]
}
```

## How it talks

- The CLI process sends one line-delimited JSON request to the running GUI
  over `QLocalSocket` (`AmneziaVPNInstance`) and exits: 0 ok, 1 failure,
  2 usage.
- `connect` and `disconnect` confirm *acceptance*, not the result — connect
  is asynchronous. Poll `status` or keep `watch` open.
- `--redact` masks IPs (`1.2.3.4` → `1.2.*.*`) in `device` and `gateway`.
- No GUI running? `status` falls back to the privileged daemon, so a tunnel
  that outlived the window still shows up — without server name and
  protocol, the daemon does not know them.

## Building

Requirements: Qt 6.10.1 from aqtinstall with `qtremoteobjects qt5compat
qtshadertools qthttpserver qtwebsockets`, Qt Installer Framework, Conan 2.34,
CMake 3.25+, Ninja, GCC.

```bash
git clone --recursive https://github.com/mceblvn/amnezia-client-cli-ext.git
cd amnezia-client-cli-ext
cp deploy/.cli-env.example deploy/.cli-env     # fill in the gateway keys
deploy/build_cli_package.sh --generator Ninja
```

→ `deploy/build/AmneziaVPN_5.1.0.6_linux_x64.run`. Add `--suffix +cli.1` to
tag the package name.

The script is a thin wrapper over `deploy/build.sh --installer all`: it
sources `deploy/.cli-env`, refuses to build with empty keys, and verifies the
keys ended up in the binary. `deploy/build.sh` finds Qt and QtIFW in the
aqtinstall layout (`~/Qt`, or `QT_INSTALL_DIR`/`QT_ROOT_PATH`/`QIF_ROOT_PATH`).

Use the aqt Qt, not the distro one: Qt's deploy on a distro Qt copies an
incomplete plugin set, drags `libc` and `libstdc++` into the bundle, and dies
on RPATH.

## Premium keys

`deploy/.cli-env` (gitignored) holds the gateway keys and mirror endpoints.
Without them everything works except Premium API calls. Key format is
strict: one line, literal `\n`, no trailing newline — the PEM bytes are
hashed into the AES key for proxy payloads, one extra byte breaks the whole
path.

## Install layout

```
/opt/AmneziaVPN/bin/AmneziaVPN            # client + CLI
/opt/AmneziaVPN/bin/AmneziaVPN-service    # privileged service
/opt/AmneziaVPN/{lib,plugins,qml}         # bundled Qt
~/.config/AmneziaVPN.ORG/AmneziaVPN.conf  # config, kept across installs
```

## License

GNU General Public License v3.0 (see LICENSE). This fork is not affiliated
with or endorsed by the Amnezia project.
