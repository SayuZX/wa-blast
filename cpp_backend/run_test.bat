@echo off
REM Run all pure-logic C++ unit tests (util + config) via MSVC.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\Administrator\qa-multi-profile-harness\cpp_backend
if not exist test_build mkdir test_build

echo ============================================
echo  [1/2] util tests (uuid/base64/trim/time)
echo ============================================
cl /std:c++20 /EHsc /I src src\util.cpp test_util.cpp /Fe:test_build\test_util.exe /Fo:test_build\ >nul 2>&1
test_build\test_util.exe
if %errorlevel% neq 0 exit /b 1

echo.
echo ============================================
echo  [2/2] config tests (flat + nested JSON schema)
echo ============================================
cl /std:c++20 /EHsc /I src /I third_party src\util.cpp src\config.cpp test_config.cpp /Fe:test_build\test_config.exe /Fo:test_build\ >nul 2>&1
test_build\test_config.exe
if %errorlevel% neq 0 exit /b 1

echo.
echo ============================================
echo  ALL C++ UNIT TESTS PASSED
echo ============================================
