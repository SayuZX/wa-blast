"""
Test suite for the QA Multi-Profile Harness backend.

These tests use FastAPI's TestClient (in-process) and DO NOT require a real
Android device or adb. Device-dependent behaviour is covered by mocking the
AdbHelper, so the suite runs on any host (CI-friendly).

Run from the project root:

    pytest backend/tests -v

or a single file:

    pytest backend/tests/test_api.py -v
"""

import os
import sys
from pathlib import Path

# Ensure the project root is on sys.path so `backend` is importable.
ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

# Configure settings BEFORE importing the app (which reads env at import time).
os.environ.setdefault("QA_API_KEY", "test-key-123")
os.environ.setdefault("QA_ADB_PATH", "adb")  # adb absent -> AdbError path
os.environ.setdefault("QA_SNAPSHOT_ROOT", str(ROOT / "data" / "test_profiles"))
os.environ.setdefault("QA_LOG_DIR", str(ROOT / "data" / "test_logs"))
os.environ.setdefault("QA_JSON_LOGGING", "false")
