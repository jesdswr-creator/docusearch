# DocuSearch Privacy Policy

**Effective date:** 2026-09-02

DocuSearch is an **offline** document search and OCR application for
Windows. This policy describes what data the app touches and — more
importantly — what it never does.

## The short version

DocuSearch does not collect, transmit, sell, or share any data.
It has no server. Everything stays on your PC.

## What the app processes locally

To do its job, DocuSearch reads the folders **you** choose to index and
stores the following on your own machine, in your Windows user profile
(`%APPDATA%\DocuSearch`):

* an index database (file names, paths, sizes, dates, extracted text,
  OCR text, content hashes, your tags and notes);
* semantic-search embeddings of indexed documents (if enabled);
* application settings and logs (log files are deleted automatically
  after 14 days);
* optional index backups, in the location you choose.

This data never leaves your computer. Removing a folder from Settings
deletes its data from the index; "Remove Database (Reset)" in
Settings → Backup & Restore deletes the entire index; uninstalling the
app removes its data directory.

## What the app does NOT do

* **No network communication.** The app performs no telemetry, no
  analytics, no crash reporting, no update phone-home, and no cloud
  processing. OCR runs via the Windows built-in OCR engine, locally.
* **No account.** There is nothing to sign up for or log in to.
* **No third-party data sharing.** There are no third-party SDKs,
  advertising frameworks, or trackers in the application.

## File-system permission (Microsoft Store package)

The packaged (MSIX/Store) version of DocuSearch requests the
**broadFileSystemAccess** capability so it can index folders across your
drives. Windows asks for your consent, and you can revoke it at any time
under **Settings → Privacy & security → File system**. The permission is
used solely for indexing and previewing the folders you configure.

## Crash dumps

If the application crashes, a minidump file may be written locally to
help diagnose the problem. It stays on your PC; sharing it with support
is always your choice.

## Contact

Questions about this policy: open an issue on the project repository.

## Changes

If a future version ever adds any network-connected feature, this policy
will be updated **before** that version ships, and the change will be
called out in the release notes.
