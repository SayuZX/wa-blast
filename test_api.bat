@echo off
REM ============================================================================
REM  test_api.bat — Uji koneksi ke backend (lokal atau Vercel).
REM
REM  Penggunaan:
REM    test_api.bat https://wa-blast-id.vercel.app  API_KEY_ANDA
REM ============================================================================

setlocal

set BASE_URL=%~1
set API_KEY=%~2

if "%BASE_URL%"=="" set BASE_URL=http://127.0.0.1:8000
if "%API_KEY%"=="" set API_KEY=test-key-123

echo ============================================
echo  Testing API: %BASE_URL%
echo ============================================

echo.
echo [1] GET /health (tanpa auth)
curl -s "%BASE_URL%/health"
echo.

echo.
echo [2] GET /profiles (tanpa key -> harus 401)
curl -s -o NUL -w "HTTP %%{http_code}\n" "%BASE_URL%/profiles"

echo.
echo [3] GET /profiles (dengan key -> harus 200)
curl -s -H "X-API-Key: %API_KEY%" "%BASE_URL%/profiles"
echo.

echo.
echo [4] POST /profile (buat WA_1)
curl -s -X POST -H "X-API-Key: %API_KEY%" -H "Content-Type: application/json" -d "{\"name\":\"WA_1\"}" "%BASE_URL%/profile"
echo.

echo.
echo [5] POST /message/trigger (antre pesan)
curl -s -X POST -H "X-API-Key: %API_KEY%" -H "Content-Type: application/json" -d "{\"number\":\"+6281\",\"message\":\"test\"}" "%BASE_URL%/message/trigger"
echo.

echo.
echo [6] GET /commands (lihat antrian)
curl -s -H "X-API-Key: %API_KEY%" "%BASE_URL%/commands"
echo.

echo.
echo ============================================
echo  Selesai
echo ============================================
