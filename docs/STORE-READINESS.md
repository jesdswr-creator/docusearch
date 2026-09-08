# Microsoft Store Publishing Guide & Readiness

**Updated:** 2026-09-08 · app version 1.7.19 · goal: **publish at no
upfront cost, sell at ~$9**

---

## Verdict: READY TO PACKAGE — 3 manual steps from submission

Everything repo-side is done: CI now builds the unsigned `.msix`
(`DocuSearch-msix-store` artifact) with the AI model, Qt DLLs, PDFium,
OCR helper and manifest assets verified inside. Publishing itself is
**free** — Microsoft removed the developer registration fee for
individual accounts (Sept 2025, ~200 markets) **and** for company
accounts (May 2026). What remains is account work only you can do.

| Area | Status |
|------|--------|
| MSIX packaging (CI) | 🟢 `DocuSearch-msix-store` artifact every build |
| MSIX packaging (local) | 🟢 `build-release.ps1 -MakeMsix` → same `scripts/build-msix.ps1` |
| AI model bundled | 🟢 `model.onnx` + ONNX Runtime ship in ZIP/MSI/MSIX (verified in CI) |
| Manifest / capabilities | 🟡 Placeholder identity → paste Partner Center values at submission |
| Restricted capabilities | 🟡 `runFullTrust` + `broadFileSystemAccess` need a justification text |
| Privacy / licensing docs | 🟢 Exist in-repo · 🟡 host PRIVACY.md at a public URL for the listing |
| Licensing stack | 🟢 PDFium (Apache-2.0/BSD), Windows.Media.Ocr (public API), BGE (MIT) — nothing GPL, nothing Microsoft-proprietary redistributed |
| Code signing | 🟢 Not needed — the Store re-signs MSIX during publishing |
| Qt LGPLv3 | 🟡 One legal decision to make (see §D) |
| Partner Center account | 🔴 Register (free) — §A |
| Store listing assets | 🔴 Screenshots, 1:1 box art, IARC questionnaire — §C |

---

## A. Your 3 manual steps (all free)

1. **Register a Partner Center developer account**
   <https://partner.microsoft.com> (or storedeveloper.microsoft.com).
   - **Individual**: free (was $19; fee waived, ID + selfie verification
     instead of credit card).
   - **Company**: free since May 2026 (was $99).
   - Which one? Policy 10.14: individual is fine for a solo developer;
     use a company account if you sell as a business/brand. Both can
     publish paid apps.
2. **Reserve the app name** "DocuSearch" → Product management →
   **Product identity** → copy the assigned `Identity Name` (a number or
   code) and `Publisher` (CN=...) into `installer/AppxManifest.xml`
   — or, without editing the manifest, pass them to the packager:
   ```powershell
   .\scripts\build-msix.ps1 -IdentityName "<PC Identity Name>" `
                            -IdentityPublisher "<PC Publisher>"
   ```
   An upload with the placeholder identity (`DocuSearch.DocuSearch` /
   `CN=DocuSearch, O=DocuSearch, C=US`) is rejected.
3. **In the submission**: write the two restricted-capability
   justifications, fill the IARC age-rating questionnaire, host
   `PRIVACY.md` content at a public URL (GitHub Pages is fine) and paste
   that URL into the listing.

**Capability justification texts to adapt:**
- *runFullTrust*: "DocuSearch is a classic Win32 desktop application
  (C++/Qt). The full-trust capability is inherent to packaging a desktop
  app; it accesses only files in folders the user explicitly adds to the
  index."
- *broadFileSystemAccess*: "DocuSearch is a local document indexer. The
  user chooses which drives/folders to index; the app reads those files
  to build an offline search index stored in %APPDATA%. Nothing is
  transmitted off the machine; no network capability is declared. The
  user can revoke access any time in Settings → Privacy → File system."

## B. Selling at $9 — how the money works

**1. Price tiers.** Partner Center pricing uses a fixed tier list — you
cannot enter a flat $9.00. The nearest tiers are **$8.99** and
**$9.99** (USD base price; other markets localize automatically).

**2. Commerce option 1 — Microsoft commerce (recommended, simplest).**
You keep **85%** for apps (Microsoft's cut is 15%; games are 88/12):

| Tier | You receive per sale |
|------|---------------------|
| $8.99 | ≈ $7.64 |
| **$9.99** | **≈ $8.49** |

Buyers pay in their local currency with cards the Store already has.
This is the zero-extra-work path: set the base price tier in
Pricing → Availability and you're done.

**3. Commerce option 2 — own commerce (keep 100%).** Since 2021 the
Windows Store lets app developers bring their own/third-party payment
system and pay Microsoft nothing. In practice that means: list the app
**free** in the Store, sell license keys on your own website
(Razorpay/Stripe/Paddle etc.), unlock the app with the key. You keep
the full $9 — but you build the license/checkout flow, handle
refunds/GST invoicing yourself, and the Store listing shows "Free"
(which reduces impulse purchases). Recommended only if you already have
a payment setup.

**4. Payout setup (do this during registration).** Complete the tax
interview (for India: W-8BEN, India–US treaty claim) and bank details
(SWIFT transfer). Microsoft pays monthly once earnings cross the
payout threshold. Note: the user in you — `pricepush`-style price
localization — is automatic; the base tier maps to local tiers per
market.

**5. Free trial option** (worth considering at $9): Partner Center lets
you offer a trial period — but DocuSearch has no trial/license logic in
code. Without it, the choice is a straight paid listing (option 1) or
free + external key (option 2).

## C. Listing material to prepare (free)

- **Screenshots**: 4–10 (main window with results, OCR in action,
  AI/semantic search, duplicates finder, dark mode). 1366×768+.
- **Box art**: 1:1 logo (required), 2:3 poster (recommended).
- **Description** ≤ 10 000 chars; lead with "100% offline — your files
  never leave your PC".
- **Notes for certification** (≤ 2 000 chars): offline architecture,
  why broadFileSystemAccess is needed, no network capability declared.
- **Applicable license terms**: paste the BSD-3-Clause text (the MSI
  EULA wizard doesn't run for Store installs).
- **IARC questionnaire**: trivial for an offline productivity tool
  (expect Everyone / 3+).

## D. The one legal decision: Qt LGPLv3

Selling LGPL-linked software is allowed. The nuance: LGPLv3 requires
users to be able to replace the Qt libraries. The portable ZIP satisfies
this (DLLs sit next to the exe). The Store package lives under
`C:\Program Files\WindowsApps` with TrustedInstaller-only ACLs, so users
can't swap DLLs there. Two defensible positions:

1. **Rely on the open-source release** — the full source is public; a
   user can rebuild the app with modified Qt. Commonly argued, slightly
   gray for the Store-locked binary.
2. **Buy the Qt Small Business license** — flat annual fee (for
   companies/devs under $250k revenue), removes all ambiguity, and lets
   you statically link if you ever want to.

At a $9 price point, option 1 is the common indie practice; revisit
option 2 if revenue justifies it.

## E. Suggested submission order

1. Tag a release → CI produces `DocuSearch-msix-store` (v1.7.19 or the
   next tag).
2. Register Partner Center account (free) + complete tax/payout setup.
3. Reserve "DocuSearch"; stamp identity via `-IdentityName/
   -IdentityPublisher`; rebuild.
4. Install that exact MSIX on a clean Windows 10 **and** Windows 11 VM:
   launch → index → extract → OCR → search → AI search → uninstall →
   upgrade over v1.7.19. (Optional: run Windows App Cert Kit
   `appcert.exe` against it.)
5. Host PRIVACY.md publicly; prepare listing assets (§C).
6. Submit with the two capability justifications + price tier $9.99.
7. Expect certification in a few business days; restricted-capability
   review may add time. If broadFileSystemAccess is initially declined,
   respond with the justification text and reference to the app's
   offline indexer purpose.

---

## Changelog of this assessment

- **2026-09-08 (b)**: CI now builds the MSIX (`build-msix.ps1` shared by
  CI and build-release.ps1); corrected earlier claim — the BGE model and
  ONNX Runtime ARE bundled in all distributions; added §B pricing
  analysis for $9; registration fees confirmed free for both account
  types; Poppler→PDFium doc drift fixed (README/BUILD/INSTALL/HELP/FAQ
  sources); MSIX stage now excludes test binaries + redist bootstrappers
  and cleans the stage dir.
- **2026-09-08 (a)**: initial assessment.

*Sources: Microsoft Learn — App package requirements (MSIX), Get
started with the Microsoft Store FAQ, File access permissions
(restricted capabilities), Free developer registration for individual
developers, Store Policies v7.19; Microsoft Store commerce announcements
(own commerce = 100% for apps, 2021); registration-fee news Sept 2025
(individuals) / May 2026 (companies); docs/AUDIT-2026-09-02.md.*
