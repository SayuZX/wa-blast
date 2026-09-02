@echo off
REM ============================================================================
REM  run_worker.bat — Jalankan worker lokal (poll antrian commands dari Firestore
REM  dan eksekusi ADB). Jalankan di PowerShell/CMD dari folder proyek.
REM
REM  Prasyarat:
REM    - Python 3.10+ (pakai `py`)
REM    - firebase-admin terpasang:  py -m pip install -r backend_vercel/requirements.txt
REM    - FIREBASE_CREDENTIALS_JSON diisi (service account key, SATU BARIS)
REM ============================================================================

setlocal

REM --- Ganti nilai di bawah dengan milik Anda --------------------------------
set FIREBASE_CREDENTIALS_JSON=
set QA_ADB_SERIAL=
set QA_TARGET_PACKAGE=com.whatsapp
set QA_TARGET_ACTIVITY=com.whatsapp.Main
set QA_WORKER_POLL=3.0
REM ---------------------------------------------------------------------------

if "%FIREBASE_CREDENTIALS_JSON%"=="" (
  echo [ERROR] FIREBASE_CREDENTIALS_JSON belum diisi.
  echo         Edit run_worker.bat dan tempel service account key (satu baris).
  exit /b 1
)

py -m backend_vercel.worker
