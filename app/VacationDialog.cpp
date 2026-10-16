#include "VacationDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "QtHelpers.h"
#include "SignaturesDialog.h"
#include "finrenamer/VacationLetters.h"

using namespace finrenamer;
using namespace ui;

namespace {

// The columns shown in the list (when the sheet has them), in this order.
const char* const kListColumns[] = {"Client (Co-Counsel)", "Attorney", "Paralegal", "OPC", "County", "Cause No"};

// Columns that usually hold several lines get a taller box in the form.
int formLines(const std::string& header)
{
    const std::string key = normalizeHeader(header);
    for (const char* tall : {"opcaddress", "opc2address", "eserviceemail", "clientcocounsel", "counsel", "opcfirmname", "immoitio"})
        if (key == tall) return 3;
    return 1;
}

QString oneLine(const std::string& text)
{
    QString s = qstr(text).trimmed();
    s.replace('\n', QStringLiteral(" / "));
    return s;
}

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

QString todayText()
{
    return QLocale::c().toString(QDate::currentDate(), QStringLiteral("MMMM d, yyyy"));
}

}  // namespace

VacationDialog::VacationDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Vacation Letters - %1").arg(appTitle()));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    buildUi();

    QSettings settings(settingsFile(), QSettings::IniFormat);
    template_->setText(QDir::toNativeSeparators(settings.value("vacation/template").toString()));
    const QString book = settings.value("vacation/workbook").toString();
    if (!book.isEmpty() && QFileInfo::exists(book)) {
        workbook_->setText(QDir::toNativeSeparators(book));
        reload();
    } else {
        workbook_->setText(QDir::toNativeSeparators(book));
        updateButtons();
    }
}

VacationDialog::~VacationDialog()
{
    disconnectChildren(this);
}

void VacationDialog::buildUi()
{
    // ---- Files ----
    QPushButton *browseBook, *browseTemplate;
    auto* filesBox = new QGroupBox(tr("Shared files"));
    auto* filesLayout = new QVBoxLayout(filesBox);
    auto* bookRow = new QHBoxLayout;
    bookRow->addWidget(new QLabel(tr("Case list (Excel):")));
    bookRow->addLayout(pathRow(workbook_, "workbook", browseBook, "browseWorkbook"), 1);
    reloadBtn_ = new QPushButton(tr("Reload"));
    reloadBtn_->setObjectName("reload");
    reloadBtn_->setAutoDefault(false);
    reloadBtn_->setToolTip(tr("Read the spreadsheet again to pick up other people's changes"));
    bookRow->addWidget(reloadBtn_);
    auto* templateRow = new QHBoxLayout;
    templateRow->addWidget(new QLabel(tr("Letter template (Word):")));
    templateRow->addLayout(pathRow(template_, "template", browseTemplate, "browseTemplate"), 1);
    auto* signaturesBtn = new QPushButton(tr("Signatures..."));
    signaturesBtn->setObjectName("signatures");
    signaturesBtn->setAutoDefault(false);
    signaturesBtn->setToolTip(tr("The attorneys' signature pictures, added to each letter automatically"));
    templateRow->addWidget(signaturesBtn);
    connect(signaturesBtn, &QPushButton::clicked, this, &VacationDialog::manageSignatures);
    filesLayout->addLayout(bookRow);
    filesLayout->addLayout(templateRow);
    auto* hint = new QLabel(tr("Both are used straight from where they are saved (nothing is copied), "
                               "so everyone sees the latest list and the latest letter wording."));
    hint->setStyleSheet("color: palette(placeholder-text);");
    hint->setWordWrap(true);
    filesLayout->addWidget(hint);

    // ---- Cases ----
    auto* casesBox = new QGroupBox(tr("Cases"));
    auto* casesLayout = new QVBoxLayout(casesBox);
    search_ = new QLineEdit;
    search_->setObjectName("search");
    search_->setPlaceholderText(tr("Search..."));
    search_->setClearButtonEnabled(true);
    attorney_ = new QComboBox;
    attorney_->setObjectName("attorney");
    paralegal_ = new QComboBox;
    paralegal_->setObjectName("paralegal");
    county_ = new QComboBox;
    county_->setObjectName("county");
    for (auto* combo : {attorney_, paralegal_, county_}) combo->setMinimumContentsLength(10);
    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(search_, 2);
    filterRow->addWidget(new QLabel(tr("Attorney:")));
    filterRow->addWidget(attorney_, 1);
    filterRow->addWidget(new QLabel(tr("Paralegal:")));
    filterRow->addWidget(paralegal_, 1);
    filterRow->addWidget(new QLabel(tr("County:")));
    filterRow->addWidget(county_, 1);
    casesLayout->addLayout(filterRow);

    table_ = new QTableWidget(0, 0);
    table_->setObjectName("cases");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSortingEnabled(true);
    casesLayout->addWidget(table_, 1);

    selectShown_ = new QPushButton(tr("Tick All Shown"));
    selectShown_->setObjectName("selectShown");
    selectShown_->setAutoDefault(false);
    clearSelection_ = new QPushButton(tr("Clear Ticks"));
    clearSelection_->setObjectName("clearSelection");
    clearSelection_->setAutoDefault(false);
    count_ = new QLabel;
    count_->setObjectName("selectedCount");
    auto* tickRow = new QHBoxLayout;
    tickRow->addWidget(selectShown_);
    tickRow->addWidget(clearSelection_);
    tickRow->addStretch();
    tickRow->addWidget(count_);
    casesLayout->addLayout(tickRow);

    // ---- Edit ----
    auto* editBox = new QGroupBox(tr("Edit Case"));
    auto* editLayout = new QVBoxLayout(editBox);
    formTitle_ = new QLabel;
    QFont bold = formTitle_->font();
    bold.setBold(true);
    formTitle_->setFont(bold);
    editLayout->addWidget(formTitle_);
    formArea_ = new QScrollArea;
    formArea_->setObjectName("form");
    formArea_->setWidgetResizable(true);
    formArea_->setFrameShape(QFrame::NoFrame);
    formHost_ = new QWidget;
    form_ = new QFormLayout(formHost_);
    form_->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    formArea_->setWidget(formHost_);
    editLayout->addWidget(formArea_, 1);
    newBtn_ = new QPushButton(tr("New Case"));
    newBtn_->setObjectName("newCase");
    newBtn_->setAutoDefault(false);
    revertBtn_ = new QPushButton(tr("Revert"));
    revertBtn_->setObjectName("revert");
    revertBtn_->setAutoDefault(false);
    saveBtn_ = new QPushButton(tr("Save to Spreadsheet"));
    saveBtn_->setObjectName("saveCase");
    saveBtn_->setAutoDefault(false);
    auto* editButtons = new QHBoxLayout;
    editButtons->addWidget(newBtn_);
    editButtons->addStretch();
    editButtons->addWidget(revertBtn_);
    editButtons->addWidget(saveBtn_);
    editLayout->addLayout(editButtons);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(casesBox);
    splitter->addWidget(editBox);
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({620, 520});

    // ---- Footer ----
    status_ = new QLabel;
    status_->setObjectName("status");
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    createBtn_ = new QPushButton(tr("Create Letters..."));
    createBtn_->setObjectName("createLetters");
    createBtn_->setAutoDefault(false);
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setObjectName("close");
    closeBtn->setAutoDefault(false);
    auto* footer = new QHBoxLayout;
    footer->addWidget(status_, 1);
    footer->addWidget(createBtn_);
    footer->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(filesBox);
    layout->addWidget(splitter, 1);
    layout->addLayout(footer);

    connect(browseBook, &QPushButton::clicked, this, &VacationDialog::browseWorkbook);
    connect(browseTemplate, &QPushButton::clicked, this, &VacationDialog::browseTemplate);
    connect(reloadBtn_, &QPushButton::clicked, this, [this] { reload(); });
    connect(workbook_, &QLineEdit::editingFinished, this, [this] {
        if (!workbook_->text().trimmed().isEmpty() && QDir::toNativeSeparators(workbookPath()) != qstr(sheetPathLoaded_))
            reload();
    });
    connect(template_, &QLineEdit::textChanged, this, [this] { updateButtons(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { applyFilters(); });
    for (auto* combo : {attorney_, paralegal_, county_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] { applyFilters(); });
    connect(table_, &QTableWidget::currentCellChanged, this, [this] { rowChosen(); });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (populating_ || !item || item->column() != 0) return;
        const auto row = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
        if (item->checkState() == Qt::Checked) {
            checked_.insert(row);
            if (const auto* r = findRow(row)) seen_[row] = r->values;
        } else {
            checked_.erase(row);
            seen_.erase(row);
        }
        updateCount();
        updateButtons();
    });
    connect(selectShown_, &QPushButton::clicked, this, [this] {
        std::set<std::size_t> rows = checked_;
        for (int r = 0; r < table_->rowCount(); ++r) rows.insert(static_cast<std::size_t>(table_->item(r, 0)->data(Qt::UserRole).toULongLong()));
        setChecked(rows);
    });
    connect(clearSelection_, &QPushButton::clicked, this, [this] { setChecked({}); });
    connect(newBtn_, &QPushButton::clicked, this, [this] {
        if (!haveSheet_ || !resolveUnsaved()) return;
        table_->clearSelection();
        loadForm(std::size_t{0});
    });
    connect(revertBtn_, &QPushButton::clicked, this, [this] {
        if (editing_) loadForm(*editing_);
    });
    connect(saveBtn_, &QPushButton::clicked, this, [this] { saveCase(); });
    connect(createBtn_, &QPushButton::clicked, this, &VacationDialog::chooseOutputAndCreate);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    resize(1180, 720);
}

// ---- paths ----------------------------------------------------------------------------------

QString VacationDialog::workbookPath() const { return workbook_->text().trimmed(); }
QString VacationDialog::templatePath() const { return template_->text().trimmed(); }
QString VacationDialog::statusText() const { return status_->text(); }

void VacationDialog::browseWorkbook()
{
    const QString start = workbookPath().isEmpty() ? QDir::homePath() : workbookPath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Vacation letter case list"), start, tr("Excel files (*.xlsx)"));
    if (!file.isEmpty()) setWorkbook(QDir::toNativeSeparators(file));
}

void VacationDialog::browseTemplate()
{
    const QString start = templatePath().isEmpty() ? QDir::homePath() : templatePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Vacation letter template"), start, tr("Word documents (*.docx)"));
    if (!file.isEmpty()) setTemplate(QDir::toNativeSeparators(file));
}

void VacationDialog::setTemplate(const QString& path)
{
    template_->setText(path);
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("vacation/template", path);
}

void VacationDialog::setWorkbook(const QString& path)
{
    workbook_->setText(path);
    reload();
}

// ---- reading ----------------------------------------------------------------------------------

void VacationDialog::report(const QString& text)
{
    status_->setText(text);
    status_->setStyleSheet(QString());
}

void VacationDialog::reportError(const QString& text)
{
    status_->setText(text);
    status_->setStyleSheet("QLabel { color: #c42b1c; }");
}

bool VacationDialog::guard(const std::function<void()>& work, const QString& what)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        work();
        QApplication::restoreOverrideCursor();
        return true;
    } catch (const SheetConflict& e) {
        QApplication::restoreOverrideCursor();
        reportError(qstr(e.what()));
        QMessageBox::warning(this, tr("Not saved"),
                             qstr(e.what()) + tr("\n\nYour typing is still in the form until you pick another case."));
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        reportError(qstr(e.what()));
        QMessageBox::warning(this, what, qstr(e.what()));
    }
    return false;
}

bool VacationDialog::reload()
{
    const QString path = workbookPath();
    if (path.isEmpty()) {
        updateButtons();
        return false;
    }
    VacationSheet fresh;
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        fresh = loadVacationSheet(toPath(path));
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        reportError(qstr(e.what()));
        updateButtons();
        return false;
    }
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("vacation/workbook", path);

    const bool headersChanged = !haveSheet_ || fresh.headers != sheet_.headers;
    sheet_ = std::move(fresh);
    haveSheet_ = true;
    sheetPathLoaded_ = stdstr(QDir::toNativeSeparators(path));

    // Ticked cases that someone changed in the meantime are unticked, never silently kept.
    std::size_t dropped = 0;
    for (auto it = checked_.begin(); it != checked_.end();) {
        const auto* row = findRow(*it);
        if (!row || row->values != seen_[*it]) {
            seen_.erase(*it);
            it = checked_.erase(it);
            ++dropped;
        } else {
            ++it;
        }
    }

    populateFilters();
    populateTable();
    if (headersChanged) rebuildForm();
    if (!dirty_) {
        if (editing_ && (*editing_ == 0 || findRow(*editing_))) loadForm(*editing_);
        else loadForm(std::nullopt);  // nothing chosen, or the case is no longer in the sheet
    }
    QString msg = tr("%n case(s) in \"%1\".", nullptr, static_cast<int>(sheet_.rows.size())).arg(qstr(sheet_.sheetName));
    if (dropped > 0)
        msg += tr("  %n ticked case(s) were changed in the spreadsheet and were unticked - please check them again.", nullptr,
                  static_cast<int>(dropped));
    report(msg);
    updateButtons();
    return true;
}

const VacationRow* VacationDialog::findRow(std::size_t sheetRow) const
{
    for (const auto& r : sheet_.rows)
        if (r.row == sheetRow) return &r;
    return nullptr;
}

// ---- the list ----------------------------------------------------------------------------------

void VacationDialog::populateFilters()
{
    auto fill = [&](QComboBox* combo, const char* header) {
        const QString keep = combo->currentText();
        QSignalBlocker block(combo);
        combo->clear();
        combo->addItem(tr("(all)"), QString());
        const int col = sheet_.column(header);
        if (col >= 0) {
            QStringList values;
            for (const auto& r : sheet_.rows) {
                const QString v = oneLine(r.values[static_cast<std::size_t>(col)]);
                if (!v.isEmpty() && !values.contains(v, Qt::CaseInsensitive)) values << v;
            }
            values.sort(Qt::CaseInsensitive);
            for (const auto& v : values) combo->addItem(v, v);
        }
        combo->setEnabled(col >= 0);
        const int i = combo->findText(keep);
        combo->setCurrentIndex(i < 0 ? 0 : i);
    };
    fill(attorney_, "Attorney");
    fill(paralegal_, "Paralegal");
    fill(county_, "County");
}

bool VacationDialog::rowMatches(const VacationRow& row) const
{
    auto sameAs = [&](QComboBox* combo, const char* header) {
        const QString want = combo->currentData().toString();
        if (want.isEmpty()) return true;
        const int col = sheet_.column(header);
        return col >= 0 && oneLine(row.values[static_cast<std::size_t>(col)]).compare(want, Qt::CaseInsensitive) == 0;
    };
    if (!sameAs(attorney_, "Attorney") || !sameAs(paralegal_, "Paralegal") || !sameAs(county_, "County")) return false;
    const QString text = search_->text().trimmed();
    if (text.isEmpty()) return true;
    for (const auto& value : row.values)
        if (qstr(value).contains(text, Qt::CaseInsensitive)) return true;
    return false;
}

void VacationDialog::populateTable()
{
    populating_ = true;
    std::vector<int> cols;
    QStringList labels{QString()};
    for (const char* name : kListColumns) {
        const int c = sheet_.column(name);
        if (c >= 0) {
            cols.push_back(c);
            labels << qstr(sheet_.headers[static_cast<std::size_t>(c)]);
        }
    }
    const std::optional<std::size_t> keep = editing_;

    table_->setSortingEnabled(false);
    table_->clearContents();
    table_->setColumnCount(labels.size());
    table_->setHorizontalHeaderLabels(labels);
    int n = 0;
    for (const auto& r : sheet_.rows)
        if (rowMatches(r)) ++n;
    table_->setRowCount(n);
    int i = 0;
    for (const auto& r : sheet_.rows) {
        if (!rowMatches(r)) continue;
        auto* check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        check->setCheckState(checked_.count(r.row) ? Qt::Checked : Qt::Unchecked);
        check->setData(Qt::UserRole, static_cast<qulonglong>(r.row));
        table_->setItem(i, 0, check);
        for (std::size_t k = 0; k < cols.size(); ++k) {
            auto* item = new QTableWidgetItem(oneLine(r.values[static_cast<std::size_t>(cols[k])]));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table_->setItem(i, static_cast<int>(k) + 1, item);
        }
        ++i;
    }
    table_->resizeColumnsToContents();
    for (int c = 1; c < table_->columnCount(); ++c) table_->setColumnWidth(c, std::min(table_->columnWidth(c), 220));
    table_->setSortingEnabled(true);
    populating_ = false;

    if (keep && *keep != 0) {
        const int r = tableRowFor(*keep);
        if (r >= 0) {
            populating_ = true;
            table_->selectRow(r);
            populating_ = false;
        }
    }
    updateCount();
}

int VacationDialog::tableRowFor(std::size_t sheetRow) const
{
    for (int r = 0; r < table_->rowCount(); ++r)
        if (static_cast<std::size_t>(table_->item(r, 0)->data(Qt::UserRole).toULongLong()) == sheetRow) return r;
    return -1;
}

void VacationDialog::applyFilters()
{
    if (!haveSheet_) return;
    populateTable();
}

void VacationDialog::updateCount()
{
    count_->setText(tr("%1 ticked, %2 shown of %3").arg(checked_.size()).arg(table_->rowCount()).arg(sheet_.rows.size()));
}

std::vector<std::size_t> VacationDialog::checkedRows() const
{
    return std::vector<std::size_t>(checked_.begin(), checked_.end());
}

void VacationDialog::setChecked(const std::set<std::size_t>& rows)
{
    checked_.clear();
    seen_.clear();
    for (std::size_t r : rows) {
        if (const auto* row = findRow(r)) {
            checked_.insert(r);
            seen_[r] = row->values;
        }
    }
    populating_ = true;
    for (int r = 0; r < table_->rowCount(); ++r) {
        auto* item = table_->item(r, 0);
        item->setCheckState(checked_.count(static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong())) ? Qt::Checked : Qt::Unchecked);
    }
    populating_ = false;
    updateCount();
    updateButtons();
}

void VacationDialog::updateButtons()
{
    const bool loaded = haveSheet_;
    reloadBtn_->setEnabled(!workbookPath().isEmpty());
    selectShown_->setEnabled(loaded && table_->rowCount() > 0);
    clearSelection_->setEnabled(!checked_.empty());
    newBtn_->setEnabled(loaded);
    saveBtn_->setEnabled(loaded && editing_ && dirty_);
    revertBtn_->setEnabled(loaded && editing_ && dirty_);
    createBtn_->setEnabled(loaded && !checked_.empty() && !templatePath().isEmpty());
    createBtn_->setToolTip(templatePath().isEmpty() ? tr("Choose the letter template first") : QString());
}

// ---- the edit form ----------------------------------------------------------------------------------

void VacationDialog::rebuildForm()
{
    while (form_->rowCount() > 0) form_->removeRow(0);
    fields_.clear();
    for (const auto& header : sheet_.headers) {
        auto* edit = new QPlainTextEdit;
        edit->setObjectName("field_" + qstr(normalizeHeader(header)));
        edit->setTabChangesFocus(true);
        edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        const int lines = formLines(header);
        edit->setFixedHeight(edit->fontMetrics().lineSpacing() * lines + 14);
        connect(edit, &QPlainTextEdit::textChanged, this, [this] { markDirty(); });
        fields_.push_back(edit);
        form_->addRow(qstr(header) + ":", edit);
    }
    editing_.reset();
    setDirty(false);
}

void VacationDialog::loadForm(std::optional<std::size_t> sheetRow)
{
    loadingForm_ = true;
    const VacationRow* row = sheetRow && *sheetRow != 0 ? findRow(*sheetRow) : nullptr;
    formLoaded_.assign(sheet_.headers.size(), std::string());
    if (row) formLoaded_ = row->values;
    for (std::size_t i = 0; i < fields_.size(); ++i) fields_[i]->setPlainText(qstr(formLoaded_[i]));
    loadingForm_ = false;

    if (!sheetRow || (*sheetRow != 0 && !row)) {
        editing_.reset();
        formTitle_->setText(tr("Choose a case from the list, or click New Case."));
    } else if (*sheetRow == 0) {
        editing_ = std::size_t{0};
        formTitle_->setText(tr("New case"));
    } else {
        editing_ = *sheetRow;
        const int c = sheet_.column("Client (Co-Counsel)");
        const QString name = c >= 0 ? oneLine(row->values[static_cast<std::size_t>(c)]) : QString();
        formTitle_->setText(tr("Row %1: %2").arg(*sheetRow).arg(name));
    }
    for (auto* f : fields_) f->setEnabled(editing_.has_value());
    setDirty(false);
}

std::vector<std::string> VacationDialog::formValues() const
{
    std::vector<std::string> v;
    for (auto* f : fields_) v.push_back(stdstr(f->toPlainText()));
    return v;
}

void VacationDialog::markDirty()
{
    if (loadingForm_) return;
    setDirty(true);
}

void VacationDialog::setDirty(bool dirty)
{
    dirty_ = dirty;
    updateButtons();
}

bool VacationDialog::resolveUnsaved()
{
    if (!dirty_ || !editing_) return true;
    QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                    tr("This case has changes that are not saved to the spreadsheet."), QMessageBox::NoButton, this);
    auto* save = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == cancel) return false;
    if (box.clickedButton() == save) return saveCase();
    setDirty(false);
    return true;
}

void VacationDialog::rowChosen()
{
    if (populating_) return;
    const int r = table_->currentRow();
    if (r < 0 || !table_->item(r, 0)) return;
    const auto row = static_cast<std::size_t>(table_->item(r, 0)->data(Qt::UserRole).toULongLong());
    if (editing_ && *editing_ == row) return;
    if (!resolveUnsaved()) {
        // Stay on the case being edited.
        populating_ = true;
        const int back = editing_ ? tableRowFor(*editing_) : -1;
        if (back >= 0) table_->selectRow(back);
        else table_->clearSelection();
        populating_ = false;
        return;
    }
    loadForm(row);
}

bool VacationDialog::saveCase()
{
    if (!haveSheet_ || !editing_) return false;
    const std::size_t was = *editing_;
    const auto values = formValues();
    std::size_t written = 0;
    const bool ok = guard([&] { written = saveVacationRow(toPath(workbookPath()), was, formLoaded_, values); },
                          tr("Couldn't save"));
    if (!ok) return false;

    // Re-read, so the list shows exactly what is in the workbook now.
    setDirty(false);
    editing_ = written;
    const bool wasTicked = checked_.count(written) > 0;
    if (!reload()) return false;
    if (wasTicked) {
        if (const auto* r = findRow(written)) seen_[written] = r->values;
    }
    loadForm(written);
    const int tr_ = tableRowFor(written);
    if (tr_ >= 0) {
        populating_ = true;
        table_->selectRow(tr_);
        populating_ = false;
    }
    report(was == 0 ? tr("Added the new case on row %1 of the spreadsheet.").arg(written)
                    : tr("Saved row %1 to the spreadsheet.").arg(written));
    return true;
}

// ---- letters --------------------------------------------------------------------------------------------

void VacationDialog::chooseOutputAndCreate()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    QString folder = settings.value("vacation/saveFolder").toString();
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(folder).filePath(
        tr("Vacation Letters %1.docx").arg(QDate::currentDate().toString("yyyy.MM.dd")));
    const QString file = QFileDialog::getSaveFileName(this, tr("Save Vacation Letters"), suggested, tr("Word documents (*.docx)"));
    if (file.isEmpty()) return;
    settings.setValue("vacation/saveFolder", QFileInfo(file).absolutePath());
    if (!createLetters(file)) return;

    QMessageBox box(QMessageBox::Information, tr("Letters created"), status_->text(), QMessageBox::NoButton, this);
    auto* open = box.addButton(tr("Open Document"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == open) QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

bool VacationDialog::createLetters(const QString& outputPath)
{
    if (!haveSheet_ || checked_.empty()) return false;
    if (templatePath().isEmpty() || !QFileInfo::exists(templatePath())) {
        reportError(tr("The letter template was not found. Choose it above."));
        return false;
    }
    if (!resolveUnsaved()) return false;

    // Use what is in the workbook right now, and make sure the cases ticked are still the ones that were ticked.
    const auto beforeTicks = checked_.size();
    if (!reload()) return false;
    if (checked_.size() != beforeTicks) {
        QMessageBox::warning(this, tr("The list changed"),
                             tr("Some of the cases you ticked were changed in the spreadsheet while you were working, so they "
                                "were unticked. No letters were made. Check the ticks and click Create Letters again."));
        return false;
    }

    std::vector<LetterValues> letters;
    for (const auto& row : sheet_.rows)
        if (checked_.count(row.row)) letters.push_back(letterValues(sheet_, row));

    // Each letter gets the signature of whoever signs it (see Signatures...).
    const auto signatureList = loadSignatures();
    std::vector<std::filesystem::path> signatures;
    std::map<QString, int> unsigned_;  // signer -> letters without a usable picture
    for (const LetterValues& letter : letters) {
        const std::string signer = signerName(letter);
        std::filesystem::path file = signatureFor(signatureList, signer);
        std::error_code ec;
        if (!file.empty() && !std::filesystem::exists(file, ec)) file.clear();
        if (file.empty() && !signatureList.empty())
            ++unsigned_[signer.empty() ? tr("(no attorney named)") : qstr(signer)];
        signatures.push_back(std::move(file));
    }

    LetterResult result;
    const bool ok = guard(
        [&] {
            result = writeCombinedLetters(toPath(templatePath()), toPath(outputPath), letters, stdstr(todayText()),
                                          signatures);
        },
        tr("Couldn't make the letters"));
    if (!ok) return false;

    QString msg = tr("Created %n letter(s) in \"%1\".", nullptr, static_cast<int>(result.letters))
                      .arg(QDir::toNativeSeparators(outputPath));
    if (!result.missingColumns.empty()) {
        QStringList names;
        for (const auto& n : result.missingColumns) names << qstr(n);
        msg += tr("\nThe template has fields with no matching column in the spreadsheet, so they were left blank: %1.")
                   .arg(names.join(", "));
    }
    if (!signatureList.empty()) {
        if (!result.templateHasSignatureField) {
            msg += tr("\nThe template has no \"Signature\" merge field yet, so no signatures were added.");
        } else {
            msg += tr("\n%n signature(s) added.", nullptr, static_cast<int>(result.signaturesAdded));
            if (!unsigned_.empty()) {
                QStringList names;
                for (const auto& [name, n] : unsigned_) names << QStringLiteral("%1 (%2)").arg(name).arg(n);
                msg += tr("\nNo signature picture found for: %1. Those letters were left unsigned.")
                           .arg(names.join(QStringLiteral(", ")));
            }
        }
    }
    report(msg);
    return true;
}

void VacationDialog::manageSignatures()
{
    // Offer the names that appear in the spreadsheet, spelled the way the letters use them.
    QStringList names;
    if (haveSheet_) {
        for (const auto& row : sheet_.rows) {
            const LetterValues v = letterValues(sheet_, row);
            for (const char* key : {"partneroncase", "attorney"}) {
                const auto it = v.find(key);
                const QString name = it == v.end() ? QString() : oneLine(it->second);
                if (!name.isEmpty() && !names.contains(name, Qt::CaseInsensitive)) names << name;
            }
        }
        names.sort(Qt::CaseInsensitive);
    }
    SignaturesDialog dialog(names, this);
    dialog.exec();
}
