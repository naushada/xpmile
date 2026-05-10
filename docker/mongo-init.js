// Runs once on first startup (when the data volume is empty).
// Creates a scoped app user and inserts the bootstrap admin document.

const appUser = process.env.MONGO_APP_USER || 'xpmile';
const appPass = process.env.MONGO_APP_PASS || 'xpmile_pass';

// Create the app user in the admin database with readWrite on xpmile only.
db = db.getSiblingDB('admin');
db.createUser({
  user: appUser,
  pwd:  appPass,
  roles: [{ role: 'readWrite', db: 'xpmile' }]
});
print(`App DB user '${appUser}' created with readWrite on 'xpmile'`);

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
