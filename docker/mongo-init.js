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
  loginCredentials: {
    accountCode: "admin",
    accountPassword: "admin@123"
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
print("Bootstrap admin document created — accountCode: admin / accountPassword: admin@123");
