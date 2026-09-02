"""Unit tests for pure logic (no app, no ADB, no device)."""

from __future__ import annotations

from backend.messaging import parse_batch_file
from backend.spoof import (
    _luhn_checksum,
    generate_identity,
    validate_identity,
)


# --------------------------------------------------------------------------- #
# Spoof / identity
# --------------------------------------------------------------------------- #
def test_luhn_checksum_known_value():
    # 14-digit prefix "35693803564380" has known check digit "9".
    assert _luhn_checksum("35693803564380") == "9"


def test_generated_imei_is_luhn_valid():
    for _ in range(20):
        imei = generate_identity()["imei"]
        assert len(imei) == 15
        digits = [int(d) for d in imei]
        total = sum(
            (d * 2 if d * 2 <= 9 else d * 2 - 9) if i % 2 == 1 else d
            for i, d in enumerate(reversed(digits))
        )
        assert total % 10 == 0, f"IMEI {imei} failed Luhn"


def test_generated_android_id_is_16_hex():
    for _ in range(20):
        android_id = generate_identity()["android_id"]
        assert len(android_id) == 16
        assert all(c in "0123456789abcdef" for c in android_id)


def test_validate_identity_drops_unknown_keys():
    clean = validate_identity({"imei": "123", "bogus": "x", "device_model": "M"})
    assert "bogus" not in clean
    assert clean["imei"] == "123"


def test_validate_identity_rejects_empty_value():
    import pytest
    from backend.spoof import SpoofError

    with pytest.raises(SpoofError):
        validate_identity({"imei": "  "})


# --------------------------------------------------------------------------- #
# Batch file parsing
# --------------------------------------------------------------------------- #
def test_parse_csv_with_header():
    items = parse_batch_file(
        b"number,message\n+6281,hello\n+6282,world\n", "b.csv"
    )
    assert items == [
        {"number": "+6281", "message": "hello"},
        {"number": "+6282", "message": "world"},
    ]


def test_parse_csv_without_header():
    items = parse_batch_file(b"+6281,hello\n", "b.csv")
    assert items == [{"number": "+6281", "message": "hello"}]


def test_parse_txt_tab_separated():
    items = parse_batch_file(b"+6283\ttab msg\n", "b.txt")
    assert items == [{"number": "+6283", "message": "tab msg"}]


def test_parse_txt_comma_separated():
    items = parse_batch_file(b"+6284,comma msg\n", "b.txt")
    assert items == [{"number": "+6284", "message": "comma msg"}]


def test_parse_skips_comments_and_blank_lines():
    items = parse_batch_file(
        b"# comment\n\n+6285\tmsg\n", "b.txt"
    )
    assert items == [{"number": "+6285", "message": "msg"}]


def test_parse_ignores_malformed_rows():
    items = parse_batch_file(b"justonenumber\n+6286\tok\n", "b.txt")
    assert items == [{"number": "+6286", "message": "ok"}]
