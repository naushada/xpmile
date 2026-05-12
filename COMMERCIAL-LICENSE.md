# Commercial Licensing

xpmile is **dual-licensed**. Pick whichever fits your situation:

1. **GNU Affero General Public License v3.0** (default) — see [`LICENSE`](LICENSE). Free, copyleft, with a network-use clause: if you run a modified xpmile as a network service for *anyone* (including internal users), you must offer the corresponding source code of your modifications to those users.

2. **Commercial Licence** — a separate paid agreement with the copyright holder. Use this if the AGPL's terms don't fit your deployment.

This page is a non-binding guide to which licence you probably need; the binding terms are in `LICENSE` and in any signed commercial agreement.

---

## You probably need a commercial licence if…

- **You're running xpmile as a SaaS for third parties** and you don't want to release the source code of your customisations, integrations, or operator portal modifications.
- **You're integrating xpmile** (in whole or in part) into a proprietary product or service that you sell, lease, or subscribe out.
- **Your organisation's policy prohibits AGPL** in production stacks (common at banks, healthcare, government, and most Fortune 500 companies — legal teams routinely block AGPL due to the copyleft contagion risk).
- **You want indemnification** — a contractual promise that we'll defend you if a third party claims our code infringes their IP. The AGPL explicitly provides none.
- **You want an SLA** — guaranteed uptime, response time, or escalation path. AGPL users get community-best-effort support.
- **You want priority bug fixes or features** — paid customers get a roadmap voice and faster patches.
- **You want to keep your customisations private.** Customer addresses, label templates, custom workflows — anything you build on top of xpmile and serve to users over a network — would have to be open-sourced under AGPL §13.

## You probably do NOT need a commercial licence if…

- You're a **student, researcher, or hobbyist** studying or extending the code without offering it as a service to others.
- You're running xpmile **internally for your own evaluation** and not exposing it to any users beyond yourself.
- Your **whole stack is already AGPL or AGPL-compatible** and you're happy to publish your modifications.
- You're contributing back to the upstream project rather than running a fork in production.

---

## What the commercial licence includes

A signed commercial licence with xpmile typically includes:

- **A perpetual or term licence** to run xpmile for your specific deployment (production, staging, DR).
- **No copyleft obligations** — your modifications, integrations, and operator UIs remain proprietary.
- **No per-shipment or per-event fees** — flat monthly subscription regardless of volume.
- **Support tier** scaled to your tier (Starter / Growth / Enterprise — see [`sales/xpmile-platform.md`](sales/xpmile-platform.md)).
- **Indemnification** against third-party IP claims, capped at the fees paid.
- **An audit-light arrangement** — annual self-declaration of user-seat count; no per-event auditing.
- **Custom branding** — your logo on labels, PDFs, and the UI shell.

Pricing is per engagement based on scale and support tier.

---

## How to get one

Email `naushad.dln@gmail.com` with:

1. Your organisation name and a short description of the use case.
2. Approximate monthly shipment volume (for tier sizing, not billing).
3. Number of operator seats you anticipate.
4. Deployment target — cloud-hosted by xpmile, your private cloud, or fully on-prem.

You'll get back a proposal within 1–2 business days. There's no minimum term; a 30-day pilot is the standard starting point.

---

## Frequently asked

**Can I evaluate without buying?**
Yes. The AGPL grant covers evaluation. You can run xpmile against your own data on your own hardware, indefinitely, as long as you don't make it available to other users over a network or refuse to share source modifications.

**Can my contractors / consultants use it under AGPL?**
Yes — same terms as you. The moment you put it in front of *your customers* over a network without releasing source, you've crossed into commercial-licence territory.

**Is the on-prem agent (`wsdbagent`) AGPL too?**
Yes — both the cloud `uniservice` binary and the on-prem `wsdbagent` are covered by the same dual licence.

**Can I redistribute xpmile under a different OSS licence?**
No. AGPL doesn't allow relicensing. Only the copyright holder can offer xpmile under terms other than AGPL.

**What if I'm not sure which licence I need?**
Email us; we'll talk you through it. No sales pressure — if AGPL works for you, we'll say so.
