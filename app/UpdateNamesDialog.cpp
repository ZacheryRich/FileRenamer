#include "UpdateNamesDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "CaseFolderList.h"
#include "QtHelpers.h"
#include "finrenamer/NameFixer.h"
#include "finrenamer/RenameEngine.h"
#include "finrenamer/Utf8Path.h"

using namespace finrenamer;
using namespace ui;
namespace fs = std::filesystem;

namespace {

enum Column { ColWhere = 0, ColNow = 1, ColNew = 2, ColStatus = 3 };
const QColor kRed(0xc4, 0x2b, 0x1c);
const QColor kGrey(0x70, 0x70, 0x70);

}  // namespace

UpdateNamesDialog::UpdateNamesDialog(Database& db, std::int64_t caseId, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId)
{
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    std::optional<ClientCase> c;
    runGuarded(this, [&] { c = db_.getCase(caseId_); });
    setWindowTitle(tr("Update File Names - %1").arg(c ? qstr(c->clientName) : QString()));

    auto* intro = new QLabel(tr(
        "Finds files and account folders that still use an account's old name (a typo since fixed, "
        "a changed display name, a new account number) and renames them where they are; nothing "
        "is moved to another folder. Add every folder where this case's statements were renamed. "
        "Nothing changes until you click Apply, and Rename History can undo it."));
    intro->setWordWrap(true);

    // ---- Folders to search (shared with the Deficiency List) ----
    folderList_ = new CaseFolderList(caseId_);
    subfolders_ = new QCheckBox(tr("Include subfolders"));
    subfolders_->setObjectName("subfolders");
    renameFolders_ = new QCheckBox(tr("Also rename account folders"));
    renameFolders_->setObjectName("renameFolders");
    renameFolders_->setToolTip(tr("An account folder still using an old name (or missing \"(was x...)\") "
                                  "is renamed where it is; its contents stay inside it."));
    folderList_->addSideWidget(subfolders_);
    folderList_->addSideWidget(renameFolders_);

    QSettings settings(settingsFile(), QSettings::IniFormat);
    subfolders_->setChecked(settings.value("updateNames/includeSubfolders", true).toBool());
    renameFolders_->setChecked(settings.value("updateNames/renameFolders", true).toBool());

    auto* foldersBox = folderList_;

    // ---- Changes ----
    table_ = new QTableWidget(0, 4);
    table_->setObjectName("changes");
    table_->setHorizontalHeaderLabels({tr("In folder"), tr("Now"), tr("Will become"), tr("Status")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideMiddle);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(ColWhere, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(ColNow, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColNew, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColStatus, QHeaderView::ResizeToContents);
    table_->setColumnWidth(ColWhere, 220);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(foldersBox);
    splitter->addWidget(table_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({140, 400});

    summary_ = new QLabel;
    summary_->setObjectName("summary");
    auto* rescanBtn = new QPushButton(tr("Scan Again"));
    applyBtn_ = new QPushButton(tr("Apply"));
    applyBtn_->setObjectName("apply");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addButton(rescanBtn, QDialogButtonBox::ActionRole);
    buttons->addButton(applyBtn_, QDialogButtonBox::AcceptRole);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(summary_, 1);
    bottom->addWidget(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(splitter, 1);
    layout->addLayout(bottom);

    for (auto* b : findChildren<QPushButton*>()) b->setAutoDefault(false);

    connect(folderList_, &CaseFolderList::changed, this, &UpdateNamesDialog::scan);
    for (auto* box : {subfolders_, renameFolders_})
        connect(box, &QCheckBox::toggled, this, [this] {
            QSettings s(settingsFile(), QSettings::IniFormat);
            s.setValue("updateNames/includeSubfolders", subfolders_->isChecked());
            s.setValue("updateNames/renameFolders", renameFolders_->isChecked());
            scan();
        });
    connect(rescanBtn, &QPushButton::clicked, this, &UpdateNamesDialog::scan);
    connect(applyBtn_, &QPushButton::clicked, this, &UpdateNamesDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(1150, 640);
    scan();
}

UpdateNamesDialog::~UpdateNamesDialog()
{
    disconnectChildren(this);
}

// ---------------------------------------------------------------------------
// Folder list

bool UpdateNamesDialog::promptForFolder()
{
    return folderList_->promptForFolder();
}

void UpdateNamesDialog::addFolder(const QString& folder)
{
    folderList_->addFolder(folder);
}

bool UpdateNamesDialog::hasFolders() const
{
    return folderList_->hasFolders();
}

// ---------------------------------------------------------------------------
// Scan / apply

void UpdateNamesDialog::scan()
{
    plan_ = {};
    const bool anyFolders = folderList_->hasFolders();
    if (anyFolders) {
        const std::vector<fs::path> roots = folderList_->paths();
        NameFixOptions options;
        options.includeSubfolders = subfolders_->isChecked();
        options.renameFolders = renameFolders_->isChecked();

        QApplication::setOverrideCursor(Qt::WaitCursor);
        runGuarded(this, [&] {
            plan_ = planNameFixes(roots, db_.loadAccounts(caseId_), db_.oldAccountNames(caseId_), options);
        });
        QApplication::restoreOverrideCursor();
    }

    // Folder paths are shown relative to the folder all the chosen ones share.
    auto where = [this](const fs::path& dir) {
        if (!plan_.root.empty()) {
            const fs::path rel = dir.lexically_relative(plan_.root);
            if (!rel.empty() && *rel.begin() != "..")
                return rel == "." ? QDir::toNativeSeparators(qpath(plan_.root))
                                  : QDir::toNativeSeparators(qpath(rel));
        }
        return QDir::toNativeSeparators(qpath(dir));
    };

    table_->setRowCount(static_cast<int>(plan_.moves.size()));
    std::size_t ready = 0;
    for (int row = 0; row < static_cast<int>(plan_.moves.size()); ++row) {
        const PlannedMove& m = plan_.moves[static_cast<std::size_t>(row)];
        const bool isFolder = m.message == kFolderRenameTag;
        QString status;
        QColor color;
        switch (m.status) {
        case MoveStatus::Ready:
            status = isFolder ? tr("Rename folder") : tr("Rename");
            ++ready;
            break;
        case MoveStatus::Collision:
            status = isFolder ? tr("Left as is: a folder with the new name is already here")
                              : tr("Left as is: name already taken");
            color = kGrey;
            break;
        default:
            status = isFolder ? tr("Path too long") : qstr(m.message);
            color = kRed;
            break;
        }

        QString now = qpath(m.source.filename());
        QString next = qpath(m.destination.filename());
        if (isFolder) {
            now = QStringLiteral("\U0001F4C1 ") + now;  // folder icon
            next = QStringLiteral("\U0001F4C1 ") + next;
        }
        const QStringList cells{where(m.destination.parent_path()), now, next, status};
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            item->setToolTip(cells[col]);
            if (color.isValid()) item->setForeground(color);
            table_->setItem(row, col, item);
        }
    }

    if (!anyFolders)
        summary_->setText(tr("Add the folders to search."));
    else if (plan_.moves.empty())
        summary_->setText(tr("Everything in these folders already uses the current names."));
    else if (ready == 0)
        summary_->setText(tr("Nothing to rename. %1 left as is (see Status).").arg(plan_.moves.size()));
    else
        summary_->setText(tr("%1 change(s) ready.").arg(ready) +
                          (ready < plan_.moves.size()
                               ? tr("  %1 left as is (see Status).").arg(plan_.moves.size() - ready)
                               : QString()));
    applyBtn_->setEnabled(ready > 0);
}

void UpdateNamesDialog::apply()
{
    if (!plan_.hasWork()) return;
    if (QMessageBox::question(this, tr("Update File Names"),
                              tr("Make %1 change(s)? Files and folders are renamed where they are.")
                                  .arg(plan_.count(MoveStatus::Ready)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;

    const ExecuteResult result = execute(plan_);
    if (!result.record.moves.empty())
        runGuarded(this, [&] { db_.saveBatch(result.record, caseId_); });

    if (!result.failures.empty()) {
        QStringList lines;
        for (std::size_t i = 0; i < result.failures.size() && i < 10; ++i)
            lines << QStringLiteral("• %1: %2").arg(qpath(result.failures[i].path.filename()),
                                                          qstr(result.failures[i].reason));
        if (result.failures.size() > 10) lines << tr("...and %1 more.").arg(result.failures.size() - 10);
        QMessageBox::warning(this, tr("Some changes weren't made"),
                             tr("%1 change(s) made. These couldn't be (if a PDF is open in another "
                                "program, or a folder is open in File Explorer, close it and try "
                                "again):\n\n%2")
                                 .arg(result.record.moves.size())
                                 .arg(lines.join('\n')));
    }

    // A chosen folder that was itself renamed: follow it in the list.
    for (const ExecutedMove& m : result.record.moves) folderList_->replaceFolder(m.from, m.to);
    scan();  // shows what (if anything) is left
}
