"""
logging_setup.py — Structured logging for the QA harness.

Produces human-readable console logs plus optional JSON-lines log files
(one file per day) so activity can be tailed by the dashboard and ingested
by log aggregators. Every log record carries a request-id when available
(thread-local), so a single API call's full trace can be correlated.
"""

from __future__ import annotations

import json
import logging
import sys
import threading
import time
from pathlib import Path
from typing import Any

from .config import settings

# Thread-local request id (set by middleware in app.py).
_tls = threading.local()


def set_request_id(rid: str) -> None:
    _tls.request_id = rid


def get_request_id() -> str:
    return getattr(_tls, "request_id", "-")


class JsonFormatter(logging.Formatter):
    """Format a record as a single JSON object per line."""

    def format(self, record: logging.LogRecord) -> str:
        payload: dict[str, Any] = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(record.created))
            + f".{int(record.msecs):03d}Z",
            "level": record.levelname,
            "logger": record.name,
            "request_id": get_request_id(),
            "message": record.getMessage(),
        }
        if record.exc_info:
            payload["exc_info"] = self.formatException(record.exc_info)
        return json.dumps(payload, ensure_ascii=False)


class ConsoleFormatter(logging.Formatter):
    """Compact, colourless console formatter (CI-safe)."""

    def format(self, record: logging.LogRecord) -> str:
        rid = get_request_id()
        suffix = f" [req={rid}]" if rid != "-" else ""
        return (
            f"{time.strftime('%H:%M:%S', time.gmtime(record.created))} "
            f"{record.levelname:<7} {record.name:<22} "
            f"{record.getMessage()}{suffix}"
        )


def setup_logging() -> None:
    """Configure root + qa_harness loggers. Idempotent."""
    settings.ensure_dirs()
    level = getattr(logging, settings.log_level.upper(), logging.INFO)

    root = logging.getLogger("qa_harness")
    if root.handlers:  # already configured
        return
    root.setLevel(level)
    root.propagate = False

    console = logging.StreamHandler(sys.stdout)
    console.setFormatter(ConsoleFormatter())
    root.addHandler(console)

    if settings.json_logging:
        today = time.strftime("%Y-%m-%d")
        json_path = settings.log_dir / f"harness-{today}.jsonl"
        file_handler = logging.FileHandler(json_path, encoding="utf-8")
        file_handler.setFormatter(JsonFormatter())
        root.addHandler(file_handler)

    # Quiet noisy third-party loggers.
    for noisy in ("uvicorn", "uvicorn.access"):
        logging.getLogger(noisy).setLevel(logging.WARNING)
