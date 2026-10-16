# FinRenamer — project briefing

Read this first. It records what the program does, every decision made so far
(and why), how the code is organized, the conventions to follow, and what's next.

## What it is

**Program name (shown to the user): "Zach's Litigation Tools"** (`ui::appTitle()`:
window titles, start-screen heading, installer name/shortcuts, setup file
`ZachsLitigationTools-Setup-x.y.z.exe`). The *internal* name stays **FinRenamer**
on purpose -- `QApplication::applicationName` (it decides `%APPDATA%\FinRenamer`, so
changing it would orphan the user's database and settings), the exe/CMake target,
namespaces and the installer's `AppId`. Don't rename those.

A Windows desktop app (C++20, Qt 6 Widgets) for a user at work who receives bulk
financial statements as PDFs and has to rename and organize them per client case.
Everything is local: no network, data stored on the user's own machine.

Workflow: create a **case** (client) → it has **people** and **accounts** →
open the **rename screen**, pick a folder of PDFs → for each file choose what it is
(a statement for an account, a file named from one of the user's own templates, or an Other file) and a date (or skip it) → preview → **Apply** renames (and optionally
sorts into subfolders) → **Undo** if needed.

## The user and environment

- Intermediate C++ developer; comfortable with CMake and libraries.
- Windows, **Visual Studio 2022** (MSVC 19.44), CMake 3.31 bundled with VS (the project needs 3.28+).
- **Qt 6.12.0** at `C:\Qt\6.12.0\msvc2022_64`, with the **Qt PDF** add-on.
- **Inno Setup 6** for the installer.
- The project was developed with Claude in a Linux sandbox (GCC 13, Qt 6.12
  built from source without Qt PDF) and built/run by the user in Visual Studio.
  Anything Windows-only or Qt-PDF-only could only be compile-checked there, so
  the user is the first to run it — ask for the VS Output window when something fails.

## Filename rules (decided with the user — don't change without asking)

    YYYY.MM.DD <Institution> <Type> x<Last4> (<Owner>; <Owner>).pdf

- **Date modes** (per file):
  - Single date → `2026.01.31`
  - Period → `2026.01.01 - 2026.01.31`
  - Quarter → `2026.Q1` (user chose this over `2026.03.31 Q1`, accepting that it
    sorts after dated files in Explorer)
  - **Month and day are optional** in a *single* date (user's request; replaced a separate
    "Full year" mode): leave Day blank -> year and month `2026.01` (`YearMonth`), leave Month
    and Day blank -> just the year `2026` (`WholeYear`). A day without a month is flagged
    ("Type the month too, or clear the day.") and the row stays incomplete. Available on
    **every** file type; periods still need full start/end dates, quarters their own box.
    Because a bare year is now a complete date, a row turns "Ready" as soon as the year is
    typed. Year folders use that year; carry-forward goes to the next month / next year; the
    Deficiency List counts `2026.01` as that month and `2026` as all twelve;
    `splitStatementName` reads both forms (`DateField::spec()` in the GUI). The loose matcher
    does *not* read a bare year (too ambiguous).
- **Institution** uses the account's *institution display name* if set
  (e.g. "Bank of America" → `BofA`), else the full institution name.
- **Account type** is free text (no autocomplete). **Optional**
  (user: leave it blank if unknown): a blank type just drops out of the label --
  `2026.01.31 Chase x1234 (H).pdf`; on a combined statement `Chase x1111, Sav x2222`.
  Only the institution and a last-four number are required (`validateAccount`).
- **Owners**: each person's *display name* (e.g. `H`, `W`, `J`), in the account's
  owner order, joined with `"; "` → `(H; W)`. No owners → no parentheses.
- Reserved Windows names (CON, PRN, AUX, NUL, COM1-9, LPT1-9, with any extension) can only occur in
  no-date names; `buildPlan` marks them Error (`isReservedWindowsName`). Path length >260 was
  already an Error (`PlanOptions::maxPathLength`).
- Windows-illegal characters are cleaned: `/ \ : |` → `-`; `< > " ? *` removed;
  whitespace collapsed; trailing dots/spaces trimmed.
- Example: `2026.01.31 BofA Checking x1234 (J).pdf`
- **The number is written with an `x`** (`x1234`) in every name, single accounts as
  well as combined statements (user's request). Names written before that
  (`... Checking 1234 (J).pdf`, and folders likewise) are still **recognised**
  (`legacyAccountLabel()`, used by `FileLabelIndex` and `NameFixer`; never used to
  name anything): the Deficiency List counts them as ordinary matches and Update File
  Names renames them (and their account folders) to the x form in place.
- **Account numbers**: an account has one or more numbers (last four), newest
  first; the top one is current (`lastFour`), the rest are `previousLastFour`.
  Edited in the account dialog as one ordered list (Add Newer Number, Newer/Older
  to reorder, Remove, double-click to correct). File names use the number the
  statement shows, chosen per file with the rename screen's **Number** dropdown
  (next to the account dropdown; enabled when the account has >1 number); they
  never show "was". The **account folder** shows the older ones:
  `Chase Credit Card x9012 (was x5678, x1234) (H)`.

- **Combined statements** (one PDF covering several accounts at the same institution):
  `2026.01.31 Chase Chk x1111, Sav x2222, Chk x3333 (H; W).pdf` -- institution once,
  then each account's *type as entered* and current number with an `x`, in the
  order the user set, comma-separated; then the owners. User's choices: types as
  entered (no separate short type), owners **chosen on the combined statement
  itself**, its own account folder named like the file label, and combined
  statements are **saved in the case** (account dialog's "Statement" dropdown:
  Single account / Combined statement; fixed once saved) and picked from the
  rename screen's account list like any account. **A combined statement is typed, not built
  from existing accounts** (user's request): the dialog takes **one institution (plus display name)
  and a table of account types and numbers** (type optional, number required, at least two,
  Add Account / Move Up / Move Down / Remove; blank rows ignored; no duplicate type+number). Stored
  as an `accounts` row with `is_combined = 1`, its own institution columns, plus `combined_parts`
  (position, type, last four; schema v7 -- the v7 migration copied the old linked members' types and
  numbers into it and dropped `combined_members`); `Account::combined` /
  `AccountRecord::parts` (`CombinedPart{accountType, lastFour}`). Single accounts and statements
  are independent: deleting/editing an account never touches a statement. The Number dropdown
  doesn't apply. The Deficiency List credits a statement to every *single* account of the case at
  the same institution (full or display name, any case) whose current or older number appears on
  it (`accountsOnStatement` in `StatementCoverage.cpp`); parts with no matching saved account
  credit nothing. Editing a statement records its old names (as for single accounts).

## File types on the rename screen (decided with the user)

The rename screen's **Type** dropdown: **Statement** (the account-based name above, the
default), then **the user's own templates** (alphabetical), then **Other**. Email, Text and Letter
are no longer hardcoded (user's request): they are templates the user defines (the Templates
window's **Add Examples** button creates Email, Text, Text Conversation, Group Text and Letter
for them). Every name starts with the date in the same `YYYY.MM.DD` format; everything after it
is typed by the user (cleaned with `sanitizeComponent`), except fixed words in a pattern.

- **Template** = name + **pattern** + "allow a period" flag (`DocumentTemplate`; stored in the
  database, table `document_templates`, schema v8, shared by all cases). Pattern syntax (the
  date is never written in it; always added in front):
  `Email from {Sender} to {Recipient}[ ({Description})]`
  - `{Name}` = a text box captioned Name (required unless inside [ ]); the same name twice
    (ignoring case) is one box;
  - `[ ... ]` = optional part: left out, fixed words included, when all its boxes are blank; if
    only some of its boxes are filled the others become required; no nesting;
  - `\[ \] \{ \} \\` = literal characters; everything else is fixed text.
  - **"Start the name with a date" checkbox** (template editor, objectName "useDate", default on;
    `DocumentTemplate::useDate`, column `use_date`, schema v9): off = no date box on the rename
    screen and no date in the name (`Photo of the car.pdf`); the "allow a period" box is then
    disabled. A template that can print nothing (all parts optional, none filled) is flagged
    "Needs a name". `documentUsesDate(info)` is the one place that decides.
  - Template names are unique ignoring case and can't be "Statement" or "Other".
    `validatePattern` / `validateTemplate` give user-facing error messages (Database throws
    `DatabaseError` with them). Dates: single date always, a period only if "allow a period" is
    ticked; **quarters are statements only**; month/day stay optional (see Date modes).
  - Boxes on the screen are built from the pattern (`RenameWindow::syncFieldEdits`; objectNames
    `field_<name with spaces as _, lower case>`, e.g. "field_sender"). Choosing a template puts the
    cursor in its first box. Boxes are **kept (not rebuilt) when the next file uses the same
    template**, so the cursor stays put on Next File. Text typed is stored per field name in the
    row (`DocumentInfo::fields`, key `fieldKey`), so switching template keeps what has the same
    box name. **No autocomplete anywhere** (user's decision, privacy: nothing typed is
    remembered; `main.cpp` deletes the old `templateValues` section of settings.ini at startup).
  - **Templates... button** (objectName "templates") beside the Type dropdown opens
    `TemplatesDialog` (table "templates"; buttons addTemplate, editTemplate, duplicateTemplate,
    deleteTemplate, addExamples, close; editor fields "templateName", "templatePattern",
    "allowPeriod" with a live example and error text). Changes save at once. Rows on screen that
    aren't renamed yet are updated (`RenameSession::refreshTemplates`): an edited template applies
    to them keeping typed text; a deleted one turns the row into an **Other** with the name it had
    (or back to a blank statement row if nothing was typed).
  - **Export... / Import...** in the Templates window (objectNames "exportTemplates", "importTemplates"):
    all templates as one XML file (`templatesToXml` / `templatesFromXml`, pugixml; root
    `fileNameTemplates`, no ids). Import validates each template, skips bad ones with a reason,
    skips identical ones, adds new ones, and asks Replace Mine / Keep Mine / Cancel for same-name
    templates whose pattern differs. Folder remembered as `templates/lastDir`.
  - A row stores a **copy** of its template (`DocumentInfo::tmpl`), so core needs no database.
- **Other** (kept for one-off files): `[Date] [Label] ([Description])`; the label is required,
  the description optional (its parentheses drop out when blank); single date or period. The date
  is **optional per file**: the "Start the name with a date" checkbox (objectName "includeDate",
  checked by default, `DocumentInfo::noDate`) turns it off for that file (name = `Label (Description)`);
  carry-forward copies the setting.
- Quarters are for statements only; **month and day are optional for every kind** (see Date modes). Dates beyond what a kind offers are rejected (`validateDocumentDate`).
- **Folder sorting is for statements only** (user's rule): template and Other files
  get neither account nor year folders -- they are renamed where they are (`buildPlan` uses the
  source folder for them). Their "Into folder" cell reads grey "(stays here)".
- **Carry-forward** after a non-statement copies the kind, template, typed text and the **same date**
  (not a month later), and blanks the **optional** boxes (for Other: the description)
  (`clearOptionalFields`).
- Missing fields show in the Status column, e.g. "Needs sender and recipient" / "Needs sender and a date"
  (`documentMissing`, delivered through `PlannedMove::message` on `Incomplete`).
- Code: `core/DocumentName` (`DocKind{Statement,Template,Other}`, `DocumentTemplate`, `DocumentInfo`,
  `templateFields`, `sampleLabel`, `exampleTemplates`, `buildDocumentFilename`);
  `PlanInput::document` and `SessionRow::document` (kind `Statement` = use the account).
  Only the templates are stored in the database; what was typed per file is not.
  Not yet covered: the Deficiency List and Update File Names only know statements, so these
  files show up under "Files Not Matched".

## Subfolder sorting

- Optional: by account, by year, or both (order: Account→Year or Year→Account). **Statements
  only.** The settings live in the **Statement Options...** window (button in the rename screen's
  options row; dialog "statementOptions", close button "closeStatementOptions", button
  "statementOptionsButton"; checkboxes "byAccount"/"byYear" are inside it). User's choice: a
  separate window, and **no summary label beside the button**. The "If a name is already taken"
  choice stays on the main screen (it applies to every file type).
- Account folder name = the **full label** (same as the filename minus the date),
  plus `(was x...)` after the number when the account has previous numbers
  (`accountFolderLabel()`): `BofA Checking x1234 (J)`, `Chase Credit Card x5678 (was x1234) (H)`.
- Year folder = the date's year; **periods use the end date's year**; quarters their own year.
- Folders are created only when a file goes into them; undo removes only folders
  that batch created, and only if empty.

## Other decisions

- Every new case starts with people **Husband (H), Wife (W), Joint (J)**;
  they're ordinary people (rename/delete freely).
- People have a **full name** and a **display name** (blank = full name). Both
  unique per case, case-insensitively, so two people can't produce the same filename.
- Institution display names are typed by hand (the old autofill/autocomplete was removed on
  purpose; don't bring back remembered suggestions).
- **Skip**: a per-file option to leave a file as is. Skipped files are *not*
  remembered between sessions (user wants to re-check them on a second pass).
- **Partial apply** is allowed: rows not filled in are left alone; applied rows
  turn "Renamed", grey and locked; the user carries on.
- **Carry-forward**: pressing Enter / Next File copies the account and the *next*
  date to the next file if it's blank (month-ends stay month-ends; periods shift by
  their own length; quarters roll over). Deliberately **not** on mouse clicks, so
  browsing never silently fills rows. **Focus stays in the field it was in** (e.g. Description)
  when moving to the next file, if that field is still enabled/visible there (`goToNext`);
  otherwise focus goes to the date (after a carry) or the account.
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
- **Edit Account... on the rename screen** (button "editAccount", next to New Account..., enabled
  when an account is chosen): opens the same `AccountDialog` as the Case List; afterwards the
  account list and preview refresh, a row whose chosen number was removed/corrected falls back
  to the account's current number, and the same "Update them now?" popup is offered. The
  popup logic is shared (`accountNameSet` / `offerNameUpdate` in `UpdateNamesDialog`), used by
  `MainWindow` too.
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

- **Deficiency List** (main window button; `DeficiencyDialog`): searches the case's
  folders (the same remembered per-case folder list as Update File Names, shared via
  `CaseFolderList`) for statements *already renamed* by this program and reports which
  months each account is missing. User's choices: date range **From/To plus a
  "Through present" option** (range line ends "Present"; present = last *completed*
  month, i.e. last month); an optional **Opened on / Closed on** date per single
  account (account dialog; months before the opening month and after the closing
  month are not expected, those two months themselves are); check boxes **Months
  found** / **Months missing** (at least one stays on); **no "months expected"**
  column; **one table per account with one row per year** in the range; title
  `<Case Name> Deficiency List`, the date range below it. Years in the range before
  opening/after closing show "2022 (not open)" with dashes. Matching reads each PDF's
  *name* (`splitStatementName` -> date + label -> `FileLabelIndex.find`), so current
  labels, previous-number labels, recorded old names and " (n)" copies all count.
  **Loose matching** (user's request: files named in the past, not by this program,
  must count too): a name that isn't in the program's format is read by
  `LooseNameMatcher` (`LooseStatementNames.cpp`): a date *anywhere* in the name
  (`2023-05-31`, `20230531`, `05-31-2023`, `2023-05`, `May 2023`, `May 31, 2023`,
  `2023 Q2`, periods) plus the account's **last four** (also `x1234`; previous numbers
  too) plus the **institution** (full or display name) **or the type**, as whole words
  in any order/case; owners and everything else are ignored. A combined statement
  needs the institution and all its numbers. **Never guesses**: a tie between two
  accounts, or a number alone, stays unmatched. Loose matches are counted, listed under
  "Other Name Styles..." (`CoverageScan::loose`) and mentioned in the summary.
  **Possible matches** (user's request; stops emails/letters/orders that mention an account from
  filling a missing month): a loose match counts only if the name has at most one word the match
  doesn't explain (`LooseNameMatcher::Match::extraWords`, `kMaxExtraWords = 1`; explained = date,
  institution, type, number, owners, filler like "statement", 1-2 digit copy numbers). Others go to
  `CoverageScan::possible` and are NOT credited by `scanStatements`; `applyPossibleMatches(scan,
  allAccounts, included)` returns the scan with the ticked ones credited. `DeficiencyDialog` keeps
  `scan_` (folders as they are) and `effective_` (after the ticks); after a scan, files without a
  remembered answer open `PossibleMatchesDialog` (check boxes, Check All/Uncheck All; start ticked;
  Cancel = not counted, not remembered); answers are remembered per case
  (`deficiency/case<id>/possible`, matched by `caseFoldKey`); "Possible Matches..." (objectName
  "possible") reopens it. The preview stays live. Months backed only by loosely named files
  (`strictCovered` vs `looseCovered`, `YearCoverage::looseOnly`) get a `*` in Months Found, with
  `DeficiencyReport::footnote` on screen and in the Word file.
  Known limit: an abbreviation the account doesn't use (e.g. "WF" for "Wells Fargo")
  still needs the type, or the file stays under "Files Not Matched...".
  A single date covers its calendar month, a period every month it touches, a
  quarter its three months, a year and month just that month, a bare year all twelve; a combined statement credits every member account.
  PDFs not named like a statement are listed under "Files Not Matched...". Combined
  statements aren't listed as tables (their members are). Nothing in the folders
  is changed. Word output is a hand-written stored zip + WordprocessingML
  (`DocxWriter.cpp`): no library. Range/options remembered per case in settings.ini
  under `deficiency/case<id>/...`.

- **Bates Log** (start-screen tile; `BatesDialog`; no case needed): ported from the user's
  earlier "Bates Log Excel Updater" (Win32/ImGui, OpenXLSX) and extended. One spreadsheet,
  two jobs, **the Bates log is optional** (user's request: List Files stands alone; the Bates
  group is titled "Add Bates numbers (optional)" and its button stays disabled until a log is
  chosen). **With no log chosen, List Files writes only the "Name" header** (no Begin/End Bates
  headers in B/C; `writeListing(..., batesColumns=false)`); with a log chosen it writes all three.
  Add Bates Numbers adds the B/C headers later if they're missing. **List Files** writes every file in a chosen folder *and its subfolders* into
  the spreadsheet in the layout of the user's sample "Bates Log.xlsx": row 1 header
  (**Name | Begin Bates | End Bates**, bold on light grey E6E6E6), row 2 blank, then per
  folder a bold folder row (`/` = the listed folder itself, then `/Folder 1`,
  `/Folder 1/Sub 1`), its file names in column A, and one blank row between folders. Order:
  depth first, names in plain character order (so `Z` before `a`, as in the sample), folders
  with no files directly in them are left out, every file type is listed (mp4, m4a, ...)
  except Office lock files `~$...`, Thumbs.db, desktop.ini, .DS_Store and the spreadsheet
  itself. New name -> a new workbook (font Aptos Narrow 11, like the sample); existing file ->
  **a fresh listing is appended at the bottom** after one blank row, even if some files are
  already listed (user's choice over merging); column A is only ever added to. **Add Bates
  Numbers** reads Adobe's tab-separated log (`name<TAB>begin<TAB>end`; UTF-8, UTF-16 BOM or
  ANSI) and writes Begin Bates / End Bates into B and C of every file row whose name matches
  (`matchKey`: control chars and outer spaces dropped, one trailing `.pdf` dropped, letter case
  ignored -- the old program was case-sensitive and stripped all non-ASCII). Rows not in the
  log are left alone (numbers already there stay), headers B1/C1 are added if missing. The
  dialog reports files not in the log, log entries not in the sheet, repeated log names, and
  names on more than one row (same numbers on all). Matching is by file name only (the log
  has no folder). Numbers are written as text. Needs the workbook closed in Excel.
  Paths remembered in settings.ini under `bates/workbook|folder|log`.

- **Vacation Letters** (start-screen tile; `VacationDialog`; no case needed): the firm's list of
  cases lives in an Excel workbook on a shared drive (Dropbox) and the letter is a Word mail-merge
  template (`Vacation_Letter_v_<date>.docx`) in the same shared place -- **both stay where they
  are and are read fresh each time** (user: others use them without this program; the program
  never copies or replaces the template). Decided with the user: **only the "ALL CASES..." tab
  matters** (the attorney tabs are an old method; the loader takes the sheet whose name starts
  with "ALL CASES", else the first); drafts go into **one combined .docx** saved wherever the
  user picks (standard Save dialog; default `Vacation Letters yyyy.MM.dd.docx`; folder
  remembered); cases chosen by **tick boxes in a list filtered by attorney / paralegal / county /
  text**; **"(HOLD)" and "eFile only" cases are treated like any other** (the cell text prints
  as is -- user's answer; don't add special handling unasked).
  - Edit form: one box per sheet column; **Save writes only the changed cells of that one row**
    back into the .xlsx (`saveVacationRow`), patching the sheet XML with pugixml and re-zipping:
    other tabs, the table, styles, conditional formats, column widths are carried over untouched
    (OpenXLSX's load/save round trip is deliberately not used for this). New text is written as
    inline strings. New case = first free row below the last (a pre-formatted blank row is
    reused; the table's `ref`/autoFilter and the sheet's dimension grow if needed). Written to
    `<name>.xlsx.saving` then renamed over the original, so a failed save (file open in Excel)
    never damages the shared workbook.
  - **Conflict safety** (it's a shared file): a save passes what the row held when loaded
    (`expected`); if the workbook's row differs now -> `SheetConflict`, nothing written. Ticked
    cases are remembered with their values; **Reload / Create Letters untick any whose row
    changed** and refuse to create until the user re-checks. Reload leaves a form with unsaved
    typing alone (Revert loads the fresh values).
  - Letters (`VacationLetters.cpp`): the template's merge fields (`MERGEFIELD name`, matched to
    columns by `normalizeHeader`: lower-case letters+digits only, so `Client_CoCounsel` =
    " Client (Co-Counsel)" and `OPC_2_Address` = "OPC 2 Address" = "OC 2 Address") are replaced
    at the XML level by a plain run copying the placeholder's formatting; a cell's line breaks
    become `<w:br/>`. The **DATE field is kept** (shows today's date until Word refreshes it).
    Letters are repeated in one body, each after the first starting with `pageBreakBefore`
    (no spacer paragraph); header/footer/letterhead come from the template's single section.
    The template's `mailMerge` settings block, its `settings.xml.rels` and `recipientData.xml`
    (the link to the user's local Dropbox path) are dropped from the output so Word doesn't
    ask about a data source. A merge field with no matching column is left blank and listed.
  - Known template typos (user's to fix in the shared template, not ours): "202" for 2026 in
    Michael Burnett's first date range; some attorney labels have a colon, some don't.
  - Needs pugixml directly (`pugixml::pugixml`, found via OpenXLSX's copy or vcpkg).
  - Settings: `vacation/workbook|template|saveFolder`. objectNames: "workbook", "template",
    "browseWorkbook", "browseTemplate", "reload", "search", "attorney", "paralegal", "county",
    "cases" (checkbox column 0, sheet row in `Qt::UserRole`), "selectShown", "clearSelection",
    "selectedCount", "form", "field_<normalized column>" (e.g. "field_county"), "newCase",
    "revert", "saveCase", "status", "createLetters", "close".
  - **Signatures** ("Signatures..." button, objectName "signatures", next to the template row;
    `SignaturesDialog`): a list of attorney -> signature picture (PNG/JPEG), add/change/remove as
    attorneys join or leave, saved at once in settings.ini under `vacation/signatures` (array of
    `attorney`, `file`; folder memory `vacation/signatureFolder`). Pictures are used from where
    they are saved, not copied. The signer of a letter is its "Partner on case" value (fallback
    "Attorney"); matching uses `normalizeHeader` (case/punctuation ignored). The **template needs a
    `Signature` merge field** (a MERGEFIELD named Signature, in its own paragraph above the signer's
    name); `fillBody` replaces it with an inline picture (`wp:inline`, media `word/media/signatureN.ext`,
    rels `rIdSignatureN`), or nothing if the signer has no picture. Create Letters reports signatures
    added, signers with no picture, and a template with no Signature field. objectNames in the dialog:
    "signers", "addSigner", "editSigner", "removeSigner", "close".
  - Not built: deleting a case (do it in Excel), editing other tabs, one-file-per-case output.

- **PDF Unlocker** (start-screen tile "pdfUnlocker"; `UnlockDialog`; no case needed): like SysTools'
  PDF Unlocker. Makes **unprotected copies**: removes owner restrictions (printing, copying,
  editing, commenting, form filling, page assembly -- no password needed) and, for PDFs that ask
  for a password to open, uses a password **the user supplies** (per file in the Password column,
  double-click; or one password for the whole list via "Try on Password-Protected PDFs"). It
  **never guesses or cracks passwords** (decided: not building that). Decided with the user: copies
  go to **a folder the user chooses** (required; remembered in `unlock/outputFolder`), **originals
  are never touched and nothing is overwritten** (name taken -> " (2)", `unlockedCopyPath`),
  input is **files or whole folders** (Add Files / Add Folder + "Include subfolders" remembered as
  `unlock/subfolders`, drag-and-drop onto the window). A PDF that isn't protected is skipped (no
  copy); unreadable ones are listed, not crashed on; every row shows Status (inspect result),
  Password, Result. A second "Unlock PDFs" doesn't redo finished rows ("Already unlocked.").
  - Core `PdfUnlock` (`inspectPdf`, `unlockPdf`, `unlockedCopyPath`) is a thin layer over **qpdf**
    (`QPDFWriter::setPreserveEncryption(false)`; writes `<dest>.unlocking` then renames; refuses
    dest == source and existing dest). Handles RC4 40/128, AES-128, AES-256 (R2-R6); tests build
    each kind with qpdf itself.
  - **Building qpdf** (`core/CMakeLists.txt`): an installed qpdf (vcpkg) is used if found, else
    qpdf 12.2.0, zlib 1.3.1 and libjpeg-turbo 3.0.4 are fetched and built static (own "native"
    crypto, no DLLs, nothing to install on Windows). qpdf only finds zlib/libjpeg through
    `find_path`/`find_library`, so those cache variables are pre-answered with our targets
    (`zlibstatic`, `jpeg-static`) and headers staged in `build/qpdf-deps/include`.
    libjpeg-turbo refuses `add_subdirectory`, so `cmake/patch_libjpeg_turbo.cmake` switches off
    that one check (FetchContent PATCH_COMMAND). qpdf registers perl-based self-tests with
    `add_test`; a wrapper `add_test` function drops them so `ctest`/Test Explorer stay clean.
    `FINRENAMER_QPDF_TARGET` (cache INTERNAL) is what tests link. Verified on Linux/GCC only;
    **the first MSVC build of qpdf/libjpeg-turbo/zlib is the user's** (first configure downloads
    them; first build takes a few extra minutes). If it fails on MSVC, ask for the Output window.
  - objectNames: "files" (columns File, Folder, Status, Password, Result), "addFiles", "addFolder",
    "includeSubfolders", "removeSelected", "clearList", "batchPassword", "applyPassword",
    "outputFolder", "browseOutput", "unlock", "openOutput", "summary", "close".
  - Licence notices: see **Third-party notices** below.

## Third-party notices

`THIRD-PARTY-NOTICES.txt` (repo root) holds the required notices and full licence texts for
qpdf (Apache-2.0 + NOTICE), libjpeg-turbo (IJG + BSD, with the "based in part on the work of the
Independent JPEG Group" line), zlib, SQLite, SQLiteCpp, OpenXLSX, pugixml, miniz, Nowide, Qt (LGPL-3.0,
dynamically linked) with Qt's bundled components and Qt PDF's, and the MSVC runtime. It is
compiled into the app (`app/CMakeLists.txt` resource; `NoticesDialog`, start-screen button
"licences") and installed to the install root (the installer copies all of dist). It was
generated from the real licence files by a script (not in the repo): **when a dependency or
its version changes, regenerate/edit it by hand.** Unverified: Nowide's copyright line, and Qt's
component list comes from the Linux Qt 6.12.0 build's SBOM, not the user's Windows install.

## Layout of the code

    CMakeLists.txt          options FINRENAMER_BUILD_APP / FINRENAMER_BUILD_TESTS
    CMakePresets.json       "vs2022" configure preset (Qt path), debug/release build presets
    core/                   static library, NO Qt -- all logic, unit-tested
      Models.h              ClientCase, Person{fullName, displayName}, Account
      DateSpec              variant<SingleDate, Period, Quarter, WholeYear, YearMonth>; format, validate,
                            filingYear, nextDateSpec (carry-forward), parseUserDate
      FilenameBuilder       sanitizeComponent, accountLabel, buildFilename, validateAccount
      DocumentName          DocKind/DocumentTemplate/DocumentInfo: user-defined name patterns (parse, fields, names, missing text) and Other files
      FolderPlanner         subfolderFor(account, date, SortOptions) -- statements only; documents get no subfolder
      FileScanner           listPdfFiles (top level, case-insensitive .pdf, sorted)
      NameFixer             planNameFixes(roots, accounts, oldNames, options): in-place renames
      RenamePlan            buildPlan(): preview, read-only. Statuses: Ready, Unchanged,
                            Skipped, Incomplete, Collision, Error
      RenameEngine          execute(plan) -> BatchRecord; removeEmptiedFolders(record);
                            undo(record) (recreates removed parent folders)
      RenameSession         rename-screen state: rows, carry-forward, refresh,
                            apply (partial), undoLast
      FolderSearch, StatementNames   find PDFs in folders; split a statement name into date + label
      LooseStatementNames   findLooseDate, LooseNameMatcher: forgiving date/account reader for old names
      StatementCoverage     scanStatements (months covered per account), analyzeCoverage (per year)
      DeficiencyReport      buildDeficiencyReport (plain-text model), writeDocx/docxBytes (DocxWriter.cpp)
      DirectoryListing      listDirectoryTree (folders + files, sample order), isIgnoredFileName
      BatesLog              parseBatesLog / matchKey: Acrobat's Bates log reader
      BatesSheet            writeListing, applyBatesLog: the .xlsx layout, via OpenXLSX
      VacationSheet         load the ALL CASES sheet; saveVacationRow patches one row (pugixml + zip), conflict check
      VacationLetters       letterValues, templateMergeFields, writeCombinedLetters (fills the Word template)
      PdfUnlock             inspectPdf / unlockPdf / unlockedCopyPath: remove PDF restrictions/passwords (qpdf)
      Database              SQLite via SQLiteCpp (pimpl; not exposed in headers)
      Utf8Path              pathFromUtf8 / utf8FromPath / utf16Length / caseFoldKey
    app/                    Qt Widgets GUI (static lib finrenamer_ui + FinRenamer.exe)
      main.cpp              opens the DB in AppData, shows HomeWindow
      HomeWindow            start screen: Case List / File Renamer / Deficiency List / Bates Log tiles
      BatesDialog           Bates Log: spreadsheet + List Files + Add Bates Numbers
      CasePickerDialog      "Choose a Case" (filter list; remembers home/lastCase; "New Case..." button "newCase" creates + selects + continues; opens straight into it when there are no cases) for the last two tiles
      MainWindow            the Case List: cases list | people table + accounts table; File menu
      CaseDialog, PersonDialog, AccountDialog
      TemplatesDialog       Templates...: add/edit/duplicate/delete file name templates (rename screen's Type list)
      RenameWindow          the rename screen (table + editor left, preview right)
      DateWidgets           DateField (Year/Month/Day boxes + calendar), DateSpecEditor (mode + inputs; setModes limits the modes)
      PdfPreview            QPdfView preview; compiled only with FINRENAMER_HAVE_QTPDF
      HistoryDialog         File > Rename History, undo any batch
      CaseFolderList        shared per-case folder list (Add/Remove/drag; settings key updateNames/case<id>/folders)
      UpdateNamesDialog     Update File Names: folder list, preview, apply
      DeficiencyDialog      Deficiency List: range, options, accounts, preview, Save as Word
      VacationDialog        Vacation Letters: case list (filters, tick boxes), edit form, Create Letters
      UnlockDialog          PDF Unlocker: file list, passwords, output folder, Unlock PDFs
      NoticesDialog         Licences...: shows THIRD-PARTY-NOTICES.txt (compiled-in resource)
      QtHelpers.h           qstr/stdstr/toPath/qpath, settingsFile, personLabel,
                            disconnectChildren, runGuarded
    tests/                  Catch2 v3 (169 tests): core + Database + RenameSession + NameFixer + BatesSheet + DocumentName + Vacation + PdfUnlock
    installer/FinRenamer.iss  Inno Setup script

Dependencies: Qt 6.12 (Widgets, optional Pdf/PdfWidgets), SQLiteCpp 3.3.3, OpenXLSX
(pinned commit c1220f3, reads/writes .xlsx; core links it PRIVATE) and Catch2 3.7.1 (vcpkg if
present, else FetchContent from GitHub). OpenXLSX itself fetches pugixml, miniz and nowide (core also uses pugixml directly, and OpenXLSX's zip class, for the Vacation Letters sheet/template).
Also qpdf 12.2.0 + zlib + libjpeg-turbo (PDF Unlocker; see that section). Only compile-checked/run on Linux/GCC so far -- the first MSVC build of OpenXLSX and qpdf is the user's.

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
  Current schema version: **9** (v9 document_templates.use_date; v8 document_templates; v7 combined_parts replaced combined_members; v2 added people.display_name, v3 added
  accounts.institution_display, v4 added account_previous_numbers and
  account_name_history, v5 added accounts.is_combined and combined_members, v6 added
  accounts.opened_on and closed_on, "YYYY-MM-DD" or empty).
- **Database backups**: opening a database older than `kSchemaVersion` first copies it (SQLite
  `VACUUM INTO`) to `finrenamer.db.backup-v<old version>` beside it (`-2`, `-3` if taken; never
  overwritten, never pruned); if the copy fails nothing is upgraded. `Database::backupTo(file)` is the
  File menu's **Back Up Database...** (Case List; saves a copy anywhere; no restore command -- put
  the file back as finrenamer.db with the program closed).
- **Database errors**: rule violations throw `DatabaseError` with a message meant
  for the user; GUI calls go through `ui::runGuarded(this, [&]{ ... })`.
- **Widget destructors**: any widget that connects its children's signals to itself
  calls `ui::disconnectChildren(this)` first in its destructor (see Lessons).
- **Aggregate orders** (brace-initializers must follow them; new fields go at the END
  so existing initializers keep working):
  `Account{id, caseId, institution, institutionDisplay, accountType, lastFour, owners, previousLastFour, combined, openedOn, closedOn}`,
  `AccountRecord{id, caseId, institution, institutionDisplay, accountType, lastFour, ownerIds, previousLastFour, parts, openedOn, closedOn}`,
  `CombinedPart{accountType, lastFour}`,
  `PlanInput{source, account, date, skip, number, document}`.
- `updateAccount(record, numberCorrections)`: history rows store, for each old file
  name, the number files with that name should **now** use (corrected text, or the
  same number); the fixer falls back to the current number if the account no
  longer has it.
- **Any DB update that can change an account's names must record the old names**
  (`Impl::recordOldNames(before, after)` inside the same transaction), or Update File
  Names won't find files named the old way.
- Key rename-screen widgets have `objectName`s ("files", "account", "number", "date",
  "editAccount", "singleDate" (its boxes "year", "month", "day"), "quarter", "quarterYear", "mode", "skip", "next", "apply", "undo",
  "summary", "byAccount", "byYear", "preview", and for the file types "kind", "templates", "label" and
  "description" and "includeDate" (Other), "field_<name>" (a template's boxes)) for automated GUI tests.
- **Start screen** (`HomeWindow`, what `main.cpp` shows): six tiles -- **Case List**
  (opens `MainWindow`, hiding the start screen until it's closed), **File Renamer** (the
  rename screen; the program's name for it is "File Renamer", not "Rename Files") and
  **Deficiency List**, **Bates Log**, **Vacation Letters** and **PDF Unlocker**; File Renamer and Deficiency List ask which case via `CasePickerDialog` (last case
  preselected). Closing the start screen quits the app (`quitOnLastWindowClosed` is off).
  objectNames: "caseList", "fileRenamer", "deficiencyList", "batesLog", "vacationLetters", "pdfUnlocker"; picker: "filter", "cases", "newCase".
  **Look** (user asked for the start screen and Case List to match the File Renamer):
  stock Fusion widgets, boxed panels (`QGroupBox`: Cases / People / Accounts), a
  1.5x bold title, and the File Renamer's footer -- information on the left (data
  folder, label "dataFolder"), **Close** on the right (button "close"); no
  `QStatusBar`. Keep new screens in this style.
- Account dialog objectNames: "statementType", "institution", "parts" (combined: Type/Number table), "addPart", "removePart", "numbers", "openedOn", "closedOn".
- Deficiency dialog objectNames: "folders", "subfolders", "fromMonth", "fromYear", "toMonth", "toYear",
  "present", "found", "missing", "accounts", "preview", "summary", "unmatched", "loose", "save".
- Bates dialog objectNames: "workbook", "folder", "logFile", "browseWorkbook", "browseFolder", "browseLog",
  "listFiles", "addBates", "results", "openSheet", "close".
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
  **installer** → `installer/Output/ZachsLitigationTools-Setup-x.y.z.exe`. It installs
  per-user without admin (`PrivilegesRequired=lowest`), upgrades in place (fixed
  `AppId`), bundles the MSVC runtime DLLs (no vc_redist), never touches AppData.

## Status and roadmap

Done: core logic, database, case/people/account management, rename screen
(carry-forward, skip, sorting, partial apply, undo last, refresh), rename history,
PDF preview, installer, previous account numbers, Update File Names (multi-folder,
in place), Year/Month/Day date boxes, combined statements, Deficiency List (Word report), Bates Log (list files to Excel, add Bates numbers), Vacation Letters (edit the shared case list, draft letters from the shared template), PDF Unlocker (remove PDF restrictions, open with a known password), file types on the rename screen
(statement / user-defined templates / other).

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
