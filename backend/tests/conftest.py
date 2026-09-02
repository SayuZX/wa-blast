"""Shared pytest fixtures: an isolated FastAPI TestClient with a fake ADB."""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

# Import the app module (its import-time side effects are already guarded by
# the env vars set in tests/__init__.py).
import backend.app as app_module


@pytest.fixture()
def client(monkeypatch):
    """Return a TestClient against the real app with ADB fully faked out.

    We patch the module-level ``adb`` (and re-point profile manager's adb) so
    no real ``adb`` subprocess is ever spawned.
    """
    from backend.adb_helper import AdbResult

    class FakeAdb:
        """A fake AdbHelper that records calls and never touches the OS."""

        def __init__(self):
            self.calls: list[str] = []
            self.serial = ""
            self.adb_path = "adb"
            self.timeout = 30.0

        def run(self, args, timeout=None, check=False):
            self.calls.append(" ".join(args))
            return AdbResult(0, "ok", "", ["adb"] + list(args))

        def shell(self, command, as_root=False, timeout=None, check=False):
            self.calls.append(f"shell {command}")
            return AdbResult(0, "uid=0(root)", "", ["adb", "shell", command])

        def push(self, local, remote, check=True):
            self.calls.append(f"push {local} {remote}")
            return AdbResult(0, "", "", ["adb", "push", local, remote])

        def pull(self, remote, local, check=True):
            self.calls.append(f"pull {remote} {local}")
            return AdbResult(0, "", "", ["adb", "pull", remote, local])

        def get_state(self):
            return AdbResult(0, "device", "", ["adb", "get-state"])

        def is_rooted(self):
            return True

        def start_activity(self, package, activity):
            return AdbResult(0, "", "", ["adb", "am", "start"])

        def force_stop(self, package):
            return AdbResult(0, "", "", ["adb", "force-stop"])

        def wake_and_unlock(self):
            self.calls.append("wake_and_unlock")

        def send_view_intent(self, uri):
            self.calls.append(f"view {uri}")
            return AdbResult(0, "", "", ["adb", "am", "start", uri])

        def input_text(self, text):
            return AdbResult(0, "", "", ["adb", "input", "text"])

        def keyevent(self, keycode):
            return AdbResult(0, "", "", ["adb", "keyevent", str(keycode)])

        def screen_size(self):
            return (1080, 2400)

    fake = FakeAdb()
    # Patch the module-level singleton used by endpoints.
    monkeypatch.setattr(app_module, "adb", fake)
    # Re-point the profile manager + messaging engine to the fake.
    app_module.profiles.adb = fake
    app_module.messaging.adb = fake
    # Re-bind the spoof callback, which closed over the REAL adb at import time.
    from backend.spoof import make_spoof_callback
    app_module.profiles.set_spoof_callback(make_spoof_callback(fake))
    # Reset in-memory profile registry for test isolation.
    app_module.profiles._profiles = {}
    app_module.profiles._save_registry()

    with TestClient(app_module.app) as c:
        yield c

    # Cleanup any registry written during the test.
    app_module.profiles._profiles = {}


@pytest.fixture()
def auth():
    """Convenience header for authenticated requests."""
    return {"X-API-Key": "test-key-123"}
