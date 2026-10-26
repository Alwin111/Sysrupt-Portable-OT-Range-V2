# Installing the SYSRUPT OT Toolkit

## Requirements

- Linux
- Python 3.9 or newer (`python3 --version`)
- No third-party packages for normal use (standard library only)
- `pytest` only if you want to run the test suite

## Option 1 — run in place (no install)

Unzip the archive and run the launcher. It puts the bundled package on `PYTHONPATH` for you and
works from any directory:

```bash
unzip sysrupt-toolkit-1.0.0.zip
cd sysrupt-toolkit-1.0.0
./bin/sysrupt help
```

You can symlink the launcher onto your PATH:

```bash
ln -s "$PWD/bin/sysrupt" ~/.local/bin/sysrupt
sysrupt status
```

## Option 2 — run the module directly

```bash
cd sysrupt-toolkit-1.0.0
PYTHONPATH="$PWD" python3 -m sysrupt help
```

## Option 3 — pip install (editable)

```bash
cd sysrupt-toolkit-1.0.0
python3 -m pip install -e .
sysrupt help
```

## Running the tests

```bash
cd sysrupt-toolkit-1.0.0
python3 -m pip install pytest
python3 -m pytest -q
```

The tests use mocked transports and never touch a real badge.

## Configuration

On first `config set`, a config file is written to `~/.config/sysrupt/config.json`. To use a
project-local config instead, copy `sysrupt.example.json` to `sysrupt.json` in your working
directory, or point `$SYSRUPT_CONFIG` at any path.

## Downloading the toolkit from the badge (authenticated)

The badge serves this archive at `http://10.0.1.1/toolkit.zip`, gated behind the engineer login
(the same session as the Reactor HMI). There is also a "SYSRUPT CLI toolkit" link on the intranet
HOME page.

```bash
# 1. join the badge Wi-Fi (SSID SYSRUPT-OT-V2-xx), then log in to get a session cookie
curl -s -c cookies.txt -d "user=<engineer-id>&pass=<passphrase>" http://10.0.1.1/login -o /dev/null

# 2. download the archive with that session
curl -s -b cookies.txt -OJ http://10.0.1.1/toolkit.zip         # -> sysrupt-toolkit.zip

# 3. (optional) verify integrity: the response carries an X-Toolkit-SHA256 header you can compare
curl -s -b cookies.txt -D - -o sysrupt-toolkit.zip http://10.0.1.1/toolkit.zip | grep -i x-toolkit-sha256
sha256sum sysrupt-toolkit.zip
```

Without a valid session the endpoint returns `401 Unauthorized` (and no challenge data). After
downloading:

```bash
unzip sysrupt-toolkit.zip
cd sysrupt-toolkit-1.0.0
chmod +x bin/sysrupt      # the exec bit is preserved by the badge download; set it if your unzip drops it
./bin/sysrupt status
```

## Platform-specific execution

- **Linux (supported, tested):** use `./bin/sysrupt` or `PYTHONPATH=. python3 -m sysrupt`.
- **Windows / macOS (NOT tested):** the code is pure standard-library Python and has no Linux-only
  imports, so `python3 -m sysrupt ...` is *likely* to work, but this has **not** been verified on
  those platforms — use at your own risk. The `bin/sysrupt` bash launcher is Linux/macOS shell only.

## Connecting to the badge

Join the badge Wi-Fi network (SSID `SYSRUPT-OT-V2-xx`), then confirm reachability:

```bash
sysrupt status
```
