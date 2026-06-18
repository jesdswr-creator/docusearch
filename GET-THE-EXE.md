# Get the .exe — Three Options

You want a working `DocuSearch.exe` you can run on Windows.
This document explains the three real options.

---

## ⚠️ Why I can't just hand you an .exe

DocuSearch is a Qt 6 + Tesseract + Poppler + SQLite application. Building
it produces a `.exe` plus ~50 Qt DLLs, Qt plugins, MSVC runtime DLLs, and
Tesseract/Leptonica/Poppler DLLs — all of which must match the compiler
(MSVC 2022), architecture (x64), and Windows version (10/11) of the
target machine.

The sandbox where I generated your source code is **Linux**. There is no
way to produce a reliable, self-contained Windows .exe of a Qt6 app from
Linux. (Cross-compiling Qt6 itself for Windows takes 4+ hours and usually
fails. Even MinGW-w64 builds of Qt apps have subtle plugin/DLL issues.)

The three real options below all involve running the actual Windows
build on a Windows machine — which is what `install.bat` does.

---

## Option 1 — GitHub Actions (no Windows PC needed) ⭐ Recommended

GitHub gives you **free Windows Server 2022 runners** for public repos.
Push the source to GitHub, and the included workflow builds the .exe +
MSI + MSIX automatically. You download the binaries from the Actions
tab — no Windows PC required.

### Steps

1. **Create a free GitHub account** if you don't have one
   (<https://github.com/signup>).

2. **Create a new public repository** (e.g. `docusearch`).

3. **Unzip** the source ZIP I gave you, then push it to GitHub:
   ```bat
   cd C:\dev\docusearch\docusearch
   git init
   git add .
   git commit -m "Initial commit: DocuSearch 1.0.0"
   git branch -M main
   git remote add origin https://github.com/YOUR-USERNAME/docusearch.git
   git push -u origin main
   ```

4. **Watch the build** at
   `https://github.com/YOUR-USERNAME/docusearch/actions`
   It takes ~30–45 minutes on the first run (vcpkg compiles Tesseract +
   Poppler from source), then ~5 minutes on subsequent runs (cached).

5. **Download the binaries** from the run page:
   - `DocuSearch-Setup-msi` artifact → unzip → double-click the .msi
   - `DocuSearch-msix` artifact → unzip → `Add-AppxPackage`
   - `DocuSearch-portable` artifact → unzip and run `DocuSearch.exe`
   - `DocuSearch-bin` artifact → the bare exe + DLLs (debugging)

6. **(Optional) Publish a Release** with downloadable binaries:
   ```bat
   git tag v1.0.0
   git push origin v1.0.0
   ```
   The release workflow fires, builds everything again, and publishes
   a polished Release page at
   `https://github.com/YOUR-USERNAME/docusearch/releases/latest` with
   the .msi, .msix, and .zip attached.

### Cost

**Free** for public repositories (GitHub gives unlimited Windows runner
minutes to open-source projects). Private repos get 2,000 free minutes
per month — about 40 DocuSearch builds.

---

## Option 2 — Run `install.bat` on a Windows PC

If you have access to any Windows 10/11 PC (even a friend's, or a
library computer), this is the simplest path.

1. **Unzip** `DocuSearch-1.0.0-source.zip` on the Windows PC.
2. **Install prerequisites** (one-time, ~30 minutes):
   - Visual Studio 2022 Community (free) — pick "Desktop development with C++"
   - vcpkg: `git clone https://github.com/microsoft/vcpkg.git C:\vcpkg && C:\vcpkg\bootstrap-vcpkg.bat`
   - Qt 6.7: `pip install aqtinstall && aqt install-qt windows desktop 6.7.0 win64_msvc2022_64 -m qtcore qtgui qtwidgets qtconcurrent qtsql qtsvg`
   - WiX v4: `dotnet tool install -g wix`
3. **Open** "x64 Native Tools Command Prompt for VS 2022" (in the Start Menu).
4. **Run**:
   ```bat
   setx VCPKG_ROOT "C:\vcpkg"
   setx QtPath "C:\Qt\6.7.0\msvc2022_64"
   :: close and reopen the terminal so the env vars take effect
   cd C:\path\to\docusearch
   install.bat
   ```
5. The `dist\` folder opens with the .msi, .msix, and .zip ready to use.

First build takes ~30 min (vcpkg compiles Tesseract/Poppler). Subsequent
builds take ~2 min.

---

## Option 3 — Cloud Windows VM

If you don't own a Windows PC and don't want to use GitHub Actions, rent
a Windows VM by the hour:

| Provider | SKU | Hourly cost | Free tier? |
|----------|-----|-------------|------------|
| **Azure** | Windows Server 2022 / B2ms | ~$0.10 | $200 credit, 12 months free |
| **AWS** | t3.medium / Windows Server 2022 | ~$0.064 | 750 hours/month free for 12 months |
| **GCP** | e2-medium / Windows Server | ~$0.07 | $300 credit, 90 days |
| **Vultr** | 4 GB / Windows Server 2022 | ~$0.036 | $100 credit |

1. Spin up a Windows Server 2022 VM (4 GB RAM minimum, 8 GB recommended).
2. RDP into it.
3. Follow **Option 2** above — install VS 2022, vcpkg, Qt, WiX, run `install.bat`.
4. Copy the resulting `dist\` folder down to your local machine.
5. Terminate the VM. Total cost: ~$1–3.

---

## What to do once you have the .msi

1. Double-click `DocuSearch-Setup-1.0.0.0.msi`.
2. Follow the wizard (accept license, choose install folder).
3. Launch DocuSearch from the Start Menu.
4. Open **Tools → Settings → Indexing**, add `D:\` (or whichever drive).
5. Add excluded folders (`D:\Movies`, `D:\Games`, etc.).
6. Click OK — indexing starts in the background.
7. Search bar is live after a few seconds (Phase 1 = filename search).
8. Content + OCR search become available as Phase 2 progresses.

### Tesseract OCR data

Download `eng.traineddata` from
<https://github.com/tesseract-ocr/tessdata/raw/main/eng.traineddata>
and drop it in `C:\Program Files\Tesseract-OCR\tessdata\` (create the
folder if needed). Or specify a custom path in **Settings → OCR**.

---

## Which option is right for you?

| If you… | Use |
|---------|-----|
| Want the binaries fast and free, no Windows PC | **Option 1** (GitHub Actions) |
| Have a Windows PC and want to iterate on the code | **Option 2** (`install.bat`) |
| Don't own a Windows PC and don't trust GitHub Actions | **Option 3** (cloud VM) |
| Want to distribute DocuSearch to others | **Option 1** with `git tag v1.0.0` for a polished Release page |

---

## Need help?

- Full build walkthrough: **`BUILD.md`** in the source ZIP
- Quick install guide: **`INSTALL.md`** in the source ZIP
- Troubleshooting: `BUILD.md` §10
- The CI workflows are at `.github/workflows/build.yml` and
  `.github/workflows/release.yml` in the source tree
