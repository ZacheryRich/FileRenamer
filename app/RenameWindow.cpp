#include "RenameWindow.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "AccountDialog.h"
#include "DateWidgets.h"
#include "PdfPreview.h"
#include "QtHelpers.h"
#include "finrenamer/FilenameBuilder.h"

using namespace finrenamer;
using namespace ui;
namespace fs = std::filesystem;

namespace {

enum Column { ColFile = 0, ColNewName = 1, ColFolder = 2, ColStatus = 3 };

// Account dropdown entries carry the account id; number dropdown entries carry
// the number to use ("" = the account's current number).
constexpr int kAccountIdRole = Qt::UserRole;
constexpr int kNumberRole = Qt::UserRole;

const QColor kGreen(0x10, 0x7c, 0x10);
const QColor kRed(0xc4, 0x2b, 0x1c);
const QColor kGrey(0x70, 0x70, 0x70);

struct StatusView {
    QString text;
    QColor color;  // invalid = default text color
};

StatusView describe(const SessionRow& row, const PlannedMove& move)
{
    if (row.done) return {QObject::tr("Renamed"), kGrey};
    if (!row.failure.empty() && move.status == MoveStatus::Ready)
        return {QObject::tr("Couldn't rename: %1").arg(qstr(row.failure)), kRed};

    switch (move.status) {
    case MoveStatus::Ready:
        return {move.message.empty() ? QObject::tr("Ready")
                                     : QObject::tr("Ready (name taken, number added)"),
                kGreen};
    case MoveStatus::Unchanged: return {QObject::tr("Already named correctly"), kGrey};
    case MoveStatus::Skipped: return {QObject::tr("Skipped"), kGrey};
    case MoveStatus::Incomplete:
        if (!move.message.empty()) return {qstr(move.message), {}};  // what an email/text/letter still needs
        if (!row.accountId && !row.date) return {QObject::tr("Needs account and date"), {}};
        if (!row.accountId) return {QObject::tr("Needs account"), {}};
        return {QObject::tr("Needs date"), {}};
    case MoveStatus::Collision: return {QObject::tr("Name already taken"), kRed};
    case MoveStatus::Error: return {qstr(move.message), kRed};
    }
    return {};
}

QString relativeName(const fs::path& root, const fs::path& p)
{
    if (p.empty()) return {};
    return QDir::toNativeSeparators(qpath(p.lexically_relative(root)));
}

QTableWidgetItem* cell(QTableWidget* table, int row, int col)
{
    QTableWidgetItem* item = table->item(row, col);
    if (!item) {
        item = new QTableWidgetItem;
        table->setItem(row, col, item);
    }
    return item;
}

}  // namespace

// ---------------------------------------------------------------------------

RenameWindow::RenameWindow(Database& db, std::int64_t caseId, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId)
{
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    std::optional<ClientCase> c;
    runGuarded(this, [&] { c = db_.getCase(caseId_); });
    setWindowTitle(tr("File Renamer - %1").arg(c ? qstr(c->clientName) : QString()));

    buildUi();
    reloadAccounts();
    rebuildTable();
    resize(1280, 740);
}

RenameWindow::~RenameWindow()
{
    disconnectChildren(this);
}

void RenameWindow::buildUi()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);

    // ---- Top: folder + options ----
    auto* chooseBtn = new QPushButton(tr("Choose Folder..."));
    refreshBtn_ = new QPushButton(tr("Refresh"));
    refreshBtn_->setToolTip(tr("Look for PDFs added to the folder since it was opened"));
    folderLabel_ = new QLabel(tr("No folder chosen"));
    folderLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(chooseBtn);
    folderRow->addWidget(refreshBtn_);
    folderRow->addWidget(folderLabel_, 1);

    // Folder sorting is for statements only, so its settings live in their own window.
    statementDialog_ = new QDialog(this);
    statementDialog_->setObjectName("statementOptions");
    statementDialog_->setWindowTitle(tr("Statement Options"));
    byAccount_ = new QCheckBox(tr("Sort into account folders"));
    byYear_ = new QCheckBox(tr("Sort into year folders"));
    order_ = new QComboBox;
    order_->addItems({tr("Account → Year"), tr("Year → Account")});
    {
        auto* note = new QLabel(tr("These settings apply to financial statements only.\n"
                                   "Emails, texts, letters and other files are renamed where they are."));
        note->setWordWrap(true);
        auto* orderRow = new QHBoxLayout;
        orderRow->addWidget(new QLabel(tr("Folder order:")));
        orderRow->addWidget(order_, 1);
        auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close);
        closeBox->button(QDialogButtonBox::Close)->setObjectName("closeStatementOptions");
        connect(closeBox, &QDialogButtonBox::rejected, statementDialog_, &QDialog::accept);
        auto* layout = new QVBoxLayout(statementDialog_);
        layout->addWidget(note);
        layout->addSpacing(8);
        layout->addWidget(byAccount_);
        layout->addWidget(byYear_);
        layout->addLayout(orderRow);
        layout->addSpacing(8);
        layout->addWidget(closeBox);
    }
    statementBtn_ = new QPushButton(tr("Statement Options..."));
    statementBtn_->setObjectName("statementOptionsButton");
    statementBtn_->setToolTip(tr("Sort statements into account and year folders. Applies to financial statements only."));
    collisions_ = new QComboBox;
    collisions_->addItems({tr("Add a number: \"(2)\""), tr("Don't rename it")});

    byAccount_->setChecked(settings.value("sort/byAccount", false).toBool());
    byYear_->setChecked(settings.value("sort/byYear", false).toBool());
    order_->setCurrentIndex(settings.value("sort/order", 0).toInt());
    collisions_->setCurrentIndex(settings.value("sort/collisions", 0).toInt());

    auto* optionsRow = new QHBoxLayout;
    optionsRow->addWidget(statementBtn_);
    optionsRow->addSpacing(24);
    optionsRow->addWidget(new QLabel(tr("If a name is already taken:")));
    optionsRow->addWidget(collisions_);
    optionsRow->addStretch();

    // ---- Left: file table ----
    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({tr("File"), tr("New name"), tr("Into folder"), tr("Status")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(table_->fontMetrics().height() + 10);
    table_->horizontalHeader()->setSectionResizeMode(ColFile, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(ColNewName, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColFolder, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(ColStatus, QHeaderView::Interactive);
    table_->setColumnWidth(ColFile, 160);
    table_->setColumnWidth(ColFolder, 180);
    table_->setColumnWidth(ColStatus, 150);

    // ---- Right: editor for the selected file ----
    fileLabel_ = new QLabel;
    QFont bold = fileLabel_->font();
    bold.setBold(true);
    fileLabel_->setFont(bold);
    fileLabel_->setWordWrap(true);

    kind_ = new QComboBox;
    kind_->addItem(tr("Statement"), static_cast<int>(DocKind::Statement));
    kind_->addItem(tr("Email"), static_cast<int>(DocKind::Email));
    kind_->addItem(tr("Text"), static_cast<int>(DocKind::Text));
    kind_->addItem(tr("Letter"), static_cast<int>(DocKind::Letter));
    kind_->addItem(tr("Other"), static_cast<int>(DocKind::Other));
    kind_->setToolTip(tr("What this file is. Statements are named from an account; the others from what you type."));

    account_ = new QComboBox;
    account_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    account_->setMinimumContentsLength(28);
    newAccountBtn_ = new QPushButton(tr("New Account..."));
    accountBox_ = new QWidget;
    auto* accountRow = new QHBoxLayout(accountBox_);
    accountRow->setContentsMargins(0, 0, 0, 0);
    accountRow->addWidget(account_, 1);
    accountRow->addWidget(newAccountBtn_);

    textStyle_ = new QComboBox;
    textStyle_->addItem(tr("One message"), static_cast<int>(TextStyle::Single));
    textStyle_->addItem(tr("Conversation (two people)"), static_cast<int>(TextStyle::Conversation));
    textStyle_->addItem(tr("Group chat"), static_cast<int>(TextStyle::Group));
    label_ = new QLineEdit;
    from_ = new QLineEdit;
    to_ = new QLineEdit;
    description_ = new QLineEdit;
    description_->setPlaceholderText(tr("Optional"));

    date_ = new DateSpecEditor;

    skip_ = new QCheckBox(tr("&Skip this file (leave its name as it is)"));

    rowStatus_ = new QLabel;
    rowStatus_->setWordWrap(true);
    rowStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rowStatus_->setMinimumHeight(rowStatus_->fontMetrics().height() * 3);
    rowStatus_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    openBtn_ = new QPushButton(tr("&Open PDF"));
    openBtn_->setToolTip(tr("Open in your PDF viewer (Ctrl+O). Close it again before applying."));
    nextBtn_ = new QPushButton(tr("Next File  ▶"));
    nextBtn_->setToolTip(tr("Enter: go to the next file, carrying this account (or these details) forward"));

    auto* editorButtons = new QHBoxLayout;
    editorButtons->addWidget(openBtn_);
    editorButtons->addStretch();
    editorButtons->addWidget(nextBtn_);

    auto* hint = new QLabel(tr("Enter: next file (the account or details carry forward)\n"
                               "Ctrl+Enter: apply    Double-click a file: open it"));
    hint->setStyleSheet("color: palette(placeholder-text);");

    form_ = new QFormLayout;
    auto* form = form_;
    number_ = new QComboBox;
    number_->setObjectName("number");
    number_->setToolTip(tr("The account number this statement shows. Older numbers are for "
                           "statements from before the number changed."));
    numberBox_ = new QWidget;
    auto* numberRow = new QHBoxLayout(numberBox_);
    numberRow->setContentsMargins(0, 0, 0, 0);
    numberRow->addWidget(number_);
    numberRow->addStretch();

    form->addRow(tr("Type:"), kind_);
    form->addRow(tr("Account:"), accountBox_);
    form->addRow(tr("Number:"), numberBox_);
    form->addRow(tr("Kind of text:"), textStyle_);
    form->addRow(tr("Label:"), label_);
    form->addRow(tr("From:"), from_);
    form->addRow(tr("To:"), to_);
    form->addRow(tr("Description:"), description_);
    form->addRow(tr("Date:"), date_);
    form->addRow(QString(), skip_);

    editorBox_ = new QGroupBox(tr("Selected file"));
    auto* editorLayout = new QVBoxLayout(editorBox_);
    editorLayout->addWidget(fileLabel_);
    editorLayout->addLayout(form);
    editorLayout->addWidget(rowStatus_);
    editorLayout->addLayout(editorButtons);
    editorLayout->addWidget(hint);

    // ---- Right: PDF preview ----
    pdf_ = new PdfPreview;
    pdf_->setObjectName("preview");

    // Left column: file list over the editor. Right: the preview, full height.
    leftSplitter_ = new QSplitter(Qt::Vertical);
    leftSplitter_->addWidget(table_);
    leftSplitter_->addWidget(editorBox_);
    leftSplitter_->setStretchFactor(0, 1);
    leftSplitter_->setStretchFactor(1, 0);
    leftSplitter_->setSizes({400, 280});

    mainSplitter_ = new QSplitter;
    mainSplitter_->addWidget(leftSplitter_);
    mainSplitter_->addWidget(pdf_);
    mainSplitter_->setStretchFactor(0, 3);
    mainSplitter_->setStretchFactor(1, 2);
    mainSplitter_->setSizes({760, 600});
    leftSplitter_->restoreState(settings.value("rename/leftSplitter").toByteArray());
    mainSplitter_->restoreState(settings.value("rename/mainSplitter").toByteArray());

    // ---- Bottom ----
    summary_ = new QLabel;
    undoBtn_ = new QPushButton(tr("Undo Last Apply"));
    applyBtn_ = new QPushButton(tr("Apply Renames"));
    applyBtn_->setToolTip(tr("Rename every file marked Ready (Ctrl+Enter)"));
    auto* closeBtn = new QPushButton(tr("Close"));

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(summary_, 1);
    bottom->addWidget(undoBtn_);
    bottom->addWidget(applyBtn_);
    bottom->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(folderRow);
    layout->addLayout(optionsRow);
    layout->addWidget(mainSplitter_, 1);
    layout->addLayout(bottom);

    editFields_ = {kind_, account_, newAccountBtn_, textStyle_, label_, from_, to_, description_, date_};

    // Names for automated GUI tests.
    kind_->setObjectName("kind");
    textStyle_->setObjectName("textStyle");
    label_->setObjectName("label");
    from_->setObjectName("from");
    to_->setObjectName("to");
    description_->setObjectName("description");
    table_->setObjectName("files");
    account_->setObjectName("account");
    date_->setObjectName("date");
    skip_->setObjectName("skip");
    nextBtn_->setObjectName("next");
    applyBtn_->setObjectName("apply");
    undoBtn_->setObjectName("undo");
    summary_->setObjectName("summary");
    byAccount_->setObjectName("byAccount");
    byYear_->setObjectName("byYear");

    // Enter = Next File. Other buttons must not grab Enter when focused.
    for (auto* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    nextBtn_->setDefault(true);

    // ---- Wiring ----
    connect(chooseBtn, &QPushButton::clicked, this, &RenameWindow::promptForFolder);
    connect(refreshBtn_, &QPushButton::clicked, this, &RenameWindow::refreshFolder);
    connect(statementBtn_, &QPushButton::clicked, this, [this] { statementDialog_->exec(); });
    for (auto* box : {byAccount_, byYear_})
        connect(box, &QCheckBox::toggled, this, [this] { saveSettings(); updatePreview(); });
    for (auto* combo : {order_, collisions_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { saveSettings(); updatePreview(); });

    connect(table_, &QTableWidget::currentCellChanged, this, &RenameWindow::loadEditor);
    connect(table_, &QTableWidget::cellDoubleClicked, this, &RenameWindow::openPdf);

    connect(account_, &QComboBox::currentIndexChanged, this, [this] {
        if (loadingEditor_) return;
        fillNumbers(account_->currentData(kAccountIdRole).isValid()
                        ? std::optional<std::int64_t>(account_->currentData(kAccountIdRole).toLongLong())
                        : std::nullopt,
                    {});  // a different account starts on its current number
        commitEditor();
    });
    connect(number_, &QComboBox::currentIndexChanged, this, &RenameWindow::commitEditor);
    connect(kind_, &QComboBox::currentIndexChanged, this, &RenameWindow::kindChanged);
    connect(textStyle_, &QComboBox::currentIndexChanged, this, &RenameWindow::kindChanged);
    for (auto* edit : {label_, from_, to_, description_})
        connect(edit, &QLineEdit::textChanged, this, &RenameWindow::commitEditor);
    connect(date_, &DateSpecEditor::changed, this, &RenameWindow::commitEditor);
    connect(skip_, &QCheckBox::toggled, this, &RenameWindow::commitEditor);
    connect(newAccountBtn_, &QPushButton::clicked, this, &RenameWindow::newAccount);
    connect(openBtn_, &QPushButton::clicked, this, &RenameWindow::openPdf);
    connect(nextBtn_, &QPushButton::clicked, this, &RenameWindow::goToNext);

    connect(undoBtn_, &QPushButton::clicked, this, &RenameWindow::undoLast);
    connect(applyBtn_, &QPushButton::clicked, this, &RenameWindow::apply);
    connect(closeBtn, &QPushButton::clicked, this, &RenameWindow::reject);

    for (auto key : {Qt::Key_Return, Qt::Key_Enter})
        connect(new QShortcut(QKeySequence(Qt::CTRL | key), this), &QShortcut::activated, this,
                &RenameWindow::apply);
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_O), this), &QShortcut::activated, this,
            &RenameWindow::openPdf);
}

// ---------------------------------------------------------------------------
// Folder

bool RenameWindow::promptForFolder()
{
    if (session_ && session_->hasUnappliedEntries()) {
        const auto answer = QMessageBox::question(
            this, tr("Choose Folder"),
            tr("Entries you haven't applied in this folder will be lost. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return false;
    }

    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Choose the folder with the statements"), settings.value("lastFolder").toString());
    if (folder.isEmpty()) return false;

    openFolder(folder);
    return true;
}

void RenameWindow::openFolder(const QString& folder)
{
    std::vector<Account> accounts;
    runGuarded(this, [&] { accounts = db_.loadAccounts(caseId_); });
    session_ = std::make_unique<RenameSession>(toPath(folder), std::move(accounts));

    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("lastFolder", folder);

    folderLabel_->setText(QDir::toNativeSeparators(qpath(session_->root())));
    rebuildTable();
    selectRow(0);
}

void RenameWindow::refreshFolder()
{
    if (!session_) return;
    commitEditor();
    const auto cur = currentRow();
    const fs::path keep = cur ? session_->row(*cur).path : fs::path();

    session_->refresh();
    rebuildTable();

    for (std::size_t i = 0; i < session_->size(); ++i)
        if (session_->row(i).path == keep) return selectRow(static_cast<int>(i));
    selectRow(0);
}

void RenameWindow::reloadAccounts()
{
    std::vector<Account> accounts;
    if (!runGuarded(this, [&] { accounts = db_.loadAccounts(caseId_); })) return;
    accounts_ = accounts;

    const QVariant keepId = account_->currentData(kAccountIdRole);
    const std::string keepNumber = stdstr(number_->currentData(kNumberRole).toString());
    {
        const QSignalBlocker blockAccount(account_);
        account_->clear();
        account_->addItem(tr("Choose an account..."));
        for (const Account& a : accounts_) {
            account_->addItem(qstr(accountLabel(a)), QVariant::fromValue<qlonglong>(a.id));
            account_->setItemData(account_->count() - 1, qstr(a.institution), Qt::ToolTipRole);
        }
        const int index = keepId.isValid() ? accountIndex(keepId.toLongLong()) : 0;
        account_->setCurrentIndex(index >= 0 ? index : 0);
    }
    fillNumbers(keepId.isValid() ? std::optional<std::int64_t>(keepId.toLongLong()) : std::nullopt,
                keepNumber);
    if (session_) session_->setAccounts(std::move(accounts));
}

int RenameWindow::accountIndex(std::optional<std::int64_t> id) const
{
    if (!id) return 0;
    for (int i = 1; i < account_->count(); ++i)
        if (account_->itemData(i, kAccountIdRole).toLongLong() == *id) return i;
    return -1;
}

void RenameWindow::fillNumbers(std::optional<std::int64_t> accountId, const std::string& select)
{
    const QSignalBlocker block(number_);
    number_->clear();

    const Account* account = nullptr;
    for (const Account& a : accounts_)
        if (accountId && a.id == *accountId) account = &a;

    if (account && !account->isCombined()) {  // a combined statement uses each account's current number
        // Newest first: the current number, then the older ones.
        number_->addItem(tr("%1  (current)").arg(qstr(account->lastFour)), QString());
        for (const auto& n : account->previousLastFour) number_->addItem(qstr(n), qstr(n));
        const int index = number_->findData(qstr(select), kNumberRole);
        number_->setCurrentIndex(index >= 0 ? index : 0);
    }
    // Only worth choosing when the account has had more than one number.
    number_->setEnabled(account_->isEnabled() && number_->count() > 1);
}

// ---------------------------------------------------------------------------
// Table

void RenameWindow::rebuildTable()
{
    const QSignalBlocker block(table_);
    const int rows = session_ ? static_cast<int>(session_->size()) : 0;
    table_->setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        const SessionRow& row = session_->row(static_cast<std::size_t>(r));
        QTableWidgetItem* item = cell(table_, r, ColFile);
        item->setText(qpath(row.originalPath.filename()));
        item->setToolTip(QDir::toNativeSeparators(qpath(row.originalPath)));
    }
    updatePreview();
    loadEditor();
}

void RenameWindow::updatePreview()
{
    if (!session_) {
        preview_.clear();
        updateSummary();
        return;
    }
    preview_ = session_->preview(options());

    for (int r = 0; r < table_->rowCount(); ++r) {
        const SessionRow& row = session_->row(static_cast<std::size_t>(r));
        const PlannedMove& move = preview_[static_cast<std::size_t>(r)];
        const StatusView status = describe(row, move);

        const bool showName = row.done || move.status == MoveStatus::Ready ||
                              move.status == MoveStatus::Unchanged;
        const fs::path dest = row.done ? row.path : move.destination;
        const QString name = showName ? qpath(dest.filename()) : QString();
        const QString folder = showName ? relativeName(session_->root(), dest.parent_path()) : QString();

        QTableWidgetItem* nameItem = cell(table_, r, ColNewName);
        nameItem->setText(name);
        nameItem->setToolTip(name);

        QTableWidgetItem* folderItem = cell(table_, r, ColFolder);
        if (row.isDocument() && showName) {
            // Account and year folders are for statements only.
            folderItem->setText(tr("(stays here)"));
            folderItem->setToolTip(tr("Emails, texts, letters and other files are renamed where they are."));
        } else {
            folderItem->setText(folder == QStringLiteral(".") ? QString() : folder);
            folderItem->setToolTip(folderItem->text());
        }

        QTableWidgetItem* statusItem = cell(table_, r, ColStatus);
        statusItem->setText(status.text);
        statusItem->setToolTip(status.text);
        statusItem->setForeground(status.color.isValid() ? QBrush(status.color) : QBrush());

        const QBrush rowBrush = row.done ? QBrush(kGrey) : QBrush();
        cell(table_, r, ColFile)->setForeground(rowBrush);
        nameItem->setForeground(rowBrush);
        folderItem->setForeground(row.isDocument() ? QBrush(kGrey) : rowBrush);
    }
    // The folder column only matters when sorting into subfolders.
    table_->setColumnHidden(ColFolder, !byAccount_->isChecked() && !byYear_->isChecked());

    // Detail for the selected file.
    if (const auto cur = currentRow()) {
        const SessionRow& row = session_->row(*cur);
        const PlannedMove& move = preview_[*cur];
        const StatusView status = describe(row, move);
        QString text = status.text;
        const QString name = relativeName(session_->root(), row.done ? row.path : move.destination);
        if (!name.isEmpty() && move.status != MoveStatus::Collision && move.status != MoveStatus::Error)
            text += QStringLiteral("\n") + name;
        if (date_->hasInvalidText() && !row.done)
            text = date_->problem();
        rowStatus_->setText(text);
        rowStatus_->setStyleSheet(status.color == kRed || date_->hasInvalidText()
                                      ? QStringLiteral("color: #c42b1c;")
                                      : QString());
    }
    updateSummary();
}

void RenameWindow::updateSummary()
{
    std::size_t ready = 0, notFilled = 0, skipped = 0, renamed = 0, problems = 0;
    if (session_) {
        for (std::size_t i = 0; i < preview_.size(); ++i) {
            if (session_->row(i).done) { ++renamed; continue; }
            switch (preview_[i].status) {
            case MoveStatus::Ready: ++ready; break;
            case MoveStatus::Skipped: ++skipped; break;
            case MoveStatus::Incomplete: ++notFilled; break;
            case MoveStatus::Unchanged: break;
            case MoveStatus::Collision:
            case MoveStatus::Error: ++problems; break;
            }
        }
    }

    if (!session_) {
        summary_->setText(tr("Choose a folder to begin."));
    } else if (session_->size() == 0) {
        summary_->setText(tr("There are no PDFs in this folder."));
    } else {
        QString text = tr("%1 ready  ·  %2 not filled in  ·  %3 skipped  ·  %4 renamed")
                           .arg(ready).arg(notFilled).arg(skipped).arg(renamed);
        if (problems) text += tr("  ·  %1 need attention").arg(problems);
        summary_->setText(text);
    }

    applyBtn_->setEnabled(ready > 0);
    undoBtn_->setEnabled(session_ && session_->canUndo());
    refreshBtn_->setEnabled(session_ != nullptr);
}

void RenameWindow::selectRow(int row)
{
    if (row < 0 || row >= table_->rowCount()) {
        loadEditor();
        return;
    }
    table_->setCurrentCell(row, ColFile);
    table_->scrollToItem(table_->item(row, ColFile));
    loadEditor();
}

std::optional<std::size_t> RenameWindow::currentRow() const
{
    const int row = table_->currentRow();
    if (!session_ || row < 0 || row >= static_cast<int>(session_->size())) return std::nullopt;
    return static_cast<std::size_t>(row);
}

// ---------------------------------------------------------------------------
// Editor

void RenameWindow::loadEditor()
{
    loadingEditor_ = true;
    const auto cur = currentRow();

    if (!cur) {
        fileLabel_->setText(session_ ? tr("No file selected") : tr("Choose a folder to begin."));
        account_->setCurrentIndex(0);
        date_->setValue(std::nullopt);
        skip_->setChecked(false);
        rowStatus_->clear();
        pdf_->clear();
        for (QWidget* w : editFields_) w->setEnabled(false);
        for (QWidget* w : std::initializer_list<QWidget*>{skip_, openBtn_, nextBtn_}) w->setEnabled(false);
        fillNumbers(std::nullopt, {});
        loadingEditor_ = false;
        return;
    }

    const SessionRow& row = session_->row(*cur);
    fileLabel_->setText(qpath(row.originalPath.filename()));
    fileLabel_->setToolTip(QDir::toNativeSeparators(qpath(row.path)));
    pdf_->showFile(qpath(row.path));

    const int index = accountIndex(row.accountId);
    account_->setCurrentIndex(index >= 0 ? index : 0);
    kind_->setCurrentIndex(std::max(0, kind_->findData(static_cast<int>(row.document.kind))));
    textStyle_->setCurrentIndex(std::max(0, textStyle_->findData(static_cast<int>(row.document.textStyle))));
    label_->setText(qstr(row.document.label));
    from_->setText(qstr(row.document.sender));
    to_->setText(qstr(row.document.recipient));
    description_->setText(qstr(row.document.description));
    updateKindFields();  // before the date is set, so the date box offers the right modes
    date_->setValue(row.date);
    skip_->setChecked(row.skip);

    const bool editable = !row.done;
    skip_->setEnabled(editable);
    for (QWidget* w : editFields_) w->setEnabled(editable && !row.skip);
    fillNumbers(row.accountId, row.number);
    openBtn_->setEnabled(true);
    nextBtn_->setEnabled(true);

    loadingEditor_ = false;
    updatePreview();
}

void RenameWindow::commitEditor()
{
    if (loadingEditor_) return;
    const auto cur = currentRow();
    if (!cur) return;
    SessionRow& row = session_->row(*cur);
    if (row.done) return;

    const QVariant id = account_->currentData(kAccountIdRole);
    row.accountId = id.isValid() ? std::optional<std::int64_t>(id.toLongLong()) : std::nullopt;
    row.number = row.accountId ? stdstr(number_->currentData(kNumberRole).toString()) : std::string();
    row.document = editorDocument();
    row.date = date_->value();
    row.skip = skip_->isChecked();
    row.failure.clear();

    for (QWidget* w : editFields_) w->setEnabled(!row.skip);
    number_->setEnabled(!row.skip && number_->count() > 1);
    updatePreview();
}

DocumentInfo RenameWindow::editorDocument() const
{
    DocumentInfo d;
    d.kind = static_cast<DocKind>(kind_->currentData().toInt());
    d.textStyle = static_cast<TextStyle>(textStyle_->currentData().toInt());
    d.label = stdstr(label_->text());
    d.sender = stdstr(from_->text());
    d.recipient = stdstr(to_->text());
    d.description = stdstr(description_->text());
    return d;
}

// Shows only the fields the chosen kind uses, with captions to match, and limits
// the date box to the modes that kind accepts.
void RenameWindow::updateKindFields()
{
    const DocumentInfo info = editorDocument();
    const bool statement = info.kind == DocKind::Statement;
    const bool text = info.kind == DocKind::Text;
    const bool hasLabel = text || info.kind == DocKind::Letter || info.kind == DocKind::Other;
    const bool hasFrom = info.kind == DocKind::Email || text || info.kind == DocKind::Letter;
    const bool hasTo = info.kind == DocKind::Email ||
                       (text && info.textStyle != TextStyle::Group);
    const bool hasDescription = !statement;

    form_->setRowVisible(accountBox_, statement);
    form_->setRowVisible(numberBox_, statement);
    form_->setRowVisible(textStyle_, text);
    form_->setRowVisible(label_, hasLabel);
    form_->setRowVisible(from_, hasFrom);
    form_->setRowVisible(to_, hasTo);
    form_->setRowVisible(description_, hasDescription);

    auto caption = [this](QWidget* field, const QString& textValue) {
        if (auto* l = qobject_cast<QLabel*>(form_->labelForField(field))) l->setText(textValue);
    };
    QString fromCaption = tr("From:"), toCaption = tr("To:"), labelCaption = tr("Label:");
    QString labelHint, fromHint = tr("Name"), toHint = tr("Name");
    if (text) {
        labelCaption = tr("Label:");
        labelHint = tr("Texts, WhatsApp, Our Family Wizard...");
        if (info.textStyle == TextStyle::Conversation) {
            fromCaption = tr("Between:");
            toCaption = tr("and:");
        } else if (info.textStyle == TextStyle::Group) {
            fromCaption = tr("With:");
            fromHint = tr("The people, or the group's name");
        }
    } else if (info.kind == DocKind::Letter) {
        labelHint = tr("Ltr");
    } else if (info.kind == DocKind::Other) {
        labelHint = tr("What it is, e.g. Photo, Court Order, Invoice");
    }
    caption(from_, fromCaption);
    caption(to_, toCaption);
    caption(label_, labelCaption);
    label_->setPlaceholderText(labelHint);
    from_->setPlaceholderText(fromHint);
    to_->setPlaceholderText(toHint);

    const DateModes modes = allowedDateModes(info);
    date_->setModes(modes.period, modes.quarter);
}

void RenameWindow::kindChanged()
{
    if (loadingEditor_) return;
    // A kind's usual label is filled in, unless the user has typed their own.
    const DocKind kind = static_cast<DocKind>(kind_->currentData().toInt());
    const QString current = label_->text().trimmed();
    if (current.isEmpty() || current == qstr(std::string(kDefaultTextLabel)) ||
        current == qstr(std::string(kDefaultLetterLabel))) {
        const QSignalBlocker block(label_);
        label_->setText(kind == DocKind::Text     ? qstr(std::string(kDefaultTextLabel))
                        : kind == DocKind::Letter ? qstr(std::string(kDefaultLetterLabel))
                                                  : QString());
    }
    updateKindFields();
    commitEditor();
}

void RenameWindow::goToNext()
{
    const auto cur = currentRow();
    if (!cur) return;
    commitEditor();

    const auto next = session_->nextPending(*cur);
    if (!next) {
        rowStatus_->setText(rowStatus_->text() +
                            tr("\n\nThat's the last file. Press Apply Renames (Ctrl+Enter) when ready."));
        return;
    }

    const bool carried = session_->carryForward(*cur, *next);
    selectRow(static_cast<int>(*next));
    if (carried) date_->focusFirstField();  // most likely thing to adjust
    else account_->setFocus();
}

void RenameWindow::newAccount()
{
    AccountDialog dialog(db_, caseId_, std::nullopt, this);
    if (dialog.exec() != QDialog::Accepted) return;

    reloadAccounts();
    const int index = accountIndex(dialog.savedAccountId());
    if (index >= 0) account_->setCurrentIndex(index);  // commits to the current row
}

void RenameWindow::openPdf()
{
    const auto cur = currentRow();
    if (!cur) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(qpath(session_->row(*cur).path)));
}

// ---------------------------------------------------------------------------
// Apply / undo

PlanOptions RenameWindow::options() const
{
    PlanOptions o;
    o.sort.byAccount = byAccount_->isChecked();
    o.sort.byYear = byYear_->isChecked();
    o.sort.order = order_->currentIndex() == 0 ? FolderOrder::AccountThenYear : FolderOrder::YearThenAccount;
    o.collisions = collisions_->currentIndex() == 0 ? CollisionPolicy::AppendNumber : CollisionPolicy::Block;
    return o;
}

void RenameWindow::apply()
{
    if (!session_) return;
    commitEditor();
    updatePreview();

    std::size_t ready = 0, notFilled = 0, problems = 0;
    for (std::size_t i = 0; i < preview_.size(); ++i) {
        if (session_->row(i).done) continue;
        switch (preview_[i].status) {
        case MoveStatus::Ready: ++ready; break;
        case MoveStatus::Incomplete: ++notFilled; break;
        case MoveStatus::Collision:
        case MoveStatus::Error: ++problems; break;
        default: break;
        }
    }
    if (ready == 0) {
        QMessageBox::information(this, tr("Apply Renames"),
                                 tr("Nothing is ready yet. Choose an account and date for at least one file."));
        return;
    }

    QString question = ready == 1 ? tr("Rename 1 file?") : tr("Rename %1 files?").arg(ready);
    if (notFilled)
        question += tr("\n\n%1 file(s) without an account or date will be left as they are.").arg(notFilled);
    if (problems)
        question += tr("\n\n%1 file(s) with a problem (see Status) will be left as they are.").arg(problems);
    if (QMessageBox::question(this, tr("Apply Renames"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;

    const ExecuteResult result = session_->apply(options());
    if (!result.record.moves.empty()) {
        // The files are already renamed; if this fails only the undo history is lost.
        runGuarded(this, [&] { db_.saveBatch(result.record, caseId_); });
    }

    updatePreview();
    if (!result.failures.empty()) {
        QStringList lines;
        for (std::size_t i = 0; i < result.failures.size() && i < 10; ++i)
            lines << QStringLiteral("• %1: %2").arg(qpath(result.failures[i].path.filename()),
                                                          qstr(result.failures[i].reason));
        if (result.failures.size() > 10) lines << tr("...and %1 more.").arg(result.failures.size() - 10);
        QMessageBox::warning(this, tr("Some files weren't renamed"),
                             tr("%1 file(s) renamed. These couldn't be (if a PDF is open in "
                                "another program, close it and apply again):\n\n%2")
                                 .arg(result.record.moves.size())
                                 .arg(lines.join('\n')));
    }

    // Continue with the first file still to fill in (else the first not renamed).
    for (std::size_t i = 0; i < session_->size(); ++i)
        if (!session_->row(i).done && !session_->row(i).skip) return selectRow(static_cast<int>(i));
    for (std::size_t i = 0; i < session_->size(); ++i)
        if (!session_->row(i).done) return selectRow(static_cast<int>(i));
    loadEditor();
}

void RenameWindow::undoLast()
{
    if (!session_ || !session_->canUndo()) return;
    if (QMessageBox::question(this, tr("Undo Last Apply"),
                              tr("Put the files from the last Apply back to their original names and "
                                 "places?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    const auto undone = session_->undoLast();
    if (!undone) return;
    runGuarded(this, [&] { db_.markBatchUndone(undone->batchId); });

    updatePreview();
    loadEditor();
    if (!undone->result.failures.empty()) {
        QStringList lines;
        for (const auto& f : undone->result.failures)
            lines << QStringLiteral("• %1: %2").arg(qpath(f.path.filename()), qstr(f.reason));
        QMessageBox::warning(this, tr("Undo"),
                             tr("%1 file(s) restored. These couldn't be:\n\n%2")
                                 .arg(undone->result.restored)
                                 .arg(lines.join('\n')));
    }
}

void RenameWindow::reject()
{
    if (session_ && session_->hasUnappliedEntries()) {
        const auto answer = QMessageBox::question(
            this, tr("Close"),
            tr("Some files have an account or date entered but haven't been renamed yet. "
               "Close and lose those entries?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("rename/leftSplitter", leftSplitter_->saveState());
    settings.setValue("rename/mainSplitter", mainSplitter_->saveState());
    QDialog::reject();
}

void RenameWindow::saveSettings() const
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("sort/byAccount", byAccount_->isChecked());
    settings.setValue("sort/byYear", byYear_->isChecked());
    settings.setValue("sort/order", order_->currentIndex());
    settings.setValue("sort/collisions", collisions_->currentIndex());
}
