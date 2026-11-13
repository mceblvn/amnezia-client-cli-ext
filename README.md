# AmneziaVPN CLI-ext

Fork of [amnezia-vpn/amnezia-client](https://github.com/amnezia-vpn/amnezia-client)
with a native CLI in the GUI binary.

## CLI

```bash
AmneziaVPN status [--json] [--redact]
AmneziaVPN servers [--json]
AmneziaVPN connect [index] [--json]
AmneziaVPN disconnect [--json]
AmneziaVPN watch [--json] [--redact]
```

macOS: `/Applications/AmneziaVPN.app/Contents/MacOS/AmneziaVPN status`

The CLI process sends a line-delimited JSON request to the running GUI over
`QLocalSocket` (`AmneziaVPNInstance`) and exits: 0 on success, 1 on failure.
`connect` and `disconnect` confirm acceptance, not the result — poll `status`
or use `watch`. `--redact` masks IPs (`1.2.3.4` → `1.2.*.*`).

## Building

Qt 6.10+ (qtremoteobjects, qt5compat, qtshadertools), Qt Installer Framework
and Conan, in the aqtinstall layout (`~/Qt` or `QT_INSTALL_DIR`).

```bash
git clone --recursive https://github.com/mceblvn/amnezia-client-cli-ext.git
cd amnezia-client-cli-ext
cp deploy/.cli-env.example deploy/.cli-env
deploy/build_cli_package.sh --generator Ninja
```

The package lands in `deploy/build/AmneziaVPN_<version>_linux_x64.run`.
Add `--suffix +cli.1` to tag the build.

## Premium keys

`deploy/.cli-env` holds the gateway keys and is gitignored. Without it the
build works except for Premium API calls. `deploy/build_cli_package.sh`
refuses to package with empty keys and verifies the keys in the binary.

## License

GNU General Public License v3.0, see LICENSE. This fork is not affiliated with
the Amnezia project.
