#include "AttorneyDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "DirectoryEditors.h"
#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

namespace {

QPushButton* button(const QString& text, const char* objectName)
{
    auto* b = new QPushButton(text);
    b->setObjectName(objectName);
    b->setAutoDefault(false);
    return b;
}

QTableWidget* makeTable(const QString& objectName, const QStringList& headers)
{
    auto* t = new QTableWidget(0, static_cast<int>(headers.size()));
    t->setObjectName(objectName);
    t->setHorizontalHeaderLabels(headers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    t->setAlternatingRowColors(true);
    return t;
}

QTableWidgetItem* cell(const std::string& text, std::int64_t id = 0)
{
    auto* item = new QTableWidgetItem(qstr(text));
    if (id) item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(id));
    return item;
}

}  // namespace

// ---- The window --------------------------------------------------------------------------------------

AttorneyDialog::AttorneyDialog(Database& db, QWidget* parent) : QDialog(parent), db_(db)
{
    setWindowTitle(tr("Attorney Directory - %1").arg(appTitle()));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    // ---- Search ----
    search_ = new QLineEdit;
    search_->setObjectName("search");
    search_->setPlaceholderText(tr("Search firms, attorneys, bar numbers, emails, phone numbers..."));
    search_->setClearButtonEnabled(true);
    show_ = new QComboBox;
    show_->setObjectName("show");
    show_->addItem(tr("All firms"));
    show_->addItem(tr("Our firm"));
    show_->addItem(tr("Opposing counsel"));
    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(tr("Search:")));
    searchRow->addWidget(search_, 1);
    searchRow->addWidget(new QLabel(tr("Show:")));
    searchRow->addWidget(show_);

    // ---- Firms ----
    auto* firmsBox = new QGroupBox(tr("Firms"));
    auto* firmsLayout = new QVBoxLayout(firmsBox);
    firmTable_ = makeTable("firms", {tr("Firm"), tr("Type"), tr("Phone"), tr("Attorneys")});
    firmTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    firmTable_->horizontalHeader()->setStretchLastSection(false);
    firmsLayout->addWidget(firmTable_, 1);
    auto* newFirmBtn = button(tr("New Firm..."), "newFirm");
    editFirmBtn_ = button(tr("Edit Firm..."), "editFirm");
    deleteFirmBtn_ = button(tr("Delete Firm"), "deleteFirm");
    moveAllBtn_ = button(tr("Move All Attorneys..."), "moveAllAttorneys");
    moveAllBtn_->setToolTip(tr("Move every attorney of this firm to another firm (for example after a merger)"));
    auto* firmButtons = new QHBoxLayout;
    firmButtons->addWidget(newFirmBtn);
    firmButtons->addWidget(editFirmBtn_);
    firmButtons->addWidget(deleteFirmBtn_);
    firmButtons->addStretch();
    firmButtons->addWidget(moveAllBtn_);
    firmsLayout->addLayout(firmButtons);

    // ---- The chosen firm and its attorneys ----
    auto* detailBox = new QGroupBox(tr("Firm details and attorneys"));
    auto* detailLayout = new QVBoxLayout(detailBox);
    firmTitle_ = new QLabel;
    firmTitle_->setObjectName("firmTitle");
    QFont bold = firmTitle_->font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 1.2);
    firmTitle_->setFont(bold);
    firmDetails_ = new QLabel;
    firmDetails_->setObjectName("firmDetails");
    firmDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    firmDetails_->setWordWrap(true);
    firmDetails_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailLayout->addWidget(firmTitle_);
    detailLayout->addWidget(firmDetails_);
    attorneyTable_ = makeTable("attorneys", {tr("Name"), tr("Bar Number"), tr("Contact Email"), tr("Service Email")});
    attorneyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    attorneyTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    attorneyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    attorneyTable_->horizontalHeader()->setStretchLastSection(false);
    detailLayout->addWidget(attorneyTable_, 1);
    newAttorneyBtn_ = button(tr("New Attorney..."), "newAttorney");
    editAttorneyBtn_ = button(tr("Edit Attorney..."), "editAttorney");
    moveAttorneyBtn_ = button(tr("Move to Another Firm..."), "moveAttorney");
    deleteAttorneyBtn_ = button(tr("Delete Attorney"), "deleteAttorney");
    auto* attorneyButtons = new QHBoxLayout;
    attorneyButtons->addWidget(newAttorneyBtn_);
    attorneyButtons->addWidget(editAttorneyBtn_);
    attorneyButtons->addWidget(deleteAttorneyBtn_);
    attorneyButtons->addStretch();
    attorneyButtons->addWidget(moveAttorneyBtn_);
    detailLayout->addLayout(attorneyButtons);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(firmsBox);
    splitter->addWidget(detailBox);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({480, 520});

    // ---- Footer ----
    summary_ = new QLabel;
    summary_->setObjectName("summary");
    auto* closeBtn = button(tr("Close"), "close");
    auto* footer = new QHBoxLayout;
    footer->addWidget(summary_, 1);
    footer->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(searchRow);
    layout->addWidget(splitter, 1);
    layout->addLayout(footer);

    connect(search_, &QLineEdit::textChanged, this, [this] { populateFirms(); });
    connect(show_, &QComboBox::currentIndexChanged, this, [this] { populateFirms(); });
    connect(firmTable_, &QTableWidget::currentCellChanged, this, [this] {
        if (populating_) return;
        showFirmDetails();
        populateAttorneys();
    });
    connect(firmTable_, &QTableWidget::cellDoubleClicked, this, [this] { editFirm(); });
    connect(attorneyTable_, &QTableWidget::currentCellChanged, this, [this] { updateButtons(); });
    connect(attorneyTable_, &QTableWidget::cellDoubleClicked, this, [this] { editAttorney(); });
    connect(newFirmBtn, &QPushButton::clicked, this, &AttorneyDialog::newFirm);
    connect(editFirmBtn_, &QPushButton::clicked, this, &AttorneyDialog::editFirm);
    connect(deleteFirmBtn_, &QPushButton::clicked, this, &AttorneyDialog::deleteFirm);
    connect(moveAllBtn_, &QPushButton::clicked, this, &AttorneyDialog::moveAllAttorneys);
    connect(newAttorneyBtn_, &QPushButton::clicked, this, &AttorneyDialog::newAttorney);
    connect(editAttorneyBtn_, &QPushButton::clicked, this, &AttorneyDialog::editAttorney);
    connect(moveAttorneyBtn_, &QPushButton::clicked, this, &AttorneyDialog::moveAttorney);
    connect(deleteAttorneyBtn_, &QPushButton::clicked, this, &AttorneyDialog::deleteAttorney);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    resize(1060, 620);
    refresh();
}

AttorneyDialog::~AttorneyDialog()
{
    disconnectChildren(this);
}

// ---- loading and showing --------------------------------------------------------------------------

void AttorneyDialog::refresh()
{
    runGuarded(this, [&] {
        firms_ = db_.listFirms();
        attorneys_ = db_.listAllAttorneys();
    });
    populateFirms();
}

const Firm* AttorneyDialog::findFirm(std::int64_t id) const
{
    for (const auto& f : firms_)
        if (f.id == id) return &f;
    return nullptr;
}

static bool containsText(const std::string& field, const QString& needle)
{
    return qstr(field).contains(needle, Qt::CaseInsensitive);
}

bool AttorneyDialog::firmMatches(const Firm& f) const
{
    const QString s = search_->text().trimmed();
    if (s.isEmpty()) return true;
    return containsText(f.name, s) || containsText(f.phone, s) || containsText(f.fax, s) || containsText(f.address, s);
}

bool AttorneyDialog::attorneyMatches(const Attorney& a) const
{
    const QString s = search_->text().trimmed();
    if (s.isEmpty()) return true;
    return containsText(a.name, s) || containsText(a.barNumber, s) || containsText(a.email, s) ||
           containsText(a.serviceEmail, s);
}

std::optional<std::int64_t> AttorneyDialog::currentFirmId() const
{
    const int r = firmTable_->currentRow();
    if (r < 0 || !firmTable_->item(r, 0)) return std::nullopt;
    return firmTable_->item(r, 0)->data(Qt::UserRole).toLongLong();
}

std::optional<std::int64_t> AttorneyDialog::currentAttorneyId() const
{
    const int r = attorneyTable_->currentRow();
    if (r < 0 || !attorneyTable_->item(r, 0)) return std::nullopt;
    return attorneyTable_->item(r, 0)->data(Qt::UserRole).toLongLong();
}

void AttorneyDialog::populateFirms()
{
    const auto keep = currentFirmId();
    populating_ = true;
    firmTable_->setRowCount(0);
    const int filter = show_->currentIndex();  // 0 all, 1 ours, 2 opposing
    int keepRow = -1;
    for (const auto& f : firms_) {
        if (filter == 1 && !f.ours) continue;
        if (filter == 2 && f.ours) continue;
        int count = 0;
        bool attorneyHit = false;
        for (const auto& a : attorneys_) {
            if (a.firmId != f.id) continue;
            ++count;
            if (attorneyMatches(a)) attorneyHit = true;
        }
        if (!firmMatches(f) && !attorneyHit) continue;
        const int row = firmTable_->rowCount();
        firmTable_->insertRow(row);
        firmTable_->setItem(row, 0, cell(f.name, f.id));
        firmTable_->setItem(row, 1, new QTableWidgetItem(typeText(f.ours)));
        firmTable_->setItem(row, 2, cell(f.phone));
        auto* n = new QTableWidgetItem(QString::number(count));
        n->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        firmTable_->setItem(row, 3, n);
        if (keep && *keep == f.id) keepRow = row;
    }
    firmTable_->resizeColumnToContents(1);
    firmTable_->resizeColumnToContents(3);
    if (firmTable_->rowCount() > 0) firmTable_->setCurrentCell(keepRow >= 0 ? keepRow : 0, 0);
    populating_ = false;

    showFirmDetails();
    populateAttorneys();

    const auto ours = std::count_if(firms_.begin(), firms_.end(), [](const Firm& f) { return f.ours; });
    summary_->setText(tr("%1 firm(s) (%2 ours, %3 opposing counsel), %4 attorney(s).")
                          .arg(firms_.size())
                          .arg(ours)
                          .arg(static_cast<qsizetype>(firms_.size()) - ours)
                          .arg(attorneys_.size()));
}

void AttorneyDialog::selectFirm(std::int64_t firmId)
{
    for (int r = 0; r < firmTable_->rowCount(); ++r) {
        if (firmTable_->item(r, 0)->data(Qt::UserRole).toLongLong() == firmId) {
            firmTable_->setCurrentCell(r, 0);
            return;
        }
    }
}

void AttorneyDialog::showFirmDetails()
{
    const auto id = currentFirmId();
    const Firm* f = id ? findFirm(*id) : nullptr;
    if (!f) {
        firmTitle_->setText(firms_.empty() ? tr("No firms yet. Click New Firm... to add one.") : tr("No firm chosen."));
        firmDetails_->clear();
        return;
    }
    firmTitle_->setText(qstr(f->name));
    QStringList lines;
    lines << typeText(f->ours);
    if (!f->phone.empty()) lines << tr("Phone: %1").arg(qstr(f->phone));
    if (!f->fax.empty()) lines << tr("Fax: %1").arg(qstr(f->fax));
    if (!f->address.empty()) lines << qstr(f->address);
    firmDetails_->setText(lines.join('\n'));
}

void AttorneyDialog::populateAttorneys()
{
    const auto keep = currentAttorneyId();
    attorneyTable_->setRowCount(0);
    const auto firmId = currentFirmId();
    if (firmId) {
        // While searching, a firm that matched by its own details shows all its attorneys;
        // otherwise only the attorneys that matched.
        const Firm* f = findFirm(*firmId);
        const bool all = search_->text().trimmed().isEmpty() || (f && firmMatches(*f));
        int keepRow = -1;
        for (const auto& a : attorneys_) {
            if (a.firmId != *firmId || (!all && !attorneyMatches(a))) continue;
            const int row = attorneyTable_->rowCount();
            attorneyTable_->insertRow(row);
            attorneyTable_->setItem(row, 0, cell(a.name, a.id));
            attorneyTable_->setItem(row, 1, cell(a.barNumber));
            attorneyTable_->setItem(row, 2, cell(a.email));
            attorneyTable_->setItem(row, 3, cell(a.serviceEmail));
            if (keep && *keep == a.id) keepRow = row;
        }
        if (attorneyTable_->rowCount() > 0) attorneyTable_->setCurrentCell(keepRow >= 0 ? keepRow : 0, 0);
    }
    updateButtons();
}

void AttorneyDialog::updateButtons()
{
    const bool firm = currentFirmId().has_value();
    const bool attorney = currentAttorneyId().has_value();
    editFirmBtn_->setEnabled(firm);
    deleteFirmBtn_->setEnabled(firm);
    moveAllBtn_->setEnabled(firm && firms_.size() > 1 && attorneyTable_->rowCount() > 0);
    newAttorneyBtn_->setEnabled(firm);
    editAttorneyBtn_->setEnabled(attorney);
    deleteAttorneyBtn_->setEnabled(attorney);
    moveAttorneyBtn_->setEnabled(attorney && firms_.size() > 1);
}

// ---- firms ----------------------------------------------------------------------------------------

void AttorneyDialog::newFirm()
{
    // Opposing counsel unless only our firm is shown.
    if (const auto f = createFirmInteractive(db_, this, show_->currentIndex() == 1)) {
        search_->clear();
        refresh();
        selectFirm(f->id);
    }
}

void AttorneyDialog::editFirm()
{
    const auto id = currentFirmId();
    const Firm* f = id ? findFirm(*id) : nullptr;
    if (!f) return;
    if (editFirmInteractive(db_, this, *f)) {
        refresh();
        selectFirm(*id);
    }
}

void AttorneyDialog::deleteFirm()
{
    const auto id = currentFirmId();
    const Firm* f = id ? findFirm(*id) : nullptr;
    if (!f) return;
    const auto count = std::count_if(attorneys_.begin(), attorneys_.end(), [&](const Attorney& a) { return a.firmId == *id; });
    if (count > 0) {
        QMessageBox::information(this, tr("Delete firm"),
                                 tr("%1 still has %n attorney(s). Move them to another firm or delete them first.", nullptr,
                                    static_cast<int>(count))
                                     .arg(qstr(f->name)));
        return;
    }
    if (QMessageBox::question(this, tr("Delete firm"), tr("Delete %1 from the directory?").arg(qstr(f->name)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (runGuarded(this, [&] { db_.deleteFirm(*id); })) refresh();
}

std::optional<std::int64_t> AttorneyDialog::chooseOtherFirm(std::int64_t exceptFirm, const QString& title,
                                                           const QString& prompt)
{
    QStringList names;
    std::vector<std::int64_t> ids;
    for (const auto& f : firms_) {
        if (f.id == exceptFirm) continue;
        names << qstr(f.name);
        ids.push_back(f.id);
    }
    if (ids.empty()) {
        QMessageBox::information(this, title, tr("There is no other firm yet. Add the new firm first with New Firm..."));
        return std::nullopt;
    }
    bool ok = false;
    const QString picked = QInputDialog::getItem(this, title, prompt, names, 0, false, &ok);
    if (!ok) return std::nullopt;
    const auto at = names.indexOf(picked);
    if (at < 0) return std::nullopt;
    return ids[static_cast<std::size_t>(at)];
}

void AttorneyDialog::moveAllAttorneys()
{
    const auto id = currentFirmId();
    const Firm* f = id ? findFirm(*id) : nullptr;
    if (!f) return;
    const QString from = qstr(f->name);
    const auto to = chooseOtherFirm(*id, tr("Move all attorneys"), tr("Move every attorney of %1 to:").arg(from));
    if (!to) return;
    std::size_t moved = 0;
    if (!runGuarded(this, [&] { moved = db_.moveAllAttorneys(*id, *to); })) return;
    search_->clear();
    refresh();
    selectFirm(*to);
    summary_->setText(tr("Moved %n attorney(s) from %1.", nullptr, static_cast<int>(moved)).arg(from) + "  " + summary_->text());
}

// ---- attorneys ------------------------------------------------------------------------------------

void AttorneyDialog::newAttorney()
{
    const auto firmId = currentFirmId();
    if (!firmId) return;
    if (const auto a = createAttorneyInteractive(db_, this, *firmId)) {
        refresh();
        selectFirm(a->firmId);
    }
}

void AttorneyDialog::editAttorney()
{
    const auto id = currentAttorneyId();
    if (!id) return;
    auto it = std::find_if(attorneys_.begin(), attorneys_.end(), [&](const Attorney& a) { return a.id == *id; });
    if (it == attorneys_.end()) return;
    if (const auto a = editAttorneyInteractive(db_, this, *it)) {
        refresh();
        selectFirm(a->firmId);  // follow the attorney if they moved
    }
}

void AttorneyDialog::moveAttorney()
{
    const auto id = currentAttorneyId();
    const auto firmId = currentFirmId();
    if (!id || !firmId) return;
    auto it = std::find_if(attorneys_.begin(), attorneys_.end(), [&](const Attorney& a) { return a.id == *id; });
    if (it == attorneys_.end()) return;
    const auto to = chooseOtherFirm(*firmId, tr("Move attorney"), tr("Move %1 to:").arg(qstr(it->name)));
    if (!to) return;
    if (!runGuarded(this, [&] { db_.moveAttorney(*id, *to); })) return;
    search_->clear();
    refresh();
    selectFirm(*to);
}

void AttorneyDialog::deleteAttorney()
{
    const auto id = currentAttorneyId();
    if (!id) return;
    auto it = std::find_if(attorneys_.begin(), attorneys_.end(), [&](const Attorney& a) { return a.id == *id; });
    if (it == attorneys_.end()) return;
    std::size_t onCases = 0;
    runGuarded(this, [&] { onCases = db_.caseCountForAttorney(*id); });
    const QString cases = onCases == 0 ? QString()
                                       : tr("\n\nThey are on %n case(s); deleting takes them off.", nullptr,
                                            static_cast<int>(onCases));
    if (QMessageBox::question(this, tr("Delete attorney"),
                              tr("Delete %1 from the directory?\n\nIf they changed firms, use Move to Another Firm... "
                                 "instead (they stay on their cases).")
                                      .arg(qstr(it->name)) +
                                  cases,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (runGuarded(this, [&] { db_.deleteAttorney(*id); })) refresh();
}
