"""
messaging.py — Message trigger orchestration for the QA harness.

Implements the three message-related operations exposed over the API:
  * single trigger  -> ADB intent VIEW / fallback SENDTO
  * batch           -> sequential triggers with 5–10 s jitter
  * CSV/TXT upload  -> parse recipients+messages and feed the batch engine

The actual "typing" path (input text + ENTER) is also available and is used
when the intent path is unavailable or when the target app has no deep-link
handler for the URI scheme.
"""

from __future__ import annotations

import asyncio
import csv
import io
import logging
import random
import time
from dataclasses import dataclass, field
from typing import List, Optional

from .adb_helper import AdbHelper
from .config import settings
from .profile_manager import Profile, ProfileManager

log = logging.getLogger("qa_harness.messaging")


class MessageError(RuntimeError):
    """Raised on any message-trigger failure."""


@dataclass
class TriggerResult:
    index: int
    number: str
    message: str
    ok: bool
    error: Optional[str] = None
    elapsed_ms: int = 0


@dataclass
class BatchResult:
    total: int
    succeeded: int
    failed: int
    results: List[TriggerResult] = field(default_factory=list)


class MessageEngine:
    def __init__(self, adb: AdbHelper, profiles: ProfileManager) -> None:
        self.adb = adb
        self.profiles = profiles

    # ------------------------------------------------------------------ #
    # Single trigger
    # ------------------------------------------------------------------ #
    def trigger(
        self,
        number: str,
        message: str,
        *,
        method: str = "auto",
    ) -> TriggerResult:
        """Trigger a single message to ``number``.

        ``method``: "auto" (intent first, fallback to typing), "intent",
        or "typing".
        """
        start = time.monotonic()
        err: Optional[str] = None
        ok = True

        try:
            self._ensure_active_profile()
            self.adb.wake_and_unlock()

            if method in ("auto", "intent"):
                ok = self._trigger_via_intent(number, message)
                if not ok and method == "auto":
                    log.info("Intent path failed; falling back to typing")
                    ok = self._trigger_via_typing(number, message)
            else:  # "typing"
                ok = self._trigger_via_typing(number, message)

            if not ok:
                err = "Trigger failed (see backend logs)"
        except MessageError as exc:
            ok = False
            err = str(exc)
        except Exception as exc:  # noqa: BLE001 - boundary for API layer
            ok = False
            err = f"Unexpected error: {exc}"

        elapsed_ms = int((time.monotonic() - start) * 1000)
        res = TriggerResult(0, number, message, ok, err, elapsed_ms)
        log.info(
            "trigger number=%s ok=%s elapsed_ms=%d",
            number, ok, elapsed_ms,
        )
        return res

    def _ensure_active_profile(self) -> None:
        if self.profiles.current() is None:
            raise MessageError("No active profile; switch to a profile first")

    def _trigger_via_intent(self, number: str, message: str) -> bool:
        """Try ACTION_VIEW deep-link (e.g. wa.me). Returns True on success."""
        # WhatsApp-style deep link. Generic enough for wa.me; other apps
        # (Telegram etc.) can be configured via QA_TARGET_PACKAGE + scheme.
        uri = f"https://wa.me/{number}?text={message}"
        res = self.adb.send_view_intent(uri)
        return res.ok

    def _trigger_via_typing(self, number: str, message: str) -> bool:
        """Fallback: launch app, focus composer, type, and press ENTER.

        This is inherently fragile (depends on the app's current UI state),
        so it is best-effort and returns False rather than raising.
        """
        try:
            # Open the app fresh.
            self.adb.start_activity(settings.target_package, settings.target_activity)
            time.sleep(1.0)
            # Type the message and press ENTER. Coordinates/flow are app-
            # specific; adjust in a subclass for the exact target app.
            self.adb.input_text(message)
            self.adb.keyevent(66)  # ENTER
            return True
        except Exception as exc:  # noqa: BLE001
            log.warning("typing path error: %s", exc)
            return False

    # ------------------------------------------------------------------ #
    # Batch
    # ------------------------------------------------------------------ #
    async def batch(
        self,
        items: List[dict],
        *,
        delay_min: Optional[float] = None,
        delay_max: Optional[float] = None,
    ) -> BatchResult:
        """Run a sequence of triggers with 5–10 s (jittered) delays.

        Each item is a dict with ``number`` and ``message`` keys.
        """
        delay_min = delay_min if delay_min is not None else settings.batch_delay_min
        delay_max = delay_max if delay_max is not None else settings.batch_delay_max

        batch = BatchResult(total=len(items), succeeded=0, failed=0)
        for i, item in enumerate(items):
            number = item.get("number", "")
            message = item.get("message", "")
            if not number or not message:
                batch.failed += 1
                batch.results.append(
                    TriggerResult(i, number, message, False, "missing number/message")
                )
                continue

            # Run the blocking trigger off the event loop.
            res = await asyncio.to_thread(self.trigger, number, message)
            res.index = i
            batch.results.append(res)
            if res.ok:
                batch.succeeded += 1
            else:
                batch.failed += 1

            # Jittered inter-message delay (skip after the last item).
            if i < len(items) - 1:
                await asyncio.sleep(random.uniform(delay_min, delay_max))

        log.info(
            "batch done total=%d ok=%d failed=%d",
            batch.total, batch.succeeded, batch.failed,
        )
        return batch


def parse_batch_file(content: bytes, filename: str) -> List[dict]:
    """Parse an uploaded CSV or TXT file into a list of {number, message}.

    CSV format: ``number,message`` (header optional; we detect it).
    TXT format: one ``number<tab>message`` or ``number,message`` per line.
    """
    text = content.decode("utf-8", errors="replace")
    items: List[dict] = []

    if filename.lower().endswith(".csv"):
        reader = csv.reader(io.StringIO(text))
        rows = list(reader)
        if not rows:
            return items
        # Skip header if it looks like one.
        first = [c.strip().lower() for c in rows[0]]
        if first and first[0] in {"number", "recipient", "to", "phone"}:
            rows = rows[1:]
        for row in rows:
            if len(row) < 2 or not row[0].strip():
                continue
            items.append({"number": row[0].strip(), "message": row[1].strip()})
    else:  # .txt
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            # Split on tab or comma (first occurrence).
            if "\t" in line:
                number, _, message = line.partition("\t")
            elif "," in line:
                number, _, message = line.partition(",")
            else:
                continue
            number, message = number.strip(), message.strip()
            if number and message:
                items.append({"number": number, "message": message})

    return items
