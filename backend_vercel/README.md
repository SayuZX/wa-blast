# Deploy ke Vercel (serverless API + Swagger)

Backend FastAPI yang ada di `backend/` **tidak bisa** berjalan di Vercel karena
bergantung pada `subprocess` yang menjalankan `adb` (Vercel serverless tidak
punya perangkat Android). Karena itu disediakan **backend serverless terpisah**
di `backend_vercel/` yang:

- Menyajikan **REST API + Swagger UI** (`/docs`) via HTTPS,
- Menyimpan `profiles` / `commands` / `logs` ke **Firestore**,
- **Tidak** mengeksekusi ADB — perintah ADB diantrekan sebagai dokumen
  `commands` berstatus `pending`, lalu diambil oleh **worker lokal** di mesin
  yang punya `adb` + perangkat.

## Arsitektur

```
Browser/Dashboard ──HTTPS──▶ Vercel API (/docs = Swagger)
                              │  tulis profiles/commands/logs
                              ▼
                           Firestore  (rules di firestore.rules)
                              ▲
Worker lokal (mesin Anda) ───┘  poll `commands` pending → jalankan ADB → update status
```

## 1. Deploy ke Vercel

### Prasyarat
- Akun Vercel + Vercel CLI (`npm i -g vercel`).
- Project Firebase + service account key.

### Langkah

```bash
cd qa-multi-profile-harness

# 1. Login Vercel
vercel login

# 2. Deploy (buat project baru)
vercel

# 3. Set environment variables (di dashboard Vercel → Settings → Env Vars)
#    QA_API_KEY            = <random key, mis. `openssl rand -hex 32`>
#    FIREBASE_CREDENTIALS_JSON = <isi penuh service-account.json, satu baris>

# 4. Deploy produksi
vercel --prod
```

Setelah deploy, akses:
- **Swagger UI**: `https://<project>.vercel.app/docs`
- **Health**: `https://<project>.vercel.app/health`

> `vercel.json` sudah mengonfigurasi build Python (`@vercel/python`) dan
> `backend_vercel/requirements.txt` berisi `fastapi`, `pydantic`,
> `firebase-admin`.

## 2. Environment variables (Vercel)

| Var | Wajib | Keterangan |
|-----|-------|-----------|
| `QA_API_KEY` | ✅ | API key untuk endpoint mutasi (header `X-API-Key`) |
| `FIREBASE_CREDENTIALS_JSON` | opsional | Full JSON service account (satu baris). Tanpa ini, API jalan mode *memory-only* (data tidak persisten) |
| `FIREBASE_CREDENTIALS_PATH` | opsional | Alternatif: path file (kurang cocok di serverless) |

## 3. Jalankan worker lokal (eksekusi ADB)

Di mesin yang punya `adb` + perangkat Android terhubung:

```bash
cd qa-multi-profile-harness

# Environment: sama seperti Vercel + config ADB lokal
export FIREBASE_CREDENTIALS_JSON='<service account json>'
export QA_ADB_SERIAL=""            # kosong = auto-detect
export QA_TARGET_PACKAGE=com.whatsapp

python -m backend_vercel.worker
```

Worker akan **polling** collection `commands` (default tiap 3 detik, atur via
`QA_WORKER_POLL`), menjalankan perintah ADB (`switch` / `trigger` / `batch`),
lalu mengubah status dokumen menjadi `done` / `failed`.

## 4. Alur endpoint

| Endpoint | Fungsi | ADB? |
|----------|--------|------|
| `GET /health` | Liveness (tanpa auth) | ❌ |
| `GET /profiles` | List profil | ❌ |
| `POST /profile` | Buat profil | ❌ |
| `POST /profile/switch` | **Antre** switch → worker | queue |
| `POST /message/trigger` | **Antre** trigger → worker | queue |
| `POST /message/batch` | **Antre** batch → worker | queue |
| `GET /commands` | Lihat status antrian | ❌ |
| `GET /logs` | Log aktivitas | ❌ |

Semua endpoint kecuali `/health` butuh header `X-API-Key`.
