// Runs once on first startup (when the data volume is empty).
// Switch to the application database and insert the bootstrap admin user.

db = db.getSiblingDB("xpmile");

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

print("Bootstrap admin user created — accountCode: admin / accountPassword: admin@123");
