# Menghubungkan Dashboard ↔ Vercel API ↔ Worker Lokal

Panduan end-to-end untuk menghubungkan tiga komponen:

```
Dashboard Flutter ──▶ Vercel API (HTTPS + Swagger) ──▶ Firestore ──▶ Worker lokal (ADB)
```

---

## Langkah 1 — Push kode terbaru ke GitHub

Commit terbaru sudah berisi perbaikan dashboard agar kompatibel dengan Vercel.
Push branch `main` lokal ke GitHub:

```powershell
cd C:\Users\Administrator\qa-multi-profile-harness
git push origin main:granular-history --force
```

> `main` lokal sudah di-rewrite jadi 28 commit granular. Kalau branch
> `granular-history` di GitHub sudah ada, pakai `--force` untuk menimpanya.

---

## Langkah 2 — Set env vars di Vercel

Buka **vercel.com → Project `wa-blast` → Settings → Environment Variables**,
lalu tambahkan:

| Name | Value | Keterangan |
|------|-------|-----------|
| `QA_API_KEY` | `<random 32-char>` | wajib — untuk auth endpoint |
| `FIREBASE_CREDENTIALS_JSON` | `<service account JSON, SATU BARIS>` | untuk persistensi Firestore |

### Cara generate `QA_API_KEY`:

```powershell
py -c "import secrets; print(secrets.token_urlsafe(32))"
```

### Cara dapatkan `FIREBASE_CREDENTIALS_JSON`:

1. Buka **Firebase Console → project `rayhan-ba768` → Project settings → Service accounts**.
2. Klik **Generate new private key** → download JSON.
3. Buka file, **hapus semua newline** (jadikan satu baris), lalu tempel ke Vercel.
   - PowerShell:
     ```powershell
     $j = Get-Content service-account.json -Raw
     $j = $j -replace "\r?\n", ""
     $j
     # copy output, paste ke Vercel
     ```

Setelah set env vars, **Redeploy** (Vercel → Deployments → ⋯ → Redeploy).

Verifikasi mode sudah `firestore` (bukan `memory`):

```powershell
curl https://wa-blast-id.vercel.app/health
# harus: {"status":"ok","mode":"firestore","backend":"vercel",...}
```

---

## Langkah 3 — Jalankan worker lokal (eksekusi ADB)

Di mesin yang punya **adb + perangkat Android ber-root** terhubung:

```powershell
cd C:\Users\Administrator\qa-multi-profile-harness

# 1. Pastikan dependensi terpasang
py -m pip install -r backend_vercel\requirements.txt
py -m pip install -r backend\requirements.txt

# 2. Set environment
$env:FIREBASE_CREDENTIALS_JSON = '<service account json SATU BARIS>'
$env:QA_ADB_SERIAL = ""            # kosong = auto-detect
$env:QA_TARGET_PACKAGE = "com.whatsapp"

# 3. Jalankan worker
py -m backend_vercel.worker
```

Worker akan tampil log seperti:
```
Worker started. Polling every 3.0s
Processing command <id> (type=switch)
Processing command <id> (type=trigger)
```

> **Alternatif cepat:** edit `run_worker.bat` (isi `FIREBASE_CREDENTIALS_JSON`),
> lalu jalankan `run_worker.bat`.

---

## Langkah 4 — Hubungkan dashboard ke Vercel

1. Buka aplikasi dashboard (APK atau web).
2. Tab **Settings**:
   - **Backend URL** = `https://wa-blast-id.vercel.app`
   - **API Key** = nilai `QA_API_KEY` (sama dengan di Vercel)
3. **Save settings**.

---

## Langkah 5 — Uji end-to-end

Gunakan `test_api.bat`:

```powershell
test_api.bat https://wa-blast-id.vercel.app <QA_API_KEY>
```

Atau manual:

```powershell
# 1. Buat profil
curl -X POST -H "X-API-Key: <key>" -H "Content-Type: application/json" `
  -d '{"name":"WA_1"}' https://wa-blast-id.vercel.app/profile

# 2. Trigger pesan (masuk antrian)
curl -X POST -H "X-API-Key: <key>" -H "Content-Type: application/json" `
  -d '{"number":"+6281","message":"halo"}' https://wa-blast-id.vercel.app/message/trigger

# 3. Cek antrian (status harus berubah ke "done" setelah worker proses)
curl -H "X-API-Key: <key>" https://wa-blast-id.vercel.app/commands
```

---

## Troubleshooting

| Gejala | Penyebab | Solusi |
|--------|----------|--------|
| `/health` masih `mode:memory` | `FIREBASE_CREDENTIALS_JSON` belum set / belum redeploy | Set env var + Redeploy |
| `commands` stuck `pending` | Worker lokal tidak jalan | Jalankan `py -m backend_vercel.worker` |
| `commands` jadi `failed` | ADB tidak nemu perangkat | Cek `adb devices`, set `QA_ADB_SERIAL` |
| Dashboard error `API 401` | API key salah | Samakan dengan `QA_API_KEY` di Vercel |
