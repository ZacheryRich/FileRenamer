#include "UnlockDialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QCheckBox>
#include <QPushButton>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <set>

#include "QtHelpers.h"
#include "finrenamer/FolderSearch.h"
#include "finrenamer/Utf8Path.h"

using namespace finrenamer;
using namespace ui;

namespace {

enum Column { ColFile = 0, ColFolder, ColStatus, ColPassword, ColResult, ColCount };

// The Password column: typed text is shown as dots; the real text is kept in UserRole.
class PasswordDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* edit = new QLineEdit(parent);
        edit->setEchoMode(QLineEdit::Password);
        return edit;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        static_cast<QLineEdit*>(editor)->setText(index.data(Qt::UserRole).toString());
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        const QString text = static_cast<QLineEdit*>(editor)->text();
        model->setData(index, text, Qt::UserRole);
        model->setData(index, QString(text.size(), QChar(0x2022)), Qt::DisplayRole);
    }
};

QHBoxLayout* pathRow(QLineEdit*& edit, const QString& objectName, QPushButton*& browse, const QString& browseName)
{
    edit = new QLineEdit;
    edit->setObjectName(objectName);
    browse = new QPushButton(QObject::tr("Browse..."));
    browse->setObjectName(browseName);
    browse->setAutoDefault(false);
    auto* row = new QHBoxLayout;
    row->addWidget(edit, 1);
    row->addWidget(browse);
    return row;
}

QPushButton* button(const QString& text, const QString& objectName)
{
    auto* b = new QPushButton(text);
    b->setObjectName(objectName);
    b->setAutoDefault(false);
    return b;
}

}  // namespace

UnlockDialog::UnlockDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("PDF Unlocker - %1").arg(appTitle()));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    setAcceptDrops(true);
    buildUi();

    QSettings settings(settingsFile(), QSettings::IniFormat);
    output_->setText(QDir::toNativeSeparators(settings.value("unlock/outputFolder").toString()));
    subfolders_->setChecked(settings.value("unlock/subfolders", true).toBool());
    updateButtons();
}

UnlockDialog::~UnlockDialog()
{
    disconnectChildren(this);
}

void UnlockDialog::buildUi()
{
    // ---- Files ----
    auto* filesBox = new QGroupBox(tr("PDFs to unlock"));
    auto* filesLayout = new QVBoxLayout(filesBox);
    auto* hint = new QLabel(tr("Add PDFs or whole folders (or drag them in). Restrictions such as no printing or copying are removed "
                               "without a password. A PDF that asks for a password to open needs that password - type it in the "
                               "Password column, or use the box below for all of them."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(placeholder-text);");
    filesLayout->addWidget(hint);

    auto* addFilesBtn = button(tr("Add Files..."), "addFiles");
    auto* addFolderBtn = button(tr("Add Folder..."), "addFolder");
    subfolders_ = new QCheckBox(tr("Include subfolders"));
    subfolders_->setObjectName("includeSubfolders");
    removeBtn_ = button(tr("Remove Selected"), "removeSelected");
    clearBtn_ = button(tr("Clear List"), "clearList");
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(addFilesBtn);
    addRow->addWidget(addFolderBtn);
    addRow->addWidget(subfolders_);
    addRow->addStretch();
    addRow->addWidget(removeBtn_);
    addRow->addWidget(clearBtn_);
    filesLayout->addLayout(addRow);

    table_ = new QTableWidget(0, ColCount);
    table_->setObjectName("files");
    table_->setHorizontalHeaderLabels({tr("File"), tr("Folder"), tr("Status"), tr("Password"), tr("Result")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table_->setItemDelegateForColumn(ColPassword, new PasswordDelegate(table_));
    table_->verticalHeader()->setVisible(false);
    for (int c = 0; c < ColCount; ++c) table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Interactive);
    table_->horizontalHeader()->resizeSection(ColFile, 230);
    table_->horizontalHeader()->resizeSection(ColFolder, 190);
    table_->horizontalHeader()->resizeSection(ColStatus, 260);
    table_->horizontalHeader()->resizeSection(ColPassword, 90);
    table_->setTextElideMode(Qt::ElideRight);
    table_->horizontalHeader()->setStretchLastSection(true);
    filesLayout->addWidget(table_, 1);

    batchPassword_ = new QLineEdit;
    batchPassword_->setObjectName("batchPassword");
    batchPassword_->setEchoMode(QLineEdit::Password);
    batchPassword_->setPlaceholderText(tr("One password to try on every PDF that needs one"));
    applyPasswordBtn_ = button(tr("Try on Password-Protected PDFs"), "applyPassword");
    auto* passRow = new QHBoxLayout;
    passRow->addWidget(new QLabel(tr("Password:")));
    passRow->addWidget(batchPassword_, 1);
    passRow->addWidget(applyPasswordBtn_);
    filesLayout->addLayout(passRow);

    // ---- Output ----
    QPushButton* browseOut;
    auto* outBox = new QGroupBox(tr("Save the unlocked copies in"));
    auto* outLayout = new QVBoxLayout(outBox);
    outLayout->addLayout(pathRow(output_, "outputFolder", browseOut, "browseOutput"));
    auto* outHint = new QLabel(tr("The originals are never changed. If a name is already taken in this folder, \" (2)\" is added."));
    outHint->setStyleSheet("color: palette(placeholder-text);");
    outLayout->addWidget(outHint);

    // ---- Footer ----
    summary_ = new QLabel;
    summary_->setObjectName("summary");
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    openOutputBtn_ = button(tr("Open Output Folder"), "openOutput");
    unlockBtn_ = button(tr("Unlock PDFs"), "unlock");
    auto* closeBtn = button(tr("Close"), "close");
    auto* footer = new QHBoxLayout;
    footer->addWidget(summary_, 1);
    footer->addWidget(openOutputBtn_);
    footer->addWidget(unlockBtn_);
    footer->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(filesBox, 1);
    layout->addWidget(outBox);
    layout->addLayout(footer);

    connect(addFilesBtn, &QPushButton::clicked, this, &UnlockDialog::addFiles);
    connect(addFolderBtn, &QPushButton::clicked, this, &UnlockDialog::addFolder);
    connect(removeBtn_, &QPushButton::clicked, this, &UnlockDialog::removeSelected);
    connect(clearBtn_, &QPushButton::clicked, this, &UnlockDialog::clearList);
    connect(applyPasswordBtn_, &QPushButton::clicked, this, &UnlockDialog::applyBatchPassword);
    connect(batchPassword_, &QLineEdit::returnPressed, this, &UnlockDialog::applyBatchPassword);
    connect(browseOut, &QPushButton::clicked, this, &UnlockDialog::browseOutput);
    connect(output_, &QLineEdit::textChanged, this, [this] { updateButtons(); });
    connect(openOutputBtn_, &QPushButton::clicked, this, &UnlockDialog::openOutput);
    connect(unlockBtn_, &QPushButton::clicked, this, [this] { unlockAll(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (refreshing_ || !item || item->column() != ColPassword) return;
        const int row = item->row();
        if (row < 0 || row >= static_cast<int>(entries_.size())) return;
        setPassword(row, item->data(Qt::UserRole).toString());
    });

    resize(980, 640);
}

// ---- adding files ---------------------------------------------------------------------------

void UnlockDialog::addPaths(const QStringList& paths, bool includeSubfolders)
{
    std::vector<std::filesystem::path> files, folders;
    for (const QString& p : paths) {
        const QFileInfo info(p);
        if (info.isDir()) folders.push_back(toPath(info.absoluteFilePath()));
        else if (info.isFile()) files.push_back(toPath(info.absoluteFilePath()));
    }
    const auto found = findPdfs(folders, includeSubfolders);
    files.insert(files.end(), found.begin(), found.end());

    std::set<std::string> have;
    for (const auto& e : entries_) have.insert(caseFoldKey(e.file));
    int added = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const auto& f : files) {
        if (!have.insert(caseFoldKey(f)).second) continue;
        Entry entry;
        entry.file = f;
        recheck(entry);
        entries_.push_back(std::move(entry));
        ++added;
    }
    QApplication::restoreOverrideCursor();
    refreshTable();
    if (added == 0 && !paths.isEmpty()) report(tr("No new PDFs were found there."));
    else report(tr("Added %n PDF(s).", nullptr, added));
}

void UnlockDialog::addFiles()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("PDFs to unlock"), settings.value("unlock/lastFolder", QDir::homePath()).toString(),
                                                            tr("PDF files (*.pdf);;All files (*)"));
    if (files.isEmpty()) return;
    settings.setValue("unlock/lastFolder", QFileInfo(files.first()).absolutePath());
    addPaths(files, false);
}

void UnlockDialog::addFolder()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder of PDFs to unlock"),
                                                          settings.value("unlock/lastFolder", QDir::homePath()).toString());
    if (dir.isEmpty()) return;
    settings.setValue("unlock/lastFolder", dir);
    settings.setValue("unlock/subfolders", subfolders_->isChecked());
    addPaths({dir}, subfolders_->isChecked());
}

void UnlockDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void UnlockDialog::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile()) paths << url.toLocalFile();
    if (!paths.isEmpty()) addPaths(paths, subfolders_->isChecked());
}

void UnlockDialog::removeSelected()
{
    std::set<int, std::greater<int>> rows;
    for (const auto& index : table_->selectionModel()->selectedRows()) rows.insert(index.row());
    for (int r : rows)
        if (r >= 0 && r < static_cast<int>(entries_.size())) entries_.erase(entries_.begin() + r);
    refreshTable();
}

void UnlockDialog::clearList()
{
    entries_.clear();
    refreshTable();
    report(QString());
}

// ---- checking ------------------------------------------------------------------------------------

void UnlockDialog::recheck(Entry& entry)
{
    entry.info = inspectPdf(entry.file, entry.password);
    // A password that doesn't open the file is "wrong"; one that wasn't needed is just ignored.
    if (entry.info.lock != PdfLock::NeedsPassword) entry.info.wrongPassword = false;
}

void UnlockDialog::setPassword(int row, const QString& password)
{
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;
    Entry& e = entries_[static_cast<std::size_t>(row)];
    e.password = stdstr(password);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    recheck(e);
    QApplication::restoreOverrideCursor();
    e.result.clear();
    refreshTable();
}

void UnlockDialog::setBatchPassword(const QString& password) { batchPassword_->setText(password); }

void UnlockDialog::applyBatchPassword()
{
    const std::string pw = stdstr(batchPassword_->text());
    if (pw.empty()) return;
    int opened = 0, tried = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (auto& e : entries_) {
        if (e.done || e.info.lock != PdfLock::NeedsPassword) continue;
        ++tried;
        e.password = pw;
        recheck(e);
        if (e.info.lock != PdfLock::NeedsPassword) ++opened;
        else e.password.clear();  // don't keep a password that didn't work
    }
    QApplication::restoreOverrideCursor();
    refreshTable();
    if (tried == 0) report(tr("No PDF in the list needs a password."));
    else report(tr("The password opened %1 of %2 password-protected PDF(s).").arg(opened).arg(tried), opened == 0);
}

QString UnlockDialog::describe(const Entry& e)
{
    switch (e.info.lock) {
    case PdfLock::NotProtected: return QObject::tr("Not protected");
    case PdfLock::Restricted: {
        QStringList r;
        for (const auto& s : e.info.restrictions) r << qstr(s);
        QString text = r.isEmpty() ? QObject::tr("Encrypted") : QObject::tr("Restricted: %1").arg(r.join(", "));
        if (!e.password.empty()) text += QObject::tr(" (password accepted)");
        return text;
    }
    case PdfLock::NeedsPassword: return e.info.wrongPassword ? QObject::tr("Wrong password") : QObject::tr("Needs a password");
    case PdfLock::Unreadable: return QObject::tr("Can't read this PDF");
    }
    return {};
}

QString UnlockDialog::statusOf(int row) const
{
    return row >= 0 && row < fileCount() ? describe(entries_[static_cast<std::size_t>(row)]) : QString();
}

QString UnlockDialog::resultOf(int row) const
{
    return row >= 0 && row < fileCount() ? qstr(entries_[static_cast<std::size_t>(row)].result) : QString();
}

QString UnlockDialog::summaryText() const { return summary_->text(); }

void UnlockDialog::refreshTable()
{
    refreshing_ = true;
    table_->setRowCount(static_cast<int>(entries_.size()));
    for (int r = 0; r < static_cast<int>(entries_.size()); ++r) {
        const Entry& e = entries_[static_cast<std::size_t>(r)];
        auto put = [&](int col, const QString& text, bool editable = false) {
            auto* item = table_->item(r, col);
            if (!item) {
                item = new QTableWidgetItem;
                table_->setItem(r, col, item);
            }
            item->setText(text);
            Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (editable) flags |= Qt::ItemIsEditable;
            item->setFlags(flags);
            return item;
        };
        put(ColFile, qstr(utf8FromPath(e.file.filename())))->setToolTip(qpath(e.file));
        put(ColFolder, QDir::toNativeSeparators(qpath(e.file.parent_path())));
        auto* status = put(ColStatus, describe(e));
        status->setToolTip(e.info.lock == PdfLock::Unreadable ? qstr(e.info.message) : describe(e));
        const bool bad = e.info.lock == PdfLock::Unreadable || (e.info.lock == PdfLock::NeedsPassword && e.info.wrongPassword);
        status->setForeground(bad ? QBrush(QColor("#c42b1c")) : QBrush());
        auto* pw = put(ColPassword, QString(e.password.size(), QChar(0x2022)), !e.done && e.info.lock != PdfLock::NotProtected && e.info.lock != PdfLock::Unreadable);
        pw->setData(Qt::UserRole, qstr(e.password));
        pw->setToolTip(tr("Double-click to type the password for this PDF"));
        put(ColResult, qstr(e.result))->setToolTip(qstr(e.result));
    }
    refreshing_ = false;
    updateButtons();
}

// ---- output + unlocking -------------------------------------------------------------------------------

void UnlockDialog::setOutputFolder(const QString& folder) { output_->setText(folder); }

void UnlockDialog::browseOutput()
{
    const QString start = output_->text().trimmed().isEmpty() ? QDir::homePath() : output_->text();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder for the unlocked copies"), start);
    if (!dir.isEmpty()) output_->setText(QDir::toNativeSeparators(dir));
}

void UnlockDialog::openOutput()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(output_->text().trimmed()));
}

void UnlockDialog::updateButtons()
{
    const bool haveFiles = !entries_.empty();
    const bool haveOut = !output_->text().trimmed().isEmpty();
    bool anyPassword = false;
    for (const auto& e : entries_) anyPassword = anyPassword || (!e.done && e.info.lock == PdfLock::NeedsPassword);
    removeBtn_->setEnabled(table_->selectionModel() && table_->selectionModel()->hasSelection());
    clearBtn_->setEnabled(haveFiles);
    applyPasswordBtn_->setEnabled(anyPassword);
    unlockBtn_->setEnabled(haveFiles && haveOut);
    unlockBtn_->setToolTip(haveOut ? QString() : tr("Choose the folder for the unlocked copies first"));
    openOutputBtn_->setEnabled(haveOut && QFileInfo(output_->text().trimmed()).isDir());
}

void UnlockDialog::report(const QString& text, bool error)
{
    summary_->setText(text);
    summary_->setStyleSheet(error ? "QLabel { color: #c42b1c; }" : QString());
}

UnlockDialog::Counts UnlockDialog::unlockAll()
{
    Counts counts;
    const QString outText = output_->text().trimmed();
    if (outText.isEmpty() || entries_.empty()) return counts;
    const auto outFolder = toPath(outText);
    std::error_code ec;
    std::filesystem::create_directories(outFolder, ec);
    if (!std::filesystem::is_directory(outFolder, ec)) {
        report(tr("The output folder couldn't be created: %1").arg(QDir::toNativeSeparators(outText)), true);
        return counts;
    }
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("unlock/outputFolder", outText);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (auto& e : entries_) {
        if (e.done) {
            e.result = "Already unlocked.";
            ++counts.skipped;
            continue;
        }
        switch (e.info.lock) {
        case PdfLock::NotProtected:
            e.result = "Not protected - nothing to do, no copy made.";
            ++counts.skipped;
            break;
        case PdfLock::NeedsPassword:
            e.result = e.info.wrongPassword ? "Skipped: that password is wrong." : "Skipped: needs the password.";
            ++counts.skipped;
            break;
        case PdfLock::Unreadable:
            e.result = "Skipped: this PDF can't be read.";
            ++counts.failed;
            break;
        case PdfLock::Restricted: {
            const auto dest = unlockedCopyPath(outFolder, e.file);
            const UnlockResult r = unlockPdf(e.file, dest, e.password);
            switch (r.outcome) {
            case UnlockOutcome::Unlocked:
                e.done = true;
                e.result = "Saved as " + utf8FromPath(dest.filename()) + (r.repaired ? " (the PDF had small problems that were repaired)" : "");
                ++counts.unlocked;
                break;
            case UnlockOutcome::NotProtected:
                e.result = r.message;
                ++counts.skipped;
                break;
            case UnlockOutcome::NeedsPassword:
            case UnlockOutcome::WrongPassword:
                e.result = r.message;
                ++counts.skipped;
                break;
            case UnlockOutcome::Failed:
                e.result = "Failed: " + r.message;
                ++counts.failed;
                break;
            }
            break;
        }
        }
    }
    QApplication::restoreOverrideCursor();
    refreshTable();
    QString text = tr("Unlocked %1 PDF(s) into \"%2\".").arg(counts.unlocked).arg(QDir::toNativeSeparators(outText));
    if (counts.skipped > 0) text += tr("  Skipped %1.").arg(counts.skipped);
    if (counts.failed > 0) text += tr("  %1 couldn't be unlocked.").arg(counts.failed);
    report(text, counts.unlocked == 0 && counts.failed > 0);
    return counts;
}
