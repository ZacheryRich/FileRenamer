# FinRenamer

Renames and sorts client financial statements (PDFs) into the format

    2026.01.31 Chase Checking 1234 (John Smith; Jane Smith).pdf
    2026.01.01 - 2026.01.31 Chase Checking 1234 (John Smith).pdf
    2026.Q1 Fidelity Brokerage 5678 (Jane Smith).pdf

with optional subfolders by account (full label), by year, or both.

## Status

- [x] `core/`: date modes, filename builder, folder planner, rename plan, rename/undo engine
- [x] `tests/`: Catch2 suite (31 tests)
- [ ] SQLite storage (cases, people, accounts, rename log)
- [ ] Qt 6 GUI (case/account management, rename session, PDF preview)

## Building (Windows, Visual Studio 2022)

    cmake -S . -B build -G "Visual Studio 17 2022"
    cmake --build build --config Debug
    ctest --test-dir build -C Debug --output-on-failure

Catch2 is used from vcpkg if found (`vcpkg install catch2`), otherwise CMake
downloads it automatically.

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
