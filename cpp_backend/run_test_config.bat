@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\Administrator\qa-multi-profile-harness\cpp_backend
if not exist test_build mkdir test_build
cl /std:c++20 /EHsc /I src /I third_party src\util.cpp src\config.cpp test_config.cpp /Fe:test_build\test_config.exe /Fo:test_build\
if %errorlevel% neq 0 (
  echo COMPILE FAILED
  exit /b 1
)
test_build\test_config.exe
