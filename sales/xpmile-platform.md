# xpmile — Last Mile Delivery Platform

**A hybrid courier-operations platform. Your data stays on your premises. You pay a predictable monthly fee for the cloud-hosted UI. No per-shipment charges, ever.**

---

## At a glance

| | |
|---|---|
| **What it is** | An end-to-end last-mile shipping platform: AWB generation, shipment lifecycle, tracking, manifests, driver runs, invoicing, inventory. |
| **Who it's for** | Courier / 3PL / last-mile delivery operators of any size who want enterprise software without enterprise lock-in. |
| **Deployment model** | Hybrid: web UI + business logic in the cloud; **your shipment database lives on your premises**. |
| **Pricing model** | Flat **monthly subscription** for the cloud-hosted application. **Zero per-shipment fees**, regardless of volume. |
| **Cloud platform** | Heroku-hosted by us — production-grade TLS, automated releases, zero infrastructure burden on you. |
| **On-prem footprint** | One small container (`wsdbagent`) + MongoDB on a machine you control. No public IP needed. |

---

## What you get

A complete operations console:

- **Shipping** — Single, bulk (Excel upload), alternate-reference bulk, third-party (Ajoul-style integrations). Auto-generated AWB numbers per sender prefix.
- **Tracking** — Single-shipment and multi-AWB tracking with full activity timeline; status update flow; email notifications.
- **Inventory** — Inbound / outbound / find / update with on-the-fly A10 barcode label generation (PDF, prints to standard label printers).
- **Drivers & DRS** — Collection workflow with driver assignment; Delivery Run Sheet (DRS) PDFs with embedded CODE128 barcodes.
- **Accounting** — Customer, employee, and admin account management; quoted-amount + VAT + IBAN fields; role-based permissions.
- **Reporting** — Detailed shipment reports; invoice generation with PDF export.
- **Live dashboards** — Today-only operational pulse (LIVE strip in the header) + monthly stats dashboard with month picker. Agent/DB connectivity badges.
- **Labels & documents** — A2 / A4 / A6 / A10 PDF labels; production-grade barcode rendering (JsBarcode + pdfMake).
- **Multi-role** — Admin, Employee, Customer roles with appropriate data scoping (Customer sees only their own shipments).

---

## The hybrid deployment model

```
   ┌──────────────────────────────────────────┐
   │ Customer / Driver / Operator (browser)   │
   └─────────────────┬────────────────────────┘
                     │  HTTPS  (Heroku-edge TLS)
                     ▼
   ╔══════════════════════════════════════════╗
   ║  CLOUD  (Heroku — managed by xpmile)     ║
   ║                                          ║
   ║   Angular UI  +  C++ Web Service          ║
   ║   ─────────────────────────────────       ║
   ║   No customer shipment data resides here. ║
   ║   Stateless application tier.             ║
   ╚════════════════╤═════════════════════════╝
                    │  WebSocket  +  inner mTLS
                    │  (encrypted DB tunnel)
                    ▼
   ╔══════════════════════════════════════════╗
   ║  YOUR PREMISES                            ║
   ║                                           ║
   ║   wsdbagent (small container)  ──►  Mongo ║
   ║                                           ║
   ║   All shipment data lives here.            ║
   ║   You back it up. You audit it.            ║
   ║   You can pull the plug any time.          ║
   ╚══════════════════════════════════════════╝
```

The `wsdbagent` is a small daemon (~10 MB image) that runs on the same machine as your MongoDB. It opens an outbound WebSocket connection to the cloud — **no inbound firewall holes, no public IP required**. All DB traffic between cloud and your premises is double-encrypted: outer TLS to the Heroku edge, plus a second TLS tunnel layered inside.

---

## Why hybrid? The three differentiators

### 1. Your data stays on your premises

Shipment records — sender, receiver, COD amounts, tracking history, customer addresses — sit in **your** MongoDB on **your** hardware. The cloud tier is stateless: it reads through to your DB on every request via the encrypted tunnel.

- No multi-tenant DB to be exposed in someone else's breach.
- No vendor data export disputes if you ever leave — the data is already yours, in your machine.
- Backups and retention policy are entirely under your control.

### 2. No per-shipment charges, ever

Most courier-software vendors charge $0.05 – $0.20 *per AWB* or *per tracking event*. At 10,000 shipments/month that's $500–$2,000 in pure volume fees, growing as you grow.

**xpmile is a flat monthly fee.** Scale from 1,000 to 100,000 shipments without renegotiating, without budget surprises.

### 3. Predictable monthly cost

You pay one subscription line item per month for the cloud application. We absorb:
- Heroku hosting costs
- TLS certificate renewals
- Software updates and security patches
- Uptime monitoring

You absorb: your local server (one VM is enough), your team's training, and your MongoDB Atlas / community-edition license (the latter is free).

---

## Security architecture (what we tell your CISO)

| Layer | Protection | Implementation |
|---|---|---|
| Browser ↔ Cloud | TLS 1.2+ via Heroku edge with public-CA certificate | Browser pinning, HSTS |
| Cloud ↔ Premises | **Two layers of TLS**: outer (Heroku) + inner mTLS (xpmile private CA) | OpenSSL, mutual cert validation, CN/SAN check |
| Premises ↔ MongoDB | Loopback or local network | Customer-controlled |
| Password storage | PBKDF2-HMAC-SHA256, 600 000 iterations, random per-user salt | OpenSSL `PKCS5_PBKDF2_HMAC` |
| Password transport | Always in request body over TLS; never in URLs, query strings, or logs | Backend redaction of `password` / `accountPassword` / `passwordHash` fields before any debug-log line |
| Access control | Role-based: Admin / Employee / Customer scoping | Server-side; UI hides what the server won't serve |

The second TLS layer matters because it means **even if a Heroku operator could observe the network**, they would see only opaque ciphertext flowing between the cloud tier and your premises. The key for that inner channel is in our private CA, not Heroku's.

---

## Pricing

Flat **monthly subscription** — quoted per organisation based on user seats and support tier, **not** per shipment.

| Tier | Includes | Typical for |
|---|---|---|
| **Starter** | Up to 5 operator seats, business-hours support | Sub-1,000 shipments/month |
| **Growth** | Up to 25 seats, extended support hours, custom branding on PDF labels | 1,000 – 25,000 shipments/month |
| **Enterprise** | Unlimited seats, SLA, priority phone support, dedicated onboarding engineer | 25,000+ shipments/month, multi-branch |

Volume is irrelevant to billing. Scale freely.

> Exact figures are quoted per engagement — contact sales.

---

## Pros and cons (honest assessment)

### Pros

| Pro | Why it matters |
|---|---|
| **Data sovereignty** | Regulatory wins for GST / VAT / customs-data residency. You can answer "where is our data?" with a server-room photograph. |
| **Predictable cost** | Volume growth doesn't cost you more. Easier to budget, easier to forecast unit economics for your customers. |
| **No public-IP requirement** | The on-prem agent dials *out* to the cloud. Works behind NAT, behind a corporate firewall, on a residential ISP. |
| **No infrastructure burden** | We patch the cloud, renew certs, monitor uptime. You operate one local VM. |
| **Vendor-lock-out protection** | If you ever want to migrate, your DB is already in your hands — no export request, no waiting period. |
| **Fast onboarding** | Typical deployment: 1–2 days to install the agent and verify the tunnel; 3–5 days for staff training. |
| **Open, auditable security** | Two-layer TLS, PBKDF2 password hashing, no plaintext in logs — every claim verifiable against the codebase. |

### Cons

| Con | What it means for you | Mitigation |
|---|---|---|
| **You run *one* server** | Pure SaaS users compare us to "zero infra" providers. We're "minimal infra" — one local VM and a MongoDB process. | We supply a one-command install script (`./run.sh start remote`) and walk you through it. |
| **Local server uptime affects you** | If your on-prem machine goes down, shipment lookups fail (cloud → tunnel → your DB → no answer). The cloud tier itself stays up. | A second on-prem replica + Mongo replica set is supported; we recommend it for >5,000 shipments/month operators. |
| **Internet outage at the premises = read-through fails** | Same dependency in reverse. | The agent automatically reconnects with exponential backoff once connectivity returns. Operators can continue capturing shipments via the cloud UI; they'll just be queued until the tunnel comes back (planned, see roadmap). |
| **First-time setup needs an IT-capable person** | You'll need someone who can run a Docker container and edit a config file on the on-prem machine. | We do this with you during onboarding; after that, day-to-day operations are pure UI. |
| **Cloud-only customers want raw API access today** | Public REST endpoints exist but are documented for internal use. | A formal Partner API spec is on the 2026 roadmap; early-access available on request. |
| **WhatsApp / OAuth2 integrations are stubs** | Modules exist in the codebase but aren't production-wired. | Roadmap; we'll quote per-integration if you need them sooner. |

### What we don't try to be

- **Not a marketplace** — we don't route your shipments to other couriers. You stay in control.
- **Not a wallet / payment gateway** — COD is tracked, but settlement is your bank's job.
- **Not a CRM** — we hold the operational shipment record, not your full customer relationship.

---

## What you provide

| Need | Spec |
|---|---|
| One Linux VM on-prem | 2 vCPU / 4 GB RAM minimum; Ubuntu 20.04+ or any podman/Docker host |
| MongoDB | Community Edition (free) or Atlas. v6+. Local disk for data. |
| Outbound internet | TLS 443 to `*.herokuapp.com`. No inbound port forwarding required. |
| One IT-capable person | For initial install (~half a day). Day-to-day is just the UI. |
| Branding assets (optional) | Logo SVG/PNG, primary brand colour, label customisation requirements |

---

## Onboarding timeline (typical)

```
Day 0       │ Kickoff call — confirm tier, branding, MongoDB host
Day 1-2     │ wsdbagent installed at customer site; mTLS certs exchanged;
            │ end-to-end tunnel verified; admin account created
Day 3-5     │ Staff training (Admin, Operator, Driver roles); first
            │ test shipments end-to-end
Day 6-10    │ Pilot: 1-2 routes / 1 branch live; daily check-in
Day 11+     │ Full rollout
```

---

## Next steps

1. **Schedule a 30-minute demo** — we drive a sandbox tenant; you see every screen and try the workflows yourself.
2. **Reference call** — talk to an existing operator on the same hybrid model.
3. **Pilot proposal** — we draft a 30-day pilot with your real data on your premises. Cancel any time, no charges if you don't proceed.

Contact: `naushad.dln@gmail.com`

---

## Licensing terms

xpmile is **source-available, not open-source** — the code is on GitHub for transparency and academic review, but **commercial use requires a paid commercial licence**.

- Production deployment, hosting as a service for third parties, or integration into a commercial offering all require a signed commercial licence with xpmile.
- A monthly subscription gives you the commercial licence for the duration of the subscription, with no per-shipment fees, no volume caps, and no audit clauses beyond user-seat declarations.
- See the full text in [`LICENSE`](../LICENSE) at the repository root.

Universities and security researchers reviewing the codebase under the academic-use grant are welcome; no fee, attribution required.
