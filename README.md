# QA Multi-Profile Harness

> **⚠️ DISCLAIMER — READ FIRST**
>
> This project is intended **exclusively** for software development, integration
> testing, and QA automation in a **controlled laboratory environment**, on a
> **device you personally own** and are legally entitled to root and modify.
>
> **Prohibited uses** include — but are not limited to — deploying this system in
> production, sending unsolicited/spam messages, impersonating real users or
> real devices, or any activity that violates the Terms of Service of WhatsApp,
> Telegram, or any other platform. The developer(s) assume **no liability**
> whatsoever for misuse. By using this software you accept full responsibility
> for your actions and for complying with all applicable laws and platform terms.

---

A modular QA/DevOps harness for **isolated multi-profile testing** of a messaging
app on a **rooted Android device**. It provides:

- **Multi-profile management** (1–100 profiles) using a *hybrid* strategy:
  Android multi-user (`user 0`, `user 10`, …) **+** per-profile data snapshots
  of `/data/data/<pkg>` (Titanium-Backup-style isolation).
- **Per-profile device-identity spoofing** (IMEI, Android ID, device model)
  via **LSPosed** + a companion module (XPrivacyLua / ShellDroid style).
- An **HTTP/ADB bridge API** (FastAPI) to automate QA scenarios.
- A **Flutter dashboard** (Material 3 + Fluent, Hugeicons, neutral solid palette).

---

## Table of contents

1. [Architecture](#architecture)
2. [Prerequisites](#prerequisites)
3. [Device setup from zero](#device-setup-from-zero)
4. [Backend setup & run](#backend-setup--run)
5. [Dashboard setup & run](#dashboard-setup--run)
6. [API reference](#api-reference)
7. [LSPosed spoof module](#lsposed-spoof-module)
8. [Directory layout](#directory-layout)
9. [Security & ethics](#security--ethics)

---

## Architecture

```
┌───────────────────────────┐        ┌───────────────────────────────┐
│  Flutter Dashboard (web/  │  HTTP  │  FastAPI Backend (app.py)      │
│  desktop)  — M3 + Fluent  │───────▶│  • auth (X-API-Key)            │
│  Hugeicons, neutral theme │  JSON  │  • structured logging           │
└───────────────────────────┘        └───────────────┬───────────────┘
                                                     │ adb (subprocess)
                                      ┌──────────────▼──────────────┐
                                      │  adb_helper.py               │
                                      │  profile_manager.py (hybrid) │
                                      │  spoof.py (LSPosed config)   │
                                      │  messaging.py (trigger/batch)│
                                      └──────────────▼──────────────┘
                                                     │ USB / TCP
                                      ┌──────────────▼──────────────┐
                                      │  Rooted Android (Magisk)     │
                                      │  • multi-user                │
                                      │  • /data/data snapshots      │
                                      │  • LSPosed spoof module      │
                                      └──────────────────────────────┘
```

- **Profile switching** = snapshot current data → apply spoof → `am switch-user`
  → restore target data → relaunch app.
- **Message trigger** = ADB `ACTION_VIEW` deep-link intent, with a typing
  fallback (`input text` + `keyevent ENTER`).
- **Batch** = sequential triggers with 5–10 s jitter.

---

## Prerequisites

- A **rooted Android device** (Magisk v24+, Android 10+ recommended).
- **ADB** (`platform-tools`) on your host machine.
- **Python 3.10+** for the backend.
- **Flutter SDK 3.3+** for the dashboard (optional — you can drive the API
  with any HTTP client / Swagger UI instead).

---

## Device setup from zero

### 1. Unlock the bootloader

> Exact steps vary by OEM. This **erases the device** — do it on a dedicated
> test device.

1. Enable **Developer options** (tap *Build number* 7× in Settings → About).
2. Enable **OEM unlocking** and **USB debugging**.
3. Reboot to bootloader: `adb reboot bootloader`
4. Unlock: `fastboot oem unlock` (or `fastboot flashing unlock` on newer devices).
5. Reboot and re-enable USB debugging.

### 2. Install Magisk (root)

1. Download the latest **Magisk APK** from the official GitHub releases.
2. Extract the stock **boot.img** for your device's exact firmware.
3. Patch `boot.img` using the Magisk app (*Install → Select and Patch a File*).
4. Flash the patched image: `fastboot flash boot magisk_patched-*.img`
5. Reboot. Open Magisk and confirm root works: `adb shell su -c id` → `uid=0`.

### 3. Enable ADB over USB (and optionally TCP)

```bash
adb devices                 # confirm device serial is listed
adb shell su -c id          # confirm uid=0 (root)
# Optional: wireless ADB for lab racks
adb tcpip 5555
adb connect <device-ip>:5555
```

### 4. Install LSPosed (for identity spoofing)

1. Download **LSPosed** (Zygisk version) from the official GitHub releases.
2. Install via Magisk as a module (*Modules → Install from storage*).
3. Reboot. Open the LSPosed manager and enable the **Zygisk** module.
4. Install **XPrivacyLua** (or your own spoof module — see
   [LSPosed spoof module](#lsposed-spoof-module)) and enable it for the target
   app package.

### 5. Install Titanium Backup (optional, for manual snapshots)

The harness performs its own snapshot/restore via `tar` under root, but you may
also install **Titanium Backup** for manual/offline inspection of app data:

1. Install the Titanium Backup APK.
2. Grant root (Superuser) access.
3. Use it to manually back up `/data/data/<pkg>` before/after tests as a sanity
   cross-check against the harness snapshots.

### 6. Confirm the target app is installed

```bash
adb shell pm list packages | grep com.whatsapp   # or your target package
```

---

## Backend setup & run

```bash
cd qa-multi-profile-harness/backend

# 1. Create a virtualenv and install dependencies
python -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt

# 2. Configure (environment variables, all prefixed QA_)
export QA_API_KEY="$(openssl rand -hex 32)"     # REQUIRED: your API key
export QA_TARGET_PACKAGE="com.whatsapp"          # app under test
export QA_TARGET_ACTIVITY="com.whatsapp.Main"    # launcher activity
export QA_ADB_SERIAL=""                          # empty = single device

# 3. Run
uvicorn backend.app:app --host 0.0.0.0 --port 8000
```

> **Windows / PowerShell note:** on some Windows setups the `python`/`pip3`
> commands are not on PATH — use the `py` launcher and `py -m` instead:
>
> ```powershell
> cd C:\Users\Administrator\qa-multi-profile-harness
> py -m pip install -r backend\requirements.txt
> py -m uvicorn backend.app:app --host 127.0.0.1 --port 8017
> ```

> **Note on import path:** the code uses relative imports (`from .adb_helper ...`).
> Run uvicorn from the **project root** (`qa-multi-profile-harness`) so the
> `backend` package is importable, exactly as shown above.

Interactive API docs (Swagger UI): <http://127.0.0.1:8000/docs>

### Running the test suite

The backend ships a `pytest` suite that runs **without a device** (ADB is faked),
so you can validate the API surface, auth, validation, and core logic on any host:

```bash
cd qa-multi-profile-harness

# Install dev deps (pytest, httpx) once
python -m pip install -r backend/requirements-dev.txt

# Run all tests
python -m pytest backend/tests -v

# Run a single file
python -m pytest backend/tests/test_logic.py -v
```

The suite covers:

- **Auth** — missing/wrong/correct API key (401 vs 200).
- **Validation** — required fields, empty batch, duplicate/unknown profile.
- **Profile lifecycle** — create → auto-generate spoof identity (valid Luhn
  IMEI) → switch marks profile active.
- **Messaging** — trigger requires an active profile; CSV batch upload parsing.
- **Pure logic** — Luhn checksum, Android ID format, CSV/TXT parser edge cases.

### Configuration reference

| Env var               | Default                        | Purpose                              |
|-----------------------|--------------------------------|--------------------------------------|
| `QA_API_KEY`          | `CHANGE_ME_GENERATE_A_RANDOM_KEY` | API key for mutating endpoints  |
| `QA_HOST` / `QA_PORT` | `0.0.0.0` / `8000`             | Server bind                          |
| `QA_ADB_SERIAL`       | `""`                           | Device serial (empty = auto-detect)  |
| `QA_ADB_PATH`         | `adb`                          | Path to adb binary                   |
| `QA_TARGET_PACKAGE`   | `com.whatsapp`                 | App package under test               |
| `QA_TARGET_ACTIVITY`  | `com.whatsapp.Main`            | Launcher activity                    |
| `QA_MAX_PROFILES`     | `100`                          | Profile limit                        |
| `QA_SNAPSHOT_ROOT`    | `./data/profiles`              | Host dir for snapshots + registry    |
| `QA_DEVICE_STAGING_DIR`| `/data/local/tmp/qa_harness`  | Device-side staging                  |
| `QA_LSPOSED_MODULE_PKG`| `com.example.spoofmodule`     | LSPosed module package               |
| `QA_SPOOF_CONFIG_PATH` | `/data/local/tmp/qa_harness/spoof.json` | Module config path     |
| `QA_BATCH_DELAY_MIN/MAX`| `5.0` / `10.0`               | Inter-message batch delay (s)        |
| `QA_LOG_LEVEL`        | `INFO`                         | Log verbosity                        |

---

## Dashboard setup & run

```bash
cd qa-multi-profile-harness/dashboard
flutter pub get

# Web (serves on a dev port; set backend URL in Settings screen)
flutter run -d chrome

# Desktop (Windows/macOS/Linux if desktop support enabled)
flutter run -d windows

# Android APK
flutter build apk --debug
```

### Android toolchain troubleshooting

If `flutter build apk` fails with an `sdkmanager` crash
(`Process 'sdkmanager.bat' finished with non-zero exit value ... 0xC0000409`),
the cause is usually a **JDK 25 + cmdline-tools 23.0** incompatibility plus a
missing NDK that AGP tries to auto-download. Fixes already applied to this repo:

1. **Install JDK 17** (AGP/Gradle are not stable on JDK 25):
   ```powershell
   # Download Temurin JDK 17 and extract, then:
   flutter config --jdk-dir="C:\path\to\jdk-17"
   ```
   This repo also pins `org.gradle.java.home` in `android/gradle.properties`.

2. **Pin `compileSdk` and `ndkVersion`** in `android/app/build.gradle.kts` to
   the versions actually installed in your SDK (this repo uses `compileSdk = 37`
   and `ndkVersion = "30.0.16138531"`). This stops AGP from invoking
   `sdkmanager` to download missing components.

To check what's installed:
```powershell
ls "$env:LOCALAPPDATA\Android\sdk\platforms"   # compileSdk options
ls "$env:LOCALAPPDATA\Android\sdk\ndk"         # ndkVersion options
```

> A quick alternative that avoids the Android toolchain entirely: build for
> **web** (`flutter build web`) — the dashboard compiles cleanly there with no
> Android SDK/JDK required.

On first launch, open **Settings** in the dashboard and enter:
- **Backend URL** (e.g. `http://<host-ip>:8000` — use your host's LAN IP when
  running the dashboard on a different machine/device).
- **API Key** (the `QA_API_KEY` you set).

The dashboard exposes four sections:

| Tab       | Capability                                                          |
|-----------|---------------------------------------------------------------------|
| Profiles  | List WA_1..WA_N, active/inactive status, switch (with confirm dialog), create profile |
| Send      | Manual trigger form (number + message) + CSV/TXT batch upload       |
| Logs      | Real-time structured activity log (4 s poll)                        |
| Settings  | Backend URL + API key, dark/light mode toggle                       |

### Design-system compliance

- **Material 3** foundation with **Fluent Design** accents (rounded corners,
  clear elevation, flat solid surfaces).
- **Hugeicons** used exclusively for every icon.
- **Neutral monochromatic palette** (black/white/grayscale). Accent colour is
  *only* used for status indicators: dark gray = active, light gray = inactive.
- **No vivid colours** and **no transparency/glassmorphism** — every surface is
  fully opaque (`opacity = 1`).
- **8dp grid** spacing, **M3 typography** (Headline/Title/Body/Label).
- **Light and dark mode** both supported with neutral palettes.

---

## API reference

All mutating endpoints require the `X-API-Key` header.

### `GET /health` (unauthenticated)

```json
{ "status": "ok", "adb_state": "device", "rooted": true,
  "profiles": 3, "active": "WA_2" }
```

### `GET /profiles`

```json
{ "active": "WA_2",
  "profiles": [
    { "name": "WA_1", "android_user": 0, "status": "inactive", "spoof": {...} },
    { "name": "WA_2", "android_user": 10, "status": "active", "spoof": {...} }
  ] }
```

### `POST /profile` — create

```json
{ "name": "WA_3", "spoof": { "imei": "356938035643809", "android_id": "..." } }
```

### `POST /profile/switch`

```json
{ "profile": "WA_2", "snapshot_current": true }
```

### `POST /message/trigger`

```json
{ "number": "+6281234567890", "message": "hello", "method": "auto" }
```

### `POST /message/batch`

```json
{ "items": [ {"number":"+628...", "message":"a"},
             {"number":"+628...", "message":"b"} ],
  "delay_min": 5, "delay_max": 10 }
```

### `POST /message/batch/upload` (multipart)

Field `file` (CSV/TXT). CSV: `number,message` per line. TXT: `number<TAB>message`.

### `GET /logs?lines=100`

Returns the most recent JSON-lines log entries.

---

## LSPosed spoof module

The backend writes a JSON config to `QA_SPOOF_CONFIG_PATH`
(`/data/local/tmp/qa_harness/spoof.json`) on each profile switch:

```json
{
  "profile": "WA_2",
  "package": "com.whatsapp",
  "identity": {
    "imei": "356938035643809",
    "android_id": "a1b2c3d4e5f60718",
    "device_model": "QA-Device-1234",
    "manufacturer": "QALab",
    "serial": "...",
    "mac": "aa:bb:cc:dd:ee:ff"
  },
  "generated_at": "2026-01-01T00:00:00Z"
}
```

Your **LSPosed module** (a companion project, `com.example.spoofmodule` by
default) must:

1. Hook `Build`/`TelephonyManager`/`Settings.Secure` getters in the target app.
2. Read this JSON file on process start (or on receiving the
   `com.example.spoofmodule.RELOAD_SPOOF` broadcast).
3. Return the per-profile values instead of the real hardware values.

A minimal XPrivacyLua-style hook skeleton is provided in
[`lsposed-module/`](lsposed-module/). **This repository intentionally ships the
config/contract and integration, not a full production hook** — you adapt it to
your exact target app and Android version.

> ⚠️ **Ethics note:** spoofing here is *synthetic*, randomly-generated test data
> used to observe the app's response to *environment variation* — it must never
> be used to impersonate a real, identifiable device or person.

---

## Directory layout

```
qa-multi-profile-harness/
├── README.md
├── backend/
│   ├── __init__.py
│   ├── app.py              # FastAPI app + endpoints + auth
│   ├── config.py           # settings (env-driven)
│   ├── adb_helper.py       # ADB execution layer
│   ├── profile_manager.py  # hybrid multi-user + snapshots
│   ├── spoof.py            # LSPosed identity config
│   ├── messaging.py        # trigger / batch / upload parsing
│   ├── logging_setup.py    # structured logging
│   └── requirements.txt
├── dashboard/
│   ├── pubspec.yaml
│   └── lib/
│       ├── main.dart
│       ├── theme.dart          # neutral M3/Fluent design system
│       ├── api_client.dart
│       ├── state.dart
│       ├── models.dart
│       └── screens/
│           ├── home_screen.dart
│           ├── profiles_screen.dart
│           ├── send_screen.dart
│           ├── logs_screen.dart
│           └── settings_screen.dart
└── lsposed-module/         # (companion) spoof hook skeleton
    └── README.md
```

---

## Security & ethics

- **Never expose this API** beyond your lab network; it drives ADB on a rooted
  device. Bind to `127.0.0.1` or use a VPN/firewall for remote access.
- **Rotate `QA_API_KEY`** per environment and never commit it.
- **Logs** are written as JSON-lines under `data/logs/`; rotate them and scrub
  any real phone numbers before sharing.
- This tool **does not** bypass encryption, retrieve other users' data, or
  interact with production services. It operates only on your own test device
  and your own test accounts.

---

*Built for internal QA/DevOps. Use responsibly.*

---

## Vercel deployment (serverless API + Swagger)

See [`backend_vercel/README.md`](backend_vercel/README.md) for the full guide.

The local backend (`backend/`) drives ADB directly and runs on your machine.
For a **public HTTPS API + Swagger** hosted on Vercel, use the serverless
backend in `backend_vercel/` — it stores data in Firestore and **queues** ADB
commands for a local worker (`python -m backend_vercel.worker`) instead of
running ADB itself (Vercel has no Android device).
