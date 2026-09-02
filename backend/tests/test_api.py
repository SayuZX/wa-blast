"""Endpoint-level tests: auth, validation, profile lifecycle, messaging."""

from __future__ import annotations


# --------------------------------------------------------------------------- #
# Auth
# --------------------------------------------------------------------------- #
def test_health_is_unauthenticated(client):
    r = client.get("/health")
    assert r.status_code == 200
    body = r.json()
    assert body["status"] == "ok"
    assert "adb_state" in body


def test_profiles_requires_api_key(client):
    r = client.get("/profiles")
    assert r.status_code == 401


def test_profiles_rejects_wrong_key(client):
    r = client.get("/profiles", headers={"X-API-Key": "wrong"})
    assert r.status_code == 401


def test_profiles_accepts_correct_key(client, auth):
    r = client.get("/profiles", headers=auth)
    assert r.status_code == 200
    assert r.json()["profiles"] == []


# --------------------------------------------------------------------------- #
# Validation
# --------------------------------------------------------------------------- #
def test_switch_requires_profile_field(client, auth):
    r = client.post("/profile/switch", json={}, headers=auth)
    assert r.status_code == 422


def test_trigger_requires_number_and_message(client, auth):
    r = client.post("/message/trigger", json={}, headers=auth)
    assert r.status_code == 422


def test_batch_rejects_empty_items(client, auth):
    r = client.post("/message/batch", json={"items": []}, headers=auth)
    assert r.status_code == 400


# --------------------------------------------------------------------------- #
# Profile lifecycle
# --------------------------------------------------------------------------- #
def test_create_profile(client, auth):
    r = client.post("/profile", json={"name": "WA_1"}, headers=auth)
    assert r.status_code == 200
    body = r.json()
    assert body["created"] == "WA_1"
    assert body["android_user"] == 0  # first profile -> Owner user


def test_create_duplicate_profile_rejected(client, auth):
    client.post("/profile", json={"name": "WA_1"}, headers=auth)
    r = client.post("/profile", json={"name": "WA_1"}, headers=auth)
    assert r.status_code == 400


def test_create_profile_auto_generates_spoof_identity(client, auth):
    client.post("/profile", json={"name": "WA_1"}, headers=auth)
    r = client.get("/profiles", headers=auth)
    profiles = r.json()["profiles"]
    spoof = profiles[0]["spoof"]
    # IMEI must be a valid 15-digit Luhn number.
    imei = spoof["imei"]
    assert len(imei) == 15
    digits = [int(d) for d in imei]
    total = sum(
        (d * 2 if d * 2 <= 9 else d * 2 - 9) if i % 2 == 1 else d
        for i, d in enumerate(reversed(digits))
    )
    assert total % 10 == 0


def test_switch_to_unknown_profile_returns_400(client, auth):
    r = client.post(
        "/profile/switch", json={"profile": "WA_9"}, headers=auth
    )
    assert r.status_code == 400


def test_switch_profile_marks_active(client, auth):
    client.post("/profile", json={"name": "WA_1"}, headers=auth)
    r = client.post(
        "/profile/switch", json={"profile": "WA_1"}, headers=auth
    )
    assert r.status_code == 200
    assert r.json()["active"] == "WA_1"
    # Confirm status reflected in /profiles.
    r2 = client.get("/profiles", headers=auth)
    assert r2.json()["active"] == "WA_1"


# --------------------------------------------------------------------------- #
# Messaging
# --------------------------------------------------------------------------- #
def test_trigger_requires_active_profile(client, auth):
    # No active profile -> graceful ok:false, not a crash.
    r = client.post(
        "/message/trigger",
        json={"number": "+6281", "message": "hi"},
        headers=auth,
    )
    assert r.status_code == 200
    assert r.json()["ok"] is False


def test_trigger_after_switch_succeeds(client, auth):
    client.post("/profile", json={"name": "WA_1"}, headers=auth)
    client.post("/profile/switch", json={"profile": "WA_1"}, headers=auth)
    r = client.post(
        "/message/trigger",
        json={"number": "+6281", "message": "hello", "method": "auto"},
        headers=auth,
    )
    assert r.status_code == 200
    assert r.json()["ok"] is True


def test_batch_upload_csv(client, auth):
    client.post("/profile", json={"name": "WA_1"}, headers=auth)
    client.post("/profile/switch", json={"profile": "WA_1"}, headers=auth)
    csv = b"number,message\n+6281111,one\n+6282222,two\n"
    r = client.post(
        "/message/batch/upload",
        files={"file": ("batch.csv", csv, "text/csv")},
        headers=auth,
    )
    assert r.status_code == 200
    body = r.json()
    assert body["parsed"] == 2
    assert body["total"] == 2


def test_batch_upload_empty_file_rejected(client, auth):
    r = client.post(
        "/message/batch/upload",
        files={"file": ("empty.csv", b"", "text/csv")},
        headers=auth,
    )
    assert r.status_code == 400


# --------------------------------------------------------------------------- #
# Logs
# --------------------------------------------------------------------------- #
def test_logs_endpoint(client, auth):
    r = client.get("/logs", headers=auth)
    assert r.status_code == 200
    assert "lines" in r.json()
