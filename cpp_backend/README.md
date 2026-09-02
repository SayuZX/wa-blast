# wa_api_server — Monolithic C++ QA Harness

> **⚠️ DISCLAIMER:** Internal QA/DevOps tooling only. For testing on your own
> rooted device in a controlled lab. Not for production, spam, or ToS
> violation. Use at your own risk.

A **single static C++ binary** (C++20, CrowCpp, SQLite) that:

- Runs an HTTP API on port 8080 (configurable).
- Drives the Android device via **ADB** (`popen`) — profile switch, send
  intent, input text, keyevent.
- Blasts messages to many numbers with **preflight check, retry (exponential
  backoff), per-target status, and a file-based mutex lock**.
- Stores state + structured logs in **local SQLite** (no Python/Node/JVM/Firebase).
- Serves **Swagger UI** at `/docs` + `/openapi.yaml`.

---

## 1. Build (host or Android)

### Prerequisites

- CMake ≥ 3.16, a C++20 compiler, Ninja.
- For Android: **Android NDK** (set `ANDROID_NDK_HOME`).
- Internet (CMake fetches header-only deps: Crow, asio, nlohmann/json, sqlite3).

### Host build (for quick testing on PC)

```bash
cd cpp_backend
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/wa_apid config.json
```

### Android ARM64-v8a build (single binary)

```bash
cd cpp_backend
export ANDROID_NDK_HOME=/path/to/android-ndk
./build_android.sh
# Output: build-android/wa_apid
```

---

## 2. Deploy to Android

```bash
adb push build-android/wa_apid /data/local/tmp/wa_apid
adb push config.json /data/local/tmp/config.json
adb shell chmod +x /data/local/tmp/wa_apid

# Run (root recommended for full ADB access)
adb shell "cd /data/local/tmp && su -c './wa_apid config.json'"
```

Or run under **Termux** (copy binary + config, `chmod +x`, run). Port 8080 is
then reachable at `http://<device-ip>:8080`.

---

## 3. Configuration (`config.json`)

```json
{
  "server": { "host": "0.0.0.0", "port": 8080, "api_key": "...", "threads": 4 },
  "adb": { "path": "adb", "serial": "", "target_package": "com.whatsapp", "target_activity": "com.whatsapp.Main" },
  "blast": {
    "delay_between_seconds": 8,
    "max_retries": 3,
    "backoff_seconds": [5, 10, 20],
    "preflight_check": true,
    "lock_file": "/data/local/tmp/.wa_lock",
    "fallback_clipboard": true
  },
  "storage": { "db_path": "./wa_harness.db", "log_retention_days": 7 },
  "profiles": [ { "name": "WA_1", "android_user": 0, "imei": "", "android_id": "", "device_model": "" } ]
}
```

- **api_key**: set a long random value (else `/api/auth` returns 401).
- **delay_between_seconds**: pause between blast targets (rate-limit avoidance).
- **max_retries / backoff_seconds**: retry with exponential backoff.
- **lock_file**: prevents concurrent blasts / profile switches.

---

## 4. API endpoints

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET  | `/api/health` | no | health |
| POST | `/api/auth` | key | verify API key |
| GET  | `/api/profiles` | key | list profiles |
| POST | `/api/profiles/switch` | key | switch profile (ADB `am switch-user`) |
| GET  | `/api/profiles/{id}/status` | key | profile status |
| POST | `/api/message/send` | key | send one message (retry) |
| POST | `/api/message/blast` | key | blast many (queue+retry) |
| GET  | `/api/blast/{job_id}/status` | key | blast job status |
| GET  | `/api/logs` | key | filtered logs |
| GET  | `/api/logs/{id}` | key | single log |
| DELETE | `/api/logs` | key | prune old logs |
| GET  | `/docs` | no | Swagger UI |

Auth: `X-API-Key: <key>` or `Authorization: Bearer <key>`.

---

## 5. Examples (curl)

```bash
K="X-API-Key: mysecret"

# Health
curl http://localhost:8080/api/health

# Send one message
curl -X POST -H "$K" -H "Content-Type: application/json" \
  -d '{"number":"+6281234567890","message":"halo"}' \
  http://localhost:8080/api/message/send

# Blast
curl -X POST -H "$K" -H "Content-Type: application/json" \
  -d '{"targets":["+6281","+6282","+6283"],"message":"blast test"}' \
  http://localhost:8080/api/message/blast
# -> {"job_id":"...","status":"running","total":3}

# Check job
curl -H "$K" http://localhost:8080/api/blast/<job_id>/status

# Logs (filtered)
curl -H "$K" "http://localhost:8080/api/logs?status=FAILED&profile=WA_1"
```

---

## 6. Troubleshooting "send failed"

| Symptom | Cause | Fix |
|---------|-------|-----|
| `WhatsApp not focused / preflight failed` | App not foreground | `ensure_app_foreground` retries `am start`; check `adb shell dumpsys window` |
| `input_text failed` | Long/special text | clipboard fallback auto-enabled (`fallback_clipboard: true`) |
| `ADB timeout` | No device | `adb devices`; set `adb.serial` |
| `another blast is running` (409) | Lock held | wait, or delete `.wa_lock` |
| `Rate limit` | Too fast | raise `delay_between_seconds` |

---

## 7. Titanium Backup / spoofing / ADB

- **Titanium Backup**: back up `/data/data/<pkg>` per profile for restore.
- **Spoofing props** (IMEI/Android ID/model): set per-profile values in
  `config.json` → the app reads them; actual spoofing requires LSPosed module
  (see root `lsposed-module/`).
- **ADB over TCP**: `adb tcpip 5555`, set `adb.serial` to `ip:5555`.

---

## 8. Notes

- Binary is statically linked (libstdc++/libgcc) — no runtime deps.
- SSE endpoint (`/api/logs/stream`) is stubbed; wire a ring-buffer broadcaster
  if real-time dashboard streaming is needed (see `broadcast_sse` in main.cpp).
