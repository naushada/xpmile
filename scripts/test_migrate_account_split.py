"""
Pytest tests for migrate-account-split.py.

Run with:

    pip3 install 'pymongo>=4' 'mongomock>=4' 'pytest>=7'
    pytest scripts/test_migrate_account_split.py -v
"""

import importlib.util
import os
import sys

import mongomock
import pytest

# Load migrate-account-split.py by file path (the hyphen in the filename
# breaks normal "import migrate-account-split").
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_MODULE_PATH = os.path.join(_SCRIPT_DIR, "migrate-account-split.py")
_spec = importlib.util.spec_from_file_location("migrate_account_split", _MODULE_PATH)
mig = importlib.util.module_from_spec(_spec)
sys.modules["migrate_account_split"] = mig
_spec.loader.exec_module(mig)


@pytest.fixture
def client():
    """A fresh mongomock client per test."""
    return mongomock.MongoClient()


# ── Step pre-A.1 ──────────────────────────────────────────────────────────────

def test_migration_exits_when_schema_version_is_already_2(client):
    """Idempotency guard: schema_version already at target → no-op."""
    client["xpmile"]["schema_version"].insert_one(
        {"_id": "current", "version": 2})
    # Seed an account WITH auth fields to prove they're not touched.
    client["xpmile"]["account"].insert_one({
        "accountCode": "alice",
        "passwordHash": "h",
        "email": "a@x.com",
        "name": "Alice",
        "role": "Admin",
        "awbPrefix": "AWB",
    })

    result = mig.migrate(client)

    assert result.skipped is True
    assert result.docs_migrated == 0
    assert result.docs_unset == 0
    # xpmile.account doc untouched
    doc = client["xpmile"]["account"].find_one({"accountCode": "alice"})
    assert doc["passwordHash"] == "h"
    assert doc["email"] == "a@x.com"
    # idp.account stayed empty
    assert client["idp"]["account"].count_documents({}) == 0


def test_migration_exits_when_schema_version_is_higher_than_target(client):
    """A future schema_version (e.g. 5) also short-circuits."""
    client["xpmile"]["schema_version"].insert_one(
        {"_id": "current", "version": 5})
    client["xpmile"]["account"].insert_one(
        {"accountCode": "alice", "passwordHash": "h"})

    result = mig.migrate(client)
    assert result.skipped is True
    # Untouched
    assert client["xpmile"]["account"].find_one(
        {"accountCode": "alice"})["passwordHash"] == "h"


# ── Step pre-A.2 ──────────────────────────────────────────────────────────────

def test_migration_copies_auth_fields_and_unsets_them_in_xpmile(client):
    """The headline behaviour: split a full account doc cleanly."""
    client["xpmile"]["account"].insert_one({
        "accountCode": "alice",
        "passwordHash": "h",
        "email": "a@x.com",
        "name": "Alice",
        "role": "Admin",
        # business fields
        "awbPrefix": "AWB",
        "eventLocation": "UAE",
        "personalInfo": {"phone": "555-1234"},
    })

    result = mig.migrate(client)

    assert result.skipped is False
    assert result.docs_migrated == 1
    assert result.docs_already_in_idp == 0
    assert result.docs_unset == 1

    # idp.account has the auth fields
    idp_doc = client["idp"]["account"].find_one({"accountCode": "alice"})
    assert idp_doc is not None
    assert idp_doc["accountCode"] == "alice"
    assert idp_doc["passwordHash"] == "h"
    assert idp_doc["email"] == "a@x.com"
    assert idp_doc["name"] == "Alice"
    assert idp_doc["role"] == "Admin"
    # No business fields leaked into idp
    assert "awbPrefix" not in idp_doc
    assert "eventLocation" not in idp_doc

    # xpmile.account lost the auth fields, kept the business fields
    xp_doc = client["xpmile"]["account"].find_one({"accountCode": "alice"})
    assert xp_doc is not None
    for field in mig.AUTH_FIELDS:
        assert field not in xp_doc, (
            f"xpmile.account still has auth field {field}: {xp_doc}")
    assert xp_doc["accountCode"] == "alice"  # link key preserved
    assert xp_doc["awbPrefix"] == "AWB"
    assert xp_doc["eventLocation"] == "UAE"
    assert xp_doc["personalInfo"] == {"phone": "555-1234"}


def test_migration_handles_multiple_accounts(client):
    """The loop covers every doc."""
    for i, code in enumerate(["alice", "bob", "carol"]):
        client["xpmile"]["account"].insert_one({
            "accountCode": code,
            "passwordHash": f"h{i}",
            "email": f"{code}@x.com",
            "name": code.capitalize(),
            "role": "Customer",
            "awbPrefix": f"P{i}",
        })

    result = mig.migrate(client)
    assert result.docs_migrated == 3

    assert client["idp"]["account"].count_documents({}) == 3
    for code in ["alice", "bob", "carol"]:
        idp_doc = client["idp"]["account"].find_one({"accountCode": code})
        assert idp_doc is not None
        assert "passwordHash" in idp_doc
        xp_doc = client["xpmile"]["account"].find_one({"accountCode": code})
        assert "passwordHash" not in xp_doc
        assert "awbPrefix" in xp_doc


def test_migration_skips_docs_without_accountCode(client):
    """Defensive: a malformed doc without accountCode is logged + skipped."""
    client["xpmile"]["account"].insert_one(
        {"passwordHash": "orphan", "awbPrefix": "ORP"})
    client["xpmile"]["account"].insert_one(
        {"accountCode": "alice", "passwordHash": "h"})

    result = mig.migrate(client)
    # Only alice migrated
    assert result.docs_migrated == 1
    assert client["idp"]["account"].count_documents({}) == 1


def test_migration_handles_doc_with_only_some_auth_fields(client):
    """Partial auth field set (e.g. no role): only the present fields move."""
    client["xpmile"]["account"].insert_one({
        "accountCode": "minimal",
        "passwordHash": "h",
        # no email, no name, no role
        "awbPrefix": "MIN",
    })

    result = mig.migrate(client)
    idp_doc = client["idp"]["account"].find_one({"accountCode": "minimal"})
    assert idp_doc["passwordHash"] == "h"
    assert "email" not in idp_doc
    assert "name" not in idp_doc
    assert "role" not in idp_doc


# ── Step pre-A.3 ──────────────────────────────────────────────────────────────

def test_migration_is_idempotent_on_partial_state(client):
    """Re-run after a crash: idp.account already has the doc, xpmile.account
    still has the auth fields. The migration cleans up without duplicating."""
    client["idp"]["account"].insert_one({
        "accountCode": "alice",
        "passwordHash": "h",
        "email": "a@x.com",
    })
    client["xpmile"]["account"].insert_one({
        "accountCode": "alice",
        "passwordHash": "h",
        "awbPrefix": "AWB",
    })

    result = mig.migrate(client)

    # idp.account still has exactly one doc — we didn't double-insert
    assert client["idp"]["account"].count_documents({}) == 1
    # The pre-existing idp doc was NOT inserted (idempotency hit)
    assert result.docs_already_in_idp == 1
    assert result.docs_migrated == 0
    # But xpmile.account had its auth field $unset
    assert result.docs_unset == 1
    xp_doc = client["xpmile"]["account"].find_one({"accountCode": "alice"})
    assert "passwordHash" not in xp_doc
    assert xp_doc["awbPrefix"] == "AWB"


# ── Step pre-A.4 ──────────────────────────────────────────────────────────────

def test_migration_sets_schema_version_to_2(client):
    """The version doc is upserted after a successful run."""
    client["xpmile"]["account"].insert_one(
        {"accountCode": "alice", "passwordHash": "h"})

    mig.migrate(client)

    sv = client["xpmile"]["schema_version"].find_one({"_id": "current"})
    assert sv is not None
    assert sv["version"] == 2


def test_migration_sets_schema_version_even_when_no_accounts(client):
    """An empty xpmile.account is still a valid migration — version bumps."""
    mig.migrate(client)
    sv = client["xpmile"]["schema_version"].find_one({"_id": "current"})
    assert sv["version"] == 2


# ── Step pre-A.5 ──────────────────────────────────────────────────────────────

def test_second_run_is_no_op_even_with_new_unmigrated_docs(client):
    """Once the gate is set, subsequent inserts of pre-split docs are NOT
    migrated automatically. This is the design intent (operator must
    rewind schema_version to re-run)."""
    # First run completes the migration.
    client["xpmile"]["account"].insert_one(
        {"accountCode": "alice", "passwordHash": "h_old"})
    mig.migrate(client)

    # Now a misbehaving caller inserts a pre-split doc.
    client["xpmile"]["account"].insert_one(
        {"accountCode": "bob",
         "passwordHash": "h_new",
         "awbPrefix": "B"})

    # Second run is skipped.
    result = mig.migrate(client)
    assert result.skipped is True

    # bob's doc is unchanged — still has passwordHash on the xpmile side.
    bob = client["xpmile"]["account"].find_one({"accountCode": "bob"})
    assert "passwordHash" in bob
    # And bob isn't in idp.account either.
    assert client["idp"]["account"].find_one({"accountCode": "bob"}) is None


# ── dry-run mode ──────────────────────────────────────────────────────────────

def test_dry_run_makes_no_changes(client):
    """--dry-run reports what would happen but doesn't touch anything."""
    client["xpmile"]["account"].insert_one({
        "accountCode": "alice",
        "passwordHash": "h",
        "awbPrefix": "AWB",
    })

    result = mig.migrate(client, dry_run=True)

    # Counts reflect what WOULD have happened.
    assert result.skipped is False
    assert result.docs_migrated == 1
    assert result.docs_unset == 1

    # But nothing actually changed.
    xp_doc = client["xpmile"]["account"].find_one({"accountCode": "alice"})
    assert "passwordHash" in xp_doc
    assert client["idp"]["account"].count_documents({}) == 0
    sv = client["xpmile"]["schema_version"].find_one({"_id": "current"})
    assert sv is None


def test_dry_run_followed_by_real_run_succeeds(client):
    """A dry-run is non-destructive and doesn't poison a subsequent real run."""
    client["xpmile"]["account"].insert_one({
        "accountCode": "alice", "passwordHash": "h", "awbPrefix": "AWB",
    })
    mig.migrate(client, dry_run=True)
    real = mig.migrate(client, dry_run=False)

    assert real.docs_migrated == 1
    assert client["idp"]["account"].count_documents({}) == 1
