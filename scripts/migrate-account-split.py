#!/usr/bin/env python3
"""
migrate-account-split.py — one-shot, idempotent migration that splits the
xpmile.account collection into:

  idp.account     auth fields (passwordHash, email, name, role)
  xpmile.account  business fields (everything else, keyed by accountCode)

The split corresponds to the database-namespace layout in
docs/design/inhouse-idp/inhouse-idp-design.md §1. After the split, the
idp uniservice reads/writes idp.account (no cross-DB), and the marvel
uniservice reads/writes xpmile.account (no cross-DB) for everything
except the legacy POST /api/v1/account/login fallback, which does one
cross-DB read to idp.account for its password check.

Idempotency:

  - Gated by an xpmile.schema_version doc ({_id:"current", version:2}).
    On second run, the script exits early with `skipped: True`.
  - Even without the gate, the per-document operations are individually
    idempotent: lookup-then-insert into idp.account, $unset on
    xpmile.account.

For fresh deployments (first volume creation), docker/mongo-init.js
seeds schema_version=2 directly, so this script is a no-op there.

This script targets existing deployments where mongo-init.js does not
re-run, i.e. the customer already has a populated xpmile.account.

Usage:

  # Run the migration against the on-prem MongoDB (most common)
  ./scripts/migrate-account-split.py \\
      --mongo-uri 'mongodb://xpmile:xpmile_pass@localhost:27017/?authSource=admin'

  # Dry-run: log what would change, don't modify anything
  ./scripts/migrate-account-split.py --mongo-uri '...' --dry-run

  # Verbose (DEBUG-level logging)
  ./scripts/migrate-account-split.py --mongo-uri '...' -v

Exit codes:

  0  success (migration ran cleanly, or was already at target schema_version)
  1  unexpected error (connection failure, etc.)
  2  invalid arguments

Requires:

  pip3 install 'pymongo>=4'

  # Tests additionally:
  pip3 install 'mongomock>=4' 'pytest>=7'
"""

import argparse
import logging
import sys
from dataclasses import dataclass

from pymongo import MongoClient

# Bump SCHEMA_VERSION_TARGET when a future migration is added; the gate
# uses ">=" so all migrations to N or lower are skipped.
SCHEMA_VERSION_TARGET = 2

# The on-prem xpmile.account doc is *nested* (mirrors what mongo-init.js
# seeds + what handle_account_POST writes), e.g.:
#
#   { loginCredentials: { accountCode, passwordHash },
#     personalInfo:     { role, name, email },
#     ... business fields ... }
#
# This migration:
#   1. Copies passwordHash → idp.account (top-level, flat — that's the
#      shape the IdP code reads via idp_login + handle_account_login_POST's
#      Phase K fallback).
#   2. Copies role + name + email (read from personalInfo) so the IdP
#      can populate session claims without a cross-DB read on every login.
#   3. $unsets ONLY loginCredentials.passwordHash from xpmile.account —
#      role / name / email stay in personalInfo because marvel views still
#      render them. The migration is intentionally read-mostly on the
#      xpmile side; only the password hash needs to move.
#
# Idempotent — running again is a no-op (schema_version gate plus per-op
# checks: insert-if-absent on idp side, $unset on already-missing field
# is a no-op at the Mongo layer).
AUTH_FIELD_SOURCES = {
    # idp-side flat name  → list of dotted paths to try on the xpmile doc.
    # Production seed (mongo-init.js) + handle_account_POST write the
    # nested shapes; the flat fallback exists for the unit-test fixtures
    # and any future tooling that mirrors the migrated/flat layout.
    "passwordHash": ["loginCredentials.passwordHash", "passwordHash"],
    "role":         ["personalInfo.role",             "role"],
    "name":         ["personalInfo.name",             "name"],
    "email":        ["personalInfo.email",            "email"],
}

# Fields to $unset on the xpmile side after copy. The flat top-level
# names are listed too so the unit-test fixtures (which seed with
# top-level passwordHash etc.) end up cleared on the same code path —
# mirrors what would happen post-migration in production where only the
# nested loginCredentials.passwordHash needs the $unset (role / name /
# email stay in personalInfo for marvel views to keep rendering).
UNSET_FROM_XPMILE = [
    "loginCredentials.passwordHash",
    # legacy flat-shape compatibility (test fixtures):
    "passwordHash", "email", "name", "role",
]

# Back-compat alias for existing tests that referenced AUTH_FIELDS.
# Kept as the flat names of the auth fields that move to idp.account.
AUTH_FIELDS = list(AUTH_FIELD_SOURCES.keys())


def _dig(doc, dotted_path):
    """Return the value at dotted_path inside doc, or None if any segment
    is missing or not a dict along the way."""
    cur = doc
    for seg in dotted_path.split("."):
        if not isinstance(cur, dict):
            return None
        cur = cur.get(seg)
    return cur


def _account_code_of(doc):
    """Extract the accountCode from either the nested production shape
    (loginCredentials.accountCode) or the legacy top-level shape that
    earlier test fixtures used. Returns None if neither path resolves."""
    return _dig(doc, "loginCredentials.accountCode") or doc.get("accountCode")

log = logging.getLogger("migrate_account_split")


@dataclass
class MigrationResult:
    """The result of one migration invocation. Returned to the CLI for
    logging and to tests for assertions."""
    skipped: bool
    docs_migrated: int        # docs newly inserted into idp.account
    docs_already_in_idp: int  # docs that already existed in idp.account (idempotency hit)
    docs_unset: int           # docs from which auth fields were $unset in xpmile.account


def get_schema_version(client: MongoClient) -> int:
    """Return the current xpmile schema_version, or 0 if absent."""
    doc = client["xpmile"]["schema_version"].find_one({"_id": "current"})
    if not doc:
        return 0
    return int(doc.get("version", 0))


def set_schema_version(client: MongoClient, version: int) -> None:
    """Set the xpmile schema_version doc; upsert."""
    client["xpmile"]["schema_version"].update_one(
        {"_id": "current"},
        {"$set": {"version": version}},
        upsert=True,
    )


def migrate(client: MongoClient, *, dry_run: bool = False) -> MigrationResult:
    """Split the xpmile.account collection along the auth/business boundary.

    Idempotent. Safe to run repeatedly; the schema_version gate short-circuits
    after the first successful run.
    """
    current = get_schema_version(client)
    if current >= SCHEMA_VERSION_TARGET:
        log.info("xpmile.schema_version is already %d (target: %d); nothing to do",
                 current, SCHEMA_VERSION_TARGET)
        return MigrationResult(skipped=True, docs_migrated=0,
                               docs_already_in_idp=0, docs_unset=0)

    log.info("migrating xpmile.account → idp.account "
             "(current schema_version=%d → target=%d)",
             current, SCHEMA_VERSION_TARGET)

    docs_migrated = 0
    docs_already_in_idp = 0
    docs_unset = 0

    xpmile_accounts = client["xpmile"]["account"]
    idp_accounts = client["idp"]["account"]

    for doc in xpmile_accounts.find({}):
        account_code = _account_code_of(doc)
        if not account_code:
            log.warning("xpmile.account doc %s has no accountCode "
                        "(looked at loginCredentials.accountCode and "
                        "top-level accountCode); skipping",
                        doc.get("_id"))
            continue

        # Build the flat target idp.account doc from the (possibly
        # nested) source paths.
        idp_doc = {"accountCode": account_code}
        for flat_name, source_paths in AUTH_FIELD_SOURCES.items():
            for path in source_paths:
                v = _dig(doc, path)
                if v is not None:
                    idp_doc[flat_name] = v
                    break

        # Insert into idp.account if the corresponding doc isn't there yet.
        existing = idp_accounts.find_one({"accountCode": account_code})
        if existing:
            docs_already_in_idp += 1
            log.debug("idp.account already has accountCode=%s; not re-inserting",
                      account_code)
        else:
            if dry_run:
                log.info("[dry-run] would insert idp.account: %s", idp_doc)
            else:
                idp_accounts.insert_one(idp_doc)
                log.info("inserted idp.account: accountCode=%s", account_code)
            docs_migrated += 1

        # $unset the auth fields from xpmile.account (always — idempotent
        # because $unset on a missing field is a no-op at the Mongo layer).
        # Only loginCredentials.passwordHash leaves; role/name/email stay
        # in personalInfo for marvel views.
        unset = {field: "" for field in UNSET_FROM_XPMILE
                 if _dig(doc, field) is not None}
        if unset:
            if dry_run:
                log.info("[dry-run] would $unset on xpmile.account accountCode=%s: %s",
                         account_code, list(unset.keys()))
            else:
                xpmile_accounts.update_one({"_id": doc["_id"]},
                                            {"$unset": unset})
                log.debug("$unset on xpmile.account accountCode=%s: %s",
                          account_code, list(unset.keys()))
            docs_unset += 1

    if dry_run:
        log.info("[dry-run] would set xpmile.schema_version to %d",
                 SCHEMA_VERSION_TARGET)
    else:
        set_schema_version(client, SCHEMA_VERSION_TARGET)
        log.info("xpmile.schema_version set to %d", SCHEMA_VERSION_TARGET)

    return MigrationResult(
        skipped=False,
        docs_migrated=docs_migrated,
        docs_already_in_idp=docs_already_in_idp,
        docs_unset=docs_unset,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Split xpmile.account into idp.account + xpmile.account.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="See docs/design/inhouse-idp/inhouse-idp-design.md §1 and §11 Phase pre-A.",
    )
    parser.add_argument("--mongo-uri", required=True,
                        help="MongoDB connection URI "
                             "(e.g. mongodb://xpmile:xpmile_pass@localhost:27017/?authSource=admin)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Log what would happen; don't modify any data.")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="DEBUG-level logging.")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-8s %(name)s: %(message)s",
    )

    try:
        client = MongoClient(args.mongo_uri)
        # Connectivity probe: fails fast on a bad URI rather than partway through.
        client.admin.command("ping")
    except Exception as exc:  # pragma: no cover (real network)
        log.error("could not connect to MongoDB: %s", exc)
        return 1

    try:
        result = migrate(client, dry_run=args.dry_run)
        log.info("done. skipped=%s docs_migrated=%d docs_already_in_idp=%d docs_unset=%d",
                 result.skipped, result.docs_migrated,
                 result.docs_already_in_idp, result.docs_unset)
        return 0
    except Exception as exc:  # pragma: no cover (defensive)
        log.error("migration failed: %s", exc, exc_info=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
