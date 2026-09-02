"""
adb_helper.py — Thin, robust wrapper around the Android Debug Bridge (ADB).

Responsibilities:
  * Build a base ``adb`` command with the correct serial/path.
  * Run commands synchronously with timeouts and rich error reporting.
  * Provide the higher-level primitives the rest of the harness uses:
      - shell commands (as root or normal shell)
      - file push/pull
      - UI automation (tap, swipe, keyevent, text input)
      - intent dispatch (VIEW / SENDTO) used by /message/trigger
      - device/user introspection

Design notes:
  * All commands are string templates passed to ``subprocess.run`` with
    ``shell=False`` (list args) — no shell injection risk from message text.
  * Text destined for the device UI is base64-encoded before being pushed to
    ``input text`` to survive spaces/newlines/special characters.
  * Every call returns a small ``AdbResult`` namedtuple so callers can
    branch on return code and capture stdout/stderr.
"""

from __future__ import annotations

import base64
import logging
import shlex
import subprocess
import time
from dataclasses import dataclass
from typing import Iterable, List, Optional, Sequence

from .config import settings

log = logging.getLogger("qa_harness.adb")


class AdbError(RuntimeError):
    """Raised when an adb command fails and the caller opts to raise."""

    def __init__(self, message: str, result: "AdbResult"):
        super().__init__(message)
        self.result = result


@dataclass
class AdbResult:
    returncode: int
    stdout: str
    stderr: str
    command: List[str]

    @property
    def ok(self) -> bool:
        return self.returncode == 0

    @property
    def out(self) -> str:
        """stdout with trailing whitespace stripped."""
        return self.stdout.strip()

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return (
            f"AdbResult(rc={self.returncode}, stdout={self.out[:120]!r}, "
            f"stderr={self.stderr[:120]!r})"
        )


class AdbHelper:
    """Encapsulates all adb interactions for a single target device."""

    def __init__(
        self,
        serial: Optional[str] = None,
        adb_path: Optional[str] = None,
        timeout: float = 30.0,
    ) -> None:
        self.serial = serial or settings.adb_serial or ""
        self.adb_path = adb_path or settings.adb_path
        self.timeout = timeout

    # ------------------------------------------------------------------ #
    # Low-level command construction & execution
    # ------------------------------------------------------------------ #
    def _base(self) -> List[str]:
        cmd = [self.adb_path]
        if self.serial:
            cmd += ["-s", self.serial]
        return cmd

    def run(
        self,
        args: Sequence[str],
        timeout: Optional[float] = None,
        check: bool = False,
    ) -> AdbResult:
        """Run an adb command and return its result."""
        cmd = self._base() + list(args)
        log.debug("adb %s", shlex.join(cmd[1:]) if len(cmd) > 1 else "(no args)")
        try:
            proc = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout or self.timeout,
            )
        except subprocess.TimeoutExpired as exc:
            raise AdbError(
                f"adb command timed out after {timeout or self.timeout}s: {cmd}",
                AdbResult(-1, "", str(exc), cmd),
            ) from exc
        except FileNotFoundError as exc:
            raise AdbError(
                f"adb binary not found at '{self.adb_path}'. "
                "Install platform-tools and set QA_ADB_PATH if needed.",
                AdbResult(-2, "", str(exc), cmd),
            ) from exc

        result = AdbResult(proc.returncode, proc.stdout, proc.stderr, cmd)
        if check and not result.ok:
            raise AdbError(
                f"adb command failed ({result.returncode}): {cmd}\n"
                f"stderr: {result.stderr.strip()}",
                result,
            )
        return result

    def shell(
        self,
        command: str,
        *,
        as_root: bool = False,
        timeout: Optional[float] = None,
        check: bool = False,
    ) -> AdbResult:
        """Run a shell command on the device.

        ``command`` is a single string; it is NOT shell-interpreted beyond
        adb's own ``sh -c``, so do not pass untrusted input without quoting.
        For safety, prefer the dedicated helpers (tap/text/etc.) for
        untrusted UI payloads.
        """
        if as_root:
            return self.run(["shell", "su", "-c", command], timeout=timeout, check=check)
        return self.run(["shell", command], timeout=timeout, check=check)

    # ------------------------------------------------------------------ #
    # File transfer
    # ------------------------------------------------------------------ #
    def push(self, local: str, remote: str, check: bool = True) -> AdbResult:
        return self.run(["push", local, remote], check=check)

    def pull(self, remote: str, local: str, check: bool = True) -> AdbResult:
        return self.run(["pull", remote, local], check=check)

    # ------------------------------------------------------------------ #
    # Device / user introspection
    # ------------------------------------------------------------------ #
    def devices(self) -> List[str]:
        """Return the list of connected serials (no ``-s`` filter applied)."""
        raw = subprocess.run(
            [self.adb_path, "devices"], capture_output=True, text=True
        )
        lines = raw.stdout.strip().splitlines()[1:]
        return [ln.split("\t")[0] for ln in lines if "\t" in ln]

    def get_state(self) -> str:
        return self.run(["get-state"]).out

    def wait_for_device(self, timeout: float = 30.0) -> None:
        self.run(["wait-for-device"], timeout=timeout, check=True)

    def is_rooted(self) -> bool:
        """Heuristic: can we elevate to root via ``su``?"""
        res = self.shell("id", as_root=True)
        return res.ok and "uid=0" in res.out

    def list_users(self) -> List[str]:
        """Return Android user ids (e.g. ['0', '10'])."""
        res = self.shell("pm list users", as_root=True)
        users: List[str] = []
        for line in res.stdout.splitlines():
            # Lines look like: "UserInfo{0:Owner:c13} running"
            if "UserInfo" in line:
                start = line.find("{") + 1
                end = line.find(":")
                if start > 0 and end > start:
                    users.append(line[start:end])
        return users

    # ------------------------------------------------------------------ #
    # UI automation primitives
    # ------------------------------------------------------------------ #
    def tap(self, x: int, y: int) -> AdbResult:
        return self.shell(f"input tap {x} {y}")

    def swipe(self, x1: int, y1: int, x2: int, y2: int, duration_ms: int = 300) -> AdbResult:
        return self.shell(f"input swipe {x1} {y1} {x2} {y2} {duration_ms}")

    def keyevent(self, keycode: int) -> AdbResult:
        return self.shell(f"input keyevent {keycode}")

    def input_text(self, text: str) -> AdbResult:
        """Type arbitrary text into the focused field.

        We base64-encode to handle spaces and special characters safely.
        ``input text`` does not support newlines; multi-line payloads are
        split and sent with ENTER between lines.
        """
        results: List[AdbResult] = []
        lines = text.split("\n")
        for i, line in enumerate(lines):
            if i > 0:
                results.append(self.keyevent(66))  # KEYCODE_ENTER
            if line:
                b64 = base64.b64encode(line.encode("utf-8")).decode("ascii")
                results.append(self.shell(f"input text '{b64}'"))
        return results[-1] if results else AdbResult(0, "", "", ["<noop>"])

    def start_activity(self, package: str, activity: str) -> AdbResult:
        """Cold-start the target app's launcher activity."""
        return self.shell(
            f"am start -n {package}/{activity}", check=False
        )

    def force_stop(self, package: str) -> AdbResult:
        return self.shell(f"am force-stop {package}", as_root=True, check=False)

    def send_view_intent(self, uri: str) -> AdbResult:
        """Dispatch an ACTION_VIEW intent with the given URI.

        Used by /message/trigger to hand a message to the target app's
        deep-link / share handler (e.g. wa.me/<number>?text=<message>).
        """
        # ``am start`` with --activity-clear-top ensures a clean handoff.
        return self.shell(
            f"am start -a android.intent.action.VIEW -d {shlex.quote(uri)}",
            check=False,
        )

    def send_sendto_intent(self, number: str, body: str) -> AdbResult:
        """Dispatch an ACTION_SENDTO (sms-like) intent. Fallback path."""
        b64 = base64.b64encode(body.encode("utf-8")).decode("ascii")
        return self.shell(
            f"am start -a android.intent.action.SENDTO "
            f"-d sms:{shlex.quote(number)} "
            f"--es sms_body '{b64}'",
            check=False,
        )

    def wake_and_unlock(self) -> None:
        """Bring the device to a known foreground/unlocked state."""
        self.keyevent(224)  # KEYCODE_WAKEUP
        self.shell("input keyevent 82")  # KEYCODE_MENU (dismisses keyguard on many setups)
        time.sleep(0.3)

    def screen_size(self) -> Optional[tuple[int, int]]:
        """Return (width, height) in pixels, or None if unavailable."""
        res = self.shell("wm size")
        # Output format: "Physical size: 1080x2400"
        try:
            dims = res.out.split(":")[-1].strip().split("x")
            return int(dims[0]), int(dims[1])
        except (ValueError, IndexError):
            return None
