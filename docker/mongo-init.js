// Runs once on first startup (when the data volume is empty).
// Creates a scoped app user and inserts the bootstrap admin document.

const appUser = process.env.MONGO_APP_USER || 'xpmile';
const appPass = process.env.MONGO_APP_PASS || 'xpmile_pass';

// Create the app user in the admin database. Needs readWrite on BOTH
//   - xpmile  (business data: shipments, accounts as business records, …)
//   - idp     (in-house IdP auth state: idp_signing_keys, idp_clients,
//              sessions, idp_codes, password_reset_tokens, account, …)
// Without the second role the on-prem Vaadin admin's IdP views (Phase B/J)
// blow up with `not authorized on idp to execute command`, the
// wsdbagent-idp container can't read/write its db, and the seed-default-
// idp-sso.sh script's upserts fail. Per-deployment alternative: drop the
// idp role and grant a separate user — but the two databases live on one
// mongod and share a single agent stack, so one user with both roles is
// the simplest correct shape.
db = db.getSiblingDB('admin');
db.createUser({
  user: appUser,
  pwd:  appPass,
  roles: [
    { role: 'readWrite', db: 'xpmile' },
    { role: 'readWrite', db: 'idp'    }
  ]
});
print(`App DB user '${appUser}' created with readWrite on 'xpmile' + 'idp'`);

// Seed the application database.
db = db.getSiblingDB('xpmile');
db.account.insertOne({
  isAccountCodeAutoGen: false,
  awbPrefix: "AWB",
  loginCredentials: {
    accountCode: "admin",
    passwordHash: "$pbkdf2-sha256$i=600000$jikaUK2A+Sqzyn4S5QrJtA==$o2sULiR0v3WmF0J8qLbZu2c5TrexkV0Bs8pK/3Bfy2I="
  },
  personalInfo: {
    role: "Admin",
    name: "Administrator",
    contact: "",
    email: "",
    address: "",
    city: "",
    state: "",
    postalCode: "",
    eventLocation: "UAE"
  },
  customerInfo: {
    companyName: "",
    quotedAmount: "",
    tradingLicense: "",
    vat: "",
    currency: "UAE - Dirham",
    bankAccountNumber: "",
    iban: ""
  }
});
print("Bootstrap admin document created — accountCode: admin (passwordHash: pbkdf2-sha256)");

// SSO collections (docs/design/sso/sso-design.md §10, §13).
// sso_config holds a single document — the source of truth for SSO providers,
// managed by the on-prem Vaadin admin UI and hot-reloaded by the C++ backend.
// Seeded empty: an empty publicBaseUrl keeps SSO disabled until an operator
// configures it.
db.sso_config.insertOne({ publicBaseUrl: "", providers: [] });
print("SSO config document seeded — sso_config (empty)");

// Server-side sessions and one-time OIDC transactions. The expiry check at
// lookup time is authoritative; these TTL indexes are best-effort cleanup.
db.sessions.createIndex({ expiresAt: 1 }, { expireAfterSeconds: 0 });
db.sso_transactions.createIndex({ expiresAt: 1 }, { expireAfterSeconds: 0 });
print("SSO collections indexed — sessions, sso_transactions (TTL on expiresAt)");
