# FinRenamer — project briefing

Read this first. It records what the program does, every decision made so far
(and why), how the code is organized, the conventions to follow, and what's next.

## What it is

A Windows desktop app (C++20, Qt 6 Widgets) for a user at work who receives bulk
financial statements as PDFs and has to rename and organize them per client case.
Everything is local: no network, data stored on the user's own machine.

Workflow: create a **case** (client) → it has **people** and **accounts** →
open the **rename screen**, pick a folder of PDFs → for each file choose an
account and a date (or skip it) → preview → **Apply** renames (and optionally
sorts into subfolders) → **Undo** if needed.

## The user and environment

- Intermediate C++ developer; comfortable with CMake and libraries.
- Windows, **Visual Studio 2022** (MSVC 19.44), CMake 3.31 bundled with VS.
- **Qt 6.12.0** at `C:\Qt\6.12.0\msvc2022_64`, with the **Qt PDF** add-on.
- **Inno Setup 6** for the installer.
- The project was developed with Claude in a Linux sandbox (GCC 13, Qt 6.12
  built from source without Qt PDF) and built/run by the user in Visual Studio.
  Anything Windows-only or Qt-PDF-only could only be compile-checked there, so
  the user is the first to run it — ask for the VS Output window when something fails.

## Filename rules (decided with the user — don't change without asking)

    YYYY.MM.DD <Institution> <Type> <Last4> (<Owner>; <Owner>).pdf

- **Date modes** (per file):
  - Single date → `2026.01.31`
  - Period → `2026.01.01 - 2026.01.31`
  - Quarter → `2026.Q1` (user chose this over `2026.03.31 Q1`, accepting that it
    sorts after dated files in Explorer)
- **Institution** uses the account's *institution display name* if set
  (e.g. "Bank of America" → `BofA`), else the full institution name.
- **Account type** is free text, autocompleted from types used before.
- **Owners**: each person's *display name* (e.g. `H`, `W`, `J`), in the account's
  owner order, joined with `"; "` → `(H; W)`. No owners → no parentheses.
- Windows-illegal characters are cleaned: `/ \ : |` → `-`; `< > " ? *` removed;
  whitespace collapsed; trailing dots/spaces trimmed.
- Example: `2026.01.31 BofA Checking 1234 (J).pdf`
- **Account numbers**: an account has one or more numbers (last four), newest
  first; the top one is current (`lastFour`), the rest are `previousLastFour`.
  Edited in the account dialog as one ordered list (Add Newer Number, Newer/Older
  to reorder, Remove, double-click to correct). File names use the number the
  statement shows, chosen per file with the rename screen's **Number** dropdown
  (next to the account dropdown; enabled when the account has >1 number); they
  never show "was". The **account folder** shows the older ones:
  `Chase Credit Card 9012 (was x5678, x1234) (H)`.

## Subfolder sorting

- Optional: by account, by year, or both (order: Account→Year or Year→Account).
- Account folder name = the **full label** (same as the filename minus the date),
  plus `(was x...)` after the number when the account has previous numbers
  (`accountFolderLabel()`): `BofA Checking 1234 (J)`, `Chase Credit Card 5678 (was x1234) (H)`.
- Year folder = the date's year; **periods use the end date's year**; quarters their own year.
- Folders are created only when a file goes into them; undo removes only folders
  that batch created, and only if empty.

## Other decisions

- Every new case starts with people **Husband (H), Wife (W), Joint (J)**;
  they're ordinary people (rename/delete freely).
- People have a **full name** and a **display name** (blank = full name). Both
  unique per case, case-insensitively, so two people can't produce the same filename.
- Institution display names are remembered: typing a known institution autofills
  its last-used abbreviation (any case); the user's own typing is never overwritten.
- **Skip**: a per-file option to leave a file as is. Skipped files are *not*
  remembered between sessions (user wants to re-check them on a second pass).
- **Partial apply** is allowed: rows not filled in are left alone; applied rows
  turn "Renamed", grey and locked; the user carries on.
- **Carry-forward**: pressing Enter / Next File copies the account and the *next*
  date to the next file if it's blank (month-ends stay month-ends; periods shift by
  their own length; quarters roll over). Deliberately **not** on mouse clicks, so
  browsing never silently fills rows.
- **Refresh button** (not automatic) to pick up PDFs added to the folder; new
  files are appended at the end, entries are kept.
- Name collisions: default **append " (2)"**; option to block instead. Never overwrite.
- Dates are typed in **separate Year / Month / Day boxes** (filename order; user's
  request, replacing one typed field). Typing advances by itself (`2026` -> Month;
  `01`, or `1` + `/`/`.`, or a single digit 2-9 -> Day); `26` becomes `2026`, `1` -> `01`
  on leaving a box; Backspace in an empty box / arrows at the edge move between boxes.
  Pasting a whole date into any box fills all three (`parseUserDate`: `1/31/2026`,
  `013126`, `2026-01-31`, plus `20260131`). Red border + `problem()` text for an
  impossible date ("February 2026 has no day 30."). After carry-forward, focus goes to
  the Month box (the year rarely changes).
- Only the **last four** digits of account numbers are ever stored.
- Data: `%APPDATA%\FinRenamer\finrenamer.db` (SQLite) and `settings.ini`
  (last folder, sort options, splitter sizes, preview zoom).

- **Correcting a number** (editing an existing entry's text in the numbers list) is
  a typo fix: the dialog passes `numberCorrections` (old -> new) to `updateAccount`,
  and files named with the old text get the corrected number. Adding, removing or
  reordering numbers needs no question (the user rejected a typo-or-new-number popup).
- After **any account or person edit that changes a file or folder name** (institution,
  display name, type, numbers or their order, owners' display names), a popup asks
  "Update them now?" and opens Update File Names. Detected by comparing all the case's
  names before/after the edit (`MainWindow::currentNames`).
- **Update File Names** (main window button; also from that popup): searches a *list of
  folders* (typos can span several sessions/folders; Add Folder..., drag from Explorer,
  Remove; the list is remembered per case in settings.ini under
  `updateNames/case<id>/folders`), optionally including subfolders. Everything is
  renamed **in place -- nothing is ever moved to another folder** (user's requirement):
  files `<date> <old label>[ (n)].pdf` get the current label in the same folder (` (2)`
  if taken); with "Also rename account folders", a folder named with an old/other label
  of an account is renamed where it is. If a folder with the new name already exists
  next to it, it is **left alone** (Collision) -- folders are never merged. One undoable
  batch. Old names are known from `account_name_history`, recorded by
  `updateAccount`/`updatePerson` -- so it only recognises names changed after v4; any
  file label of an account (current or previous number) used as a folder name is also
  recognised.

## Layout of the code

    CMakeLists.txt          options FINRENAMER_BUILD_APP / FINRENAMER_BUILD_TESTS
    CMakePresets.json       "vs2022" configure preset (Qt path), debug/release build presets
    core/                   static library, NO Qt -- all logic, unit-tested
      Models.h              ClientCase, Person{fullName, displayName}, Account
      DateSpec              variant<SingleDate, Period, Quarter>; format, validate,
                            filingYear, nextDateSpec (carry-forward), parseUserDate
      FilenameBuilder       sanitizeComponent, accountLabel, buildFilename, validateAccount
      FolderPlanner         subfolderFor(account, date, SortOptions)
      FileScanner           listPdfFiles (top level, case-insensitive .pdf, sorted)
      NameFixer             planNameFixes(roots, accounts, oldNames, options): in-place renames
      RenamePlan            buildPlan(): preview, read-only. Statuses: Ready, Unchanged,
                            Skipped, Incomplete, Collision, Error
      RenameEngine          execute(plan) -> BatchRecord; removeEmptiedFolders(record);
                            undo(record) (recreates removed parent folders)
      RenameSession         rename-screen state: rows, carry-forward, refresh,
                            apply (partial), undoLast
      Database              SQLite via SQLiteCpp (pimpl; not exposed in headers)
      Utf8Path              pathFromUtf8 / utf8FromPath / utf16Length / caseFoldKey
    app/                    Qt Widgets GUI (static lib finrenamer_ui + FinRenamer.exe)
      main.cpp              opens the DB in AppData, shows MainWindow
      MainWindow            cases list | people table + accounts table; File menu
      CaseDialog, PersonDialog, AccountDialog
      RenameWindow          the rename screen (table + editor left, preview right)
      DateWidgets           DateField (Year/Month/Day boxes + calendar), DateSpecEditor (mode + inputs)
      PdfPreview            QPdfView preview; compiled only with FINRENAMER_HAVE_QTPDF
      HistoryDialog         File > Rename History, undo any batch
      UpdateNamesDialog     Update File Names: folder list, preview, apply
      QtHelpers.h           qstr/stdstr/toPath/qpath, settingsFile, personLabel,
                            disconnectChildren, runGuarded
    tests/                  Catch2 v3 (78 tests): core + Database + RenameSession + NameFixer
    installer/FinRenamer.iss  Inno Setup script

Dependencies: Qt 6.12 (Widgets, optional Pdf/PdfWidgets), SQLiteCpp 3.3.3 and
Catch2 3.7.1 (vcpkg if present, else FetchContent from GitHub).

## Conventions (follow these)

- **Core has no Qt.** Logic goes in `core/` with Catch2 tests; the GUI stays thin.
- **All strings are UTF-8.** Never build a `std::filesystem::path` from a plain
  `std::string` (Windows would use the ANSI code page): use `pathFromUtf8` /
  `utf8FromPath`, or `ui::toPath` / `ui::qpath` in the GUI. MSVC builds with `/utf-8`.
- **Compare paths with `caseFoldKey`** (Windows is case-insensitive; on Windows it
  uses `LCMapStringEx` uppercase, matching NTFS).
- **Schema changes**: append a new entry to `kMigrations` in `Database.cpp` and bump
  `kSchemaVersion`; never edit a shipped migration. Add a test that upgrades a
  database in the previous format (see "A version 1 database is upgraded").
  Current schema version: **4** (v2 added people.display_name, v3 added
  accounts.institution_display, v4 added account_previous_numbers and
  account_name_history).
- **Database errors**: rule violations throw `DatabaseError` with a message meant
  for the user; GUI calls go through `ui::runGuarded(this, [&]{ ... })`.
- **Widget destructors**: any widget that connects its children's signals to itself
  calls `ui::disconnectChildren(this)` first in its destructor (see Lessons).
- **Aggregate orders** (brace-initializers must follow them; new fields go at the END
  so existing initializers keep working):
  `Account{id, caseId, institution, institutionDisplay, accountType, lastFour, owners, previousLastFour}`,
  `AccountRecord{id, caseId, institution, institutionDisplay, accountType, lastFour, ownerIds, previousLastFour}`,
  `PlanInput{source, account, date, skip, number}`.
- `updateAccount(record, numberCorrections)`: history rows store, for each old file
  name, the number files with that name should **now** use (corrected text, or the
  same number); the fixer falls back to the current number if the account no
  longer has it.
- **Any DB update that can change an account's names must record the old names**
  (`Impl::recordOldNames(before, after)` inside the same transaction), or Update File
  Names won't find files named the old way.
- Key rename-screen widgets have `objectName`s ("files", "account", "number", "date",
  "singleDate" (its boxes "year", "month", "day"), "quarter", "quarterYear", "skip", "next", "apply", "undo",
  "summary", "byAccount", "byYear", "preview") for automated GUI tests.
- The PDF preview reads the file into memory (QBuffer) so it never locks the file
  against renaming.

## Lessons learned (bugs already hit)

- **Close-time crash with Qt PDF**: `QPdfDocument`'s destructor calls `close()`,
  which emits `statusChanged`/`pageCountChanged`. Children are destroyed after the
  parent's destructor, so those signals hit a half-destroyed `PdfPreview`
  ("Called object is not of the correct type" assert in Debug). Fixed by
  `disconnectChildren` and deleting the view/document in `~PdfPreview`.
- `std::filesystem::rename` silently replaces an existing file on Windows — the
  engine re-checks the destination right before every move.
- Qt's offscreen platform + no Qt PDF in the sandbox means Qt-PDF code is only
  compile-checked against the real headers; runtime issues surface on Windows.
- GUI test harnesses that answer modal dialogs from a timer must (a) never start a
  modal from inside a step synchronously (defer with `QTimer::singleShot(0, ...)`)
  and (b) guard steps against re-entry, since a step that processes events lets
  the timer fire again.

## Build / test / ship

- Develop: open the folder in VS, preset "Visual Studio 2022 x64 (Qt 6.12)".
  A post-build `windeployqt` makes `build/app/Debug/FinRenamer.exe` runnable.
- Tests: Test Explorer, or `ctest --preset debug`.
- Installer: bump `project(... VERSION x.y.z)`, Release config, build target
  **installer** → `installer/Output/FinRenamer-Setup-x.y.z.exe`. It installs
  per-user without admin (`PrivilegesRequired=lowest`), upgrades in place (fixed
  `AppId`), bundles the MSVC runtime DLLs (no vc_redist), never touches AppData.

## Status and roadmap

Done: core logic, database, case/people/account management, rename screen
(carry-forward, skip, sorting, partial apply, undo last, refresh), rename history,
PDF preview, installer, previous account numbers, Update File Names.

Ideas discussed, not built yet (user's choice of order):
1. Real-world trial on copies of client folders; fix friction found.
2. Auto-suggest account and date from the PDF's text (Qt PDF can extract text):
   match an account's last four, find statement date/period; suggestions the user
   confirms, never silent. Scanned PDFs would need OCR (Tesseract) later.
3. App icon (.ico + resource file); code signing (ask IT) to avoid SmartScreen.
4. Database backup/restore; put the project in git; turn the GUI click-through
   tests (previously run outside the repo) into Qt Test tests in `tests/`.
5. Sharing cases across a team would need a different storage design (SQLite on a
   network share is unreliable) — currently each user has their own database.
