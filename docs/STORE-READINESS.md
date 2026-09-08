# Microsoft Store Readiness Assessment

**Assessed:** 2026-09-08 · app version 1.7.19 · target: paid/commercial listing in the Microsoft Store (Windows 10/11 desktop, MSIX route)

---

## Verdict: NOT YET — but close

The engineering groundwork is ~80 % done. What remains is a mix of
**(A) account/legal work only you can do**, **(B) a handful of repo-side
fixes**, and **(C) Store-listing material**. Nothing here requires a
rewrite; the architecture (Win32 + full trust, data in `%APPDATA%`,
Windows.Media.Ocr, PDFium) is fundamentally Store-compatible.

| Area | Status |
|------|--------|
| MSIX packaging | 🟡 Works locally (`build-release.ps1 -MakeMsix`), not built in CI |
| Manifest / capabilities | 🟡 Correct, but placeholder identity + 2 restricted capabilities need justification |
| Privacy / licensing docs | 🟢 PRIVACY.md + THIRD-PARTY-NOTICES.md exist; 🟡 not hosted at a public URL |
| PDF engine licensing | 🟢 PDFium (Apache/BSD-style) — no GPL problem. README's "Poppler" claim is stale |
| OCR licensing | 🟢 Windows.Media.Ocr public API — nothing redistributed |
| Data hygiene for MSIX | 🟢 All user data via `QStandardPaths::AppDataLocation` → `%APPDATA%` (virtualizes cleanly) |
| AI model | 🔴 Not bundled — "Open Model Download Page" sends users to a browser; Store listing would advertise AI search that's missing out of the box |
| Code signing | 🟢 Not needed for Store — Microsoft re-signs MSIX automatically |
| Partner Center | 🔴 No account / name reservation yet (hard blocker) |
| Store listing assets | 🔴 None (screenshots, 1:1 box art, IARC questionnaire) |

---

## A. Blockers only you can resolve (outside the repo)

1. **Partner Center developer account** (https://partner.microsoft.com).
   - Individual registration: the $19 fee is now **waived** in the new
     ID-verified flow; company accounts have a one-time fee (~$99).
   - Store policy 10.14: use a **company** account if you sell as a
     business or the publisher name reads like one.
2. **Reserve the app name**, then copy the assigned **Identity Name** and
   **Publisher** (Product management → Product identity) into
   `installer/AppxManifest.xml`. The current values
   (`DocuSearch.DocuSearch` / `CN=DocuSearch, O=DocuSearch, C=US`) are
   placeholders that only work for self-signed sideloading. The upload is
   rejected when identity doesn't match Partner Center.
3. **Restricted-capability justifications** (Submission options page).
   Both declared capabilities are restricted:
   - `runFullTrust` — routine for packaged desktop (Win32) apps; a
     one-paragraph explanation suffices and is routinely approved.
   - `broadFileSystemAccess` — **approval is discretionary.** A document
     indexer is exactly the intended use ("all files that the user has
     access to"), so the case is strong, but write a clear justification:
     user-selected folders/drives are indexed locally, nothing is
     transmitted, permission is user-revocable in Settings → Privacy →
     File system. Expect possible reviewer back-and-forth.
4. **Host the privacy policy at a public URL.** Required because the app
   accesses personal data (indexed documents). Host `PRIVACY.md` content
   (GitHub Pages / any URL) and put the link in the listing.
5. **Decide commerce**: Microsoft Store commerce takes **15 %** of paid
   app revenue; or choose own-commerce flow. Set the price tier.
6. **Qt LGPLv3 decision** (legal, not technical): selling LGPL-linked
   software is allowed, but LGPLv3 §4 requires users to be able to
   replace the Qt libraries. In an MSIX the install dir lives under
   `C:\Program Files\WindowsApps` with TrustedInstaller-only ACLs —
   users *cannot* swap DLLs there. Options:
   - buy the **Qt Small Business license** (flat annual fee, covers
     developers < $250k revenue) → zero ambiguity for the Store-locked
     package; or
   - rely on the BSD-3 open-source release: users can build a modified
     copy from source with patched Qt (the repo is public). Defensible,
     but not the clean reading of the license.
   The portable-ZIP distribution satisfies LGPL cleanly either way —
   only the *Store package* is the gray area.
7. **IARC age-rating questionnaire** during submission (policy 11.11) —
   trivial for an offline productivity tool.

## B. Repo-side gaps (fixable here)

1. **CI doesn't build the MSIX.** `build.yml` has zero MSIX steps; only
   the local `build-release.ps1 -MakeMsix` path produces one. Add a CI
   job: stage → stamp identity/version → `makeappx pack` → (optional)
   `.msixupload`. No signing needed for Store submission.
2. **Bundle the BGE model into the package** (~50 MB; the 25 GB package
   cap is irrelevant). Remove/hide the "Open Model Download Page"
   external-browser flow for the Store package — it contradicts the
   "completely offline" claim and degrades the listing. The
   `%APPDATA%/models` fallback path already works.
3. **Run the Windows App Certification Kit (WACK)** on the staged MSIX
   (optional for submission but catches banned-API/privacy failures
   before review does). Add as a CI smoke step.
4. **Don't ship install scripts inside the MSIX**
   (`scripts/download_bge_model.ps1`, `verify_setup.ps1` are currently
   staged into the package layout).
5. **Fix stale docs**: README/BUILD still say "Poppler" — the app links
   **PDFium** (Apache-2.0/BSD-style via bblanchon binaries). Update
   README, BUILD.md, FAQ. (This is *good* news: no GPL obligations at
   all in the current stack, contrary to what the README implies.)
6. **MSIX stage vs EULA**: the Store package doesn't show the MSI's EULA
   wizard — the "Applicable license terms" field in the listing must
   carry the license text instead (paste from `installer/LICENSE.rtf` /
   `LICENSE`).

## C. Store-listing material (needs making)

- Screenshots (≥1, recommend 4–10), 1:1 box-art logo (2:3 poster
  recommended), description ≤ 10 000 chars, release notes, keywords.
- "Notes for certification" (≤ 2 000 chars): explain the offline
  architecture, broadFileSystemAccess scope, and that no network
  capability is declared.
- Supported hardware notes (x64; consider an ARM64 build later).

---

## Why the hard parts are already solved

- **No CA code-signing certificate needed** — the Store re-signs MSIX
  packages with a Microsoft certificate during publishing.
- **No banned-API risk from OCR** — Windows.Media.Ocr is the public
  royalty-free WinRT API; nothing Microsoft-proprietary is redistributed.
- **No MSI needed** — the Store now also accepts MSI/EXE via URL, but that
  route *requires* your own Authenticode cert and silent-install support.
  The existing MSIX route is strictly better here.
- **Data writes are already MSIX-correct** — everything goes to
  `%APPDATA%\DocuSearch` via `QStandardPaths`; the install dir is only
  read (HELP.md, model lookup with AppData fallback).

## Suggested order of operations

1. Fix repo items B1–B6 → tag a release, download the CI-built MSIX.
2. Create Partner Center account (individual → convert to company if
   selling as a business), reserve the name, paste identity into the
   manifest, rebuild.
3. Install the MSIX from the Store-identical build on a clean Win10 +
   Win11 VM: launch, index, search, OCR, uninstall, upgrade path.
4. Host privacy policy, prepare listing assets, fill the IARC
   questionnaire, write the two capability justifications.
5. Submit. First review typically takes a few business days; the
   restricted-capability review can add time.

---

*Sources: Microsoft Learn — App package requirements (MSIX), Get started
with the Microsoft Store FAQ, App capability declarations (restricted
capabilities), Free developer registration for individual developers,
Microsoft Store Policies v7.19; docs/AUDIT-2026-09-02.md (repo).*
