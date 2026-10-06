# FinRenamer

Renames and sorts client financial statements (PDFs) into the format

    2026.01.31 Chase Checking 1234 (John Smith; Jane Smith).pdf
    2026.01.01 - 2026.01.31 Chase Checking 1234 (John Smith).pdf
    2026.Q1 Fidelity Brokerage 5678 (Jane Smith).pdf

with optional subfolders by account (full label), by year, or both.

## Status

- [x] `core/`: date modes, filename builder, folder planner, rename plan, rename/undo engine
- [x] `core/Database`: SQLite storage (cases, people, accounts, rename history)
- [x] `core/RenameSession`: rename-screen state (rows, carry-forward, refresh, apply, undo last)
- [x] `tests/`: Catch2 suite (78 tests)
- [x] `app/`: Qt 6 main window (cases, people, accounts) and dialogs
- [x] Rename screen (file table, date modes, sorting, apply/undo) and Rename History
- [x] PDF preview pane (Qt PDF; optional at build time)
- [x] Previous account numbers (`(was x1234)` in folder names) and Update File Names (several folders, renamed in place)

## Building (Windows, Visual Studio 2022)

`CMakePresets.json` points CMake at Qt in `C:/Qt/6.12.0/msvc2022_64`; change
that path there if Qt moves or is upgraded.

    cmake --preset vs2022
    cmake --build --preset debug
    ctest --preset debug

Or open the folder in Visual Studio and pick the "Visual Studio 2022 x64 (Qt 6.12)"
preset. Each build runs `windeployqt`, so `build/app/Debug/FinRenamer.exe` runs as-is.
The database lives in `%APPDATA%\FinRenamer\finrenamer.db` (File > Show Data Folder).

To build only the core library and tests without Qt: `-DFINRENAMER_BUILD_APP=OFF`.

Catch2 and SQLiteCpp are used from vcpkg if found (`vcpkg install catch2 sqlitecpp`),
otherwise CMake downloads them automatically.

## Making an installer (Inno Setup)

One-time: install Inno Setup 6 (https://jrsoftware.org/isinfo.php), then
reconfigure CMake so it finds it (look for no "Inno Setup not found" message).

Each release:

1. Bump `VERSION` in the top-level `CMakeLists.txt` (`project(FinRenamer VERSION 0.2.0 ...)`).
2. Switch to the **Release** configuration and build the **installer** target, or:

       cmake --build --preset release --target installer

3. The installer is `installer/Output/FinRenamer-Setup-<version>.exe`.

What happens: the Release build is installed into `dist/` (only the exe, the Qt
DLLs/plugins and the MSVC runtime DLLs), then `installer/FinRenamer.iss` is compiled.
The setup installs per-user without admin rights by default, upgrades older
versions in place (same `AppId`), and never touches `%APPDATA%\FinRenamer`.

## Database

One SQLite file (the GUI will keep it in `%APPDATA%`). Schema changes go in
`kMigrations` in `Database.cpp` as a new entry; never edit a shipped one.
`PRAGMA user_version` records which entries have run.

## How the pieces fit

    PlanInput (file + chosen Account + DateSpec)   <- from the GUI table
        |
        v
    buildPlan()   -> RenamePlan   (preview; reads the disk, never changes it)
        |
        v
    execute()     -> BatchRecord  (save to RenameLog for undo)
        |
        v
    undo(record)

Key rules the core enforces:

- Files marked Skip are left untouched, and their names stay reserved.
- Never overwrites a file; same-name files get " (2)", " (3)"... (or are blocked).
- Collisions are checked against the final path, including files already in subfolders.
- Periods file under their end date's year.
- Folders are created only when a file goes into them; undo removes only folders
  this batch created, and only if they are empty.
- Windows-illegal characters are cleaned from names; paths over 259 characters are rejected.
- All strings are UTF-8; use `pathFromUtf8` / `utf8FromPath` when converting to
  `std::filesystem::path` (and `QString::toStdString()` / `QString::fromStdString()`
  from Qt, which are UTF-8).
