#include "OpposingCounselDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
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

QLabel* hint(const QString& text)
{
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet("color: palette(placeholder-text);");
    return l;
}

}  // namespace

OpposingCounselDialog::OpposingCounselDialog(Database& db, const QString& caseTitle,
                                             const std::vector<OpposingFirmChoice>& initial,
                                             const std::vector<std::string>& unmatched, QWidget* parent)
    : QDialog(parent), db_(db)
{
    setWindowTitle(tr("Opposing Counsel"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    auto* title = new QLabel(caseTitle);
    title->setObjectName("caseTitle");
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(title);
    if (!unmatched.empty()) {
        QStringList items;
        for (const auto& u : unmatched) items << "  " + qstr(u);
        auto* warn = new QLabel(tr("This case already has opposing counsel text that isn't in the Attorney Directory:\n%1\n"
                                   "Clicking Use These replaces it. Add the firm or attorney below first if you want to keep "
                                   "them, or Cancel to leave the case as it is.")
                                    .arg(items.join('\n')));
        warn->setObjectName("unmatched");
        warn->setWordWrap(true);
        warn->setTextInteractionFlags(Qt::TextSelectableByMouse);
        warn->setStyleSheet("QLabel { color: #9a5b00; }");
        layout->addWidget(warn);
    }

    // ---- Left: the opposing firms on this case ----
    auto* entriesBox = new QGroupBox(tr("Opposing firms on this case"));
    auto* entriesLayout = new QVBoxLayout(entriesBox);
    entryList_ = new QListWidget;
    entryList_->setObjectName("entries");
    entriesLayout->addWidget(entryList_, 1);
    auto* addEntryBtn = button(tr("Add Firm"), "addEntry");
    removeEntryBtn_ = button(tr("Remove Firm"), "removeEntry");
    upBtn_ = button(tr("Move Up"), "entryUp");
    downBtn_ = button(tr("Move Down"), "entryDown");
    auto* entryButtons = new QHBoxLayout;
    entryButtons->addWidget(addEntryBtn);
    entryButtons->addWidget(removeEntryBtn_);
    entryButtons->addStretch();
    entryButtons->addWidget(upBtn_);
    entryButtons->addWidget(downBtn_);
    entriesLayout->addLayout(entryButtons);
    entriesLayout->addWidget(hint(tr("The first firm goes in the OPC, OPC Firm Name and OPC Address columns; any others "
                                     "go in OPC 2 Address. Every ticked attorney's email goes in eService Email.")));

    // ---- Right: the chosen firm and its attorneys ----
    auto* firmBox = new QGroupBox(tr("Firm and attorneys"));
    auto* firmLayout = new QVBoxLayout(firmBox);
    firmCombo_ = new QComboBox;
    firmCombo_->setObjectName("firm");
    auto* newFirmBtn = button(tr("New Firm..."), "newFirm");
    editFirmBtn_ = button(tr("Edit Firm..."), "editFirm");
    auto* firmRow = new QHBoxLayout;
    firmRow->addWidget(new QLabel(tr("Firm:")));
    firmRow->addWidget(firmCombo_, 1);
    firmRow->addWidget(newFirmBtn);
    firmRow->addWidget(editFirmBtn_);
    firmLayout->addLayout(firmRow);
    firmDetails_ = new QLabel;
    firmDetails_->setObjectName("firmDetails");
    firmDetails_->setWordWrap(true);
    firmDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    firmLayout->addWidget(firmDetails_);
    firmLayout->addWidget(new QLabel(tr("Tick the attorneys on this case:")));
    attorneyList_ = new QListWidget;
    attorneyList_->setObjectName("attorneys");
    firmLayout->addWidget(attorneyList_, 1);
    newAttorneyBtn_ = button(tr("New Attorney..."), "newAttorney");
    editAttorneyBtn_ = button(tr("Edit Attorney..."), "editAttorney");
    auto* attorneyButtons = new QHBoxLayout;
    attorneyButtons->addWidget(newAttorneyBtn_);
    attorneyButtons->addWidget(editAttorneyBtn_);
    attorneyButtons->addStretch();
    firmLayout->addLayout(attorneyButtons);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(entriesBox);
    splitter->addWidget(firmBox);
    splitter->setSizes({380, 460});
    layout->addWidget(splitter, 3);

    // ---- What will go into the case ----
    auto* previewBox = new QGroupBox(tr("What will be filled in"));
    auto* previewLayout = new QVBoxLayout(previewBox);
    preview_ = new QPlainTextEdit;
    preview_->setObjectName("preview");
    preview_->setReadOnly(true);
    previewLayout->addWidget(preview_);
    layout->addWidget(previewBox, 2);

    auto* buttons = new QDialogButtonBox;
    useBtn_ = buttons->addButton(tr("Use These"), QDialogButtonBox::AcceptRole);
    useBtn_->setObjectName("useThese");
    auto* proSe = buttons->addButton(tr("Pro Se (Type by Hand)"), QDialogButtonBox::ResetRole);
    proSe->setObjectName("proSe");
    proSe->setToolTip(tr("The other party is representing themselves: close this window and type their details "
                         "into the case's boxes by hand"));
    auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    cancel->setObjectName("cancel");
    connect(proSe, &QPushButton::clicked, this, [this] { done(kProSe); });
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(entryList_, &QListWidget::currentRowChanged, this, [this] {
        if (!populating_) showEntry();
    });
    connect(addEntryBtn, &QPushButton::clicked, this, [this] {
        addFirmEntry();
        firmCombo_->setFocus();
    });
    connect(removeEntryBtn_, &QPushButton::clicked, this, &OpposingCounselDialog::removeEntry);
    connect(upBtn_, &QPushButton::clicked, this, [this] { moveEntry(-1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { moveEntry(+1); });
    connect(firmCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (!populating_) setEntryFirm(firmCombo_->currentData().toLongLong());
    });
    connect(newFirmBtn, &QPushButton::clicked, this, &OpposingCounselDialog::newFirm);
    connect(editFirmBtn_, &QPushButton::clicked, this, &OpposingCounselDialog::editFirm);
    connect(attorneyList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (populating_ || !item) return;
        setAttorneyChecked(item->data(Qt::UserRole).toLongLong(), item->checkState() == Qt::Checked);
    });
    connect(attorneyList_, &QListWidget::currentRowChanged, this, [this] { updateButtons(); });
    connect(attorneyList_, &QListWidget::itemDoubleClicked, this, [this] { editAttorney(); });
    connect(newAttorneyBtn_, &QPushButton::clicked, this, &OpposingCounselDialog::newAttorney);
    connect(editAttorneyBtn_, &QPushButton::clicked, this, &OpposingCounselDialog::editAttorney);

    for (const auto& c : initial) {
        Entry e;
        e.firmId = c.firm.id;
        for (const auto& a : c.attorneys) e.checked.insert(a.id);
        entries_.push_back(std::move(e));
    }
    reloadDirectory();
    if (entries_.empty()) entries_.push_back({});  // start with one firm to fill in
    populateEntries();
    entryList_->setCurrentRow(0);
    showEntry();
    resize(900, 680);
}

OpposingCounselDialog::~OpposingCounselDialog()
{
    disconnectChildren(this);
}

// ---- data -----------------------------------------------------------------------------------------

void OpposingCounselDialog::reloadDirectory()
{
    runGuarded(this, [&] {
        firms_ = db_.listFirms();
        attorneys_ = db_.listAllAttorneys();
    });
    for (auto& e : entries_) {
        if (e.firmId && !findFirm(e.firmId)) e = Entry{};
        for (auto it = e.checked.begin(); it != e.checked.end();) {
            const Attorney* a = findAttorney(*it);
            if (!a || a->firmId != e.firmId) it = e.checked.erase(it);  // deleted or moved to another firm
            else ++it;
        }
    }
}

const Firm* OpposingCounselDialog::findFirm(std::int64_t id) const
{
    for (const auto& f : firms_)
        if (f.id == id) return &f;
    return nullptr;
}

const Attorney* OpposingCounselDialog::findAttorney(std::int64_t id) const
{
    for (const auto& a : attorneys_)
        if (a.id == id) return &a;
    return nullptr;
}

std::vector<OpposingFirmChoice> OpposingCounselDialog::choices() const
{
    std::vector<OpposingFirmChoice> out;
    for (const auto& e : entries_) {
        const Firm* f = findFirm(e.firmId);
        if (!f) continue;  // a row whose firm was never chosen
        OpposingFirmChoice c{*f, {}};
        for (const auto& a : attorneys_)  // directory order (by name)
            if (a.firmId == f->id && e.checked.count(a.id)) c.attorneys.push_back(a);
        out.push_back(std::move(c));
    }
    return out;
}

int OpposingCounselDialog::currentEntry() const
{
    const int r = entryList_->currentRow();
    return r >= 0 && r < static_cast<int>(entries_.size()) ? r : -1;
}

// ---- entries --------------------------------------------------------------------------------------

void OpposingCounselDialog::populateEntries()
{
    const int keep = entryList_->currentRow();
    populating_ = true;
    entryList_->clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        const Firm* f = findFirm(e.firmId);
        QString text = QStringLiteral("%1. ").arg(i + 1);
        if (!f) {
            text += tr("(choose a firm)");
        } else {
            const auto total = std::count_if(attorneys_.begin(), attorneys_.end(), [&](const Attorney& a) { return a.firmId == f->id; });
            text += qstr(f->name) + "  -  " +
                    tr("%1 of %2 attorney(s)").arg(e.checked.size()).arg(total);
        }
        entryList_->addItem(text);
    }
    if (!entries_.empty()) entryList_->setCurrentRow(std::clamp(keep, 0, static_cast<int>(entries_.size()) - 1));
    populating_ = false;
    updatePreview();
    updateButtons();
}

void OpposingCounselDialog::addFirmEntry(std::int64_t firmId)
{
    entries_.push_back({});
    populateEntries();
    entryList_->setCurrentRow(static_cast<int>(entries_.size()) - 1);
    showEntry();
    if (firmId) setEntryFirm(firmId);
}

void OpposingCounselDialog::removeEntry()
{
    const int r = currentEntry();
    if (r < 0) return;
    entries_.erase(entries_.begin() + r);
    populateEntries();
    showEntry();
}

void OpposingCounselDialog::moveEntry(int delta)
{
    const int r = currentEntry();
    const int to = r + delta;
    if (r < 0 || to < 0 || to >= static_cast<int>(entries_.size())) return;
    std::swap(entries_[static_cast<std::size_t>(r)], entries_[static_cast<std::size_t>(to)]);
    populateEntries();
    entryList_->setCurrentRow(to);
    showEntry();
}

void OpposingCounselDialog::setEntryFirm(std::int64_t firmId)
{
    const int r = currentEntry();
    if (r < 0) return;
    auto& e = entries_[static_cast<std::size_t>(r)];
    if (e.firmId == firmId) return;
    e.firmId = findFirm(firmId) ? firmId : 0;
    e.checked.clear();
    // A firm with just one attorney: tick them, it's almost always who is meant.
    std::vector<std::int64_t> at;
    for (const auto& a : attorneys_)
        if (a.firmId == e.firmId) at.push_back(a.id);
    if (at.size() == 1) e.checked.insert(at.front());
    populateEntries();
    showEntry();
}

void OpposingCounselDialog::setAttorneyChecked(std::int64_t attorneyId, bool on)
{
    const int r = currentEntry();
    if (r < 0) return;
    auto& e = entries_[static_cast<std::size_t>(r)];
    const Attorney* a = findAttorney(attorneyId);
    if (!a || a->firmId != e.firmId) return;
    if (on) e.checked.insert(attorneyId);
    else e.checked.erase(attorneyId);
    populateEntries();
    // keep the list's check boxes in step when called from code
    populating_ = true;
    for (int i = 0; i < attorneyList_->count(); ++i) {
        auto* item = attorneyList_->item(i);
        if (item->data(Qt::UserRole).toLongLong() == attorneyId) item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    populating_ = false;
}

void OpposingCounselDialog::showEntry()
{
    const int r = currentEntry();
    populating_ = true;
    firmCombo_->clear();
    attorneyList_->clear();
    firmDetails_->clear();
    if (r >= 0) {
        const auto& e = entries_[static_cast<std::size_t>(r)];
        // Opposing-counsel firms not already on another row (plus this row's own firm).
        std::set<std::int64_t> used;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (static_cast<int>(i) != r && entries_[i].firmId) used.insert(entries_[i].firmId);
        firmCombo_->addItem(tr("(choose a firm)"), QVariant::fromValue<qlonglong>(0));
        for (const auto& f : firms_) {
            if (f.id != e.firmId && (f.ours || used.count(f.id))) continue;
            firmCombo_->addItem(qstr(f.name), QVariant::fromValue<qlonglong>(f.id));
            if (f.id == e.firmId) firmCombo_->setCurrentIndex(firmCombo_->count() - 1);
        }
        if (const Firm* f = findFirm(e.firmId)) {
            QStringList lines;
            if (!f->address.empty()) lines << qstr(f->address);
            if (!f->phone.empty()) lines << tr("Phone: %1").arg(qstr(f->phone));
            if (!f->fax.empty()) lines << tr("Fax: %1").arg(qstr(f->fax));
            if (lines.isEmpty()) lines << tr("(no address on file)");
            firmDetails_->setText(lines.join('\n'));
            for (const auto& a : attorneys_) {
                if (a.firmId != f->id) continue;
                QString text = qstr(a.name);
                if (!a.email.empty()) text += "   <" + qstr(a.email) + ">";
                auto* item = new QListWidgetItem(text);
                item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(a.id));
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(e.checked.count(a.id) ? Qt::Checked : Qt::Unchecked);
                attorneyList_->addItem(item);
            }
            if (attorneyList_->count() == 0) firmDetails_->setText(firmDetails_->text() + "\n" + tr("No attorneys at this firm yet: click New Attorney..."));
        } else {
            firmDetails_->setText(tr("Choose the firm from the list, or click New Firm... to add it to the directory."));
        }
    }
    populating_ = false;
    updateButtons();
}

void OpposingCounselDialog::updateButtons()
{
    const int r = currentEntry();
    const bool haveFirm = r >= 0 && findFirm(entries_[static_cast<std::size_t>(r)].firmId);
    removeEntryBtn_->setEnabled(r >= 0);
    upBtn_->setEnabled(r > 0);
    downBtn_->setEnabled(r >= 0 && r + 1 < static_cast<int>(entries_.size()));
    firmCombo_->setEnabled(r >= 0);
    editFirmBtn_->setEnabled(haveFirm);
    newAttorneyBtn_->setEnabled(haveFirm);
    editAttorneyBtn_->setEnabled(haveFirm && attorneyList_->currentItem());
}

void OpposingCounselDialog::updatePreview()
{
    const auto text = opposingCounselText(choices());
    auto show = [](const QString& column, const std::string& value) {
        return column + ":\n" + (value.empty() ? QObject::tr("    (blank)") : "    " + qstr(value).replace('\n', "\n    "));
    };
    QStringList parts;
    parts << show("OPC", text.names) << show("OPC Firm Name", text.firmName) << show("OPC Address", text.address)
          << show("OPC 2 Address", text.others)
          << (text.emails.empty() ? tr("eService Email:\n    (left as it is: no ticked attorney has an email)")
                                  : show("eService Email", text.emails));
    preview_->setPlainText(parts.join("\n\n"));
}

// ---- directory edits ------------------------------------------------------------------------------

void OpposingCounselDialog::newFirm()
{
    const auto f = createFirmInteractive(db_, this, false);
    if (!f) return;
    reloadDirectory();
    int r = currentEntry();
    if (r < 0 || entries_[static_cast<std::size_t>(r)].firmId != 0) {
        // Put the new firm on a new row rather than replacing a firm already chosen.
        entries_.push_back({});
        r = static_cast<int>(entries_.size()) - 1;
    }
    populateEntries();
    entryList_->setCurrentRow(r);
    showEntry();
    setEntryFirm(f->id);
}

void OpposingCounselDialog::editFirm()
{
    const int r = currentEntry();
    if (r < 0) return;
    const Firm* f = findFirm(entries_[static_cast<std::size_t>(r)].firmId);
    if (!f || !editFirmInteractive(db_, this, *f)) return;
    reloadDirectory();
    populateEntries();
    showEntry();
}

void OpposingCounselDialog::newAttorney()
{
    const int r = currentEntry();
    if (r < 0) return;
    const std::int64_t firmId = entries_[static_cast<std::size_t>(r)].firmId;
    if (!findFirm(firmId)) return;
    const auto a = createAttorneyInteractive(db_, this, firmId);
    if (!a) return;
    reloadDirectory();
    for (auto& e : entries_)  // tick the new attorney wherever their firm is on the case
        if (e.firmId == a->firmId) e.checked.insert(a->id);
    populateEntries();
    showEntry();
}

void OpposingCounselDialog::editAttorney()
{
    auto* item = attorneyList_->currentItem();
    if (!item) return;
    const Attorney* a = findAttorney(item->data(Qt::UserRole).toLongLong());
    if (!a) return;
    const Attorney before = *a;
    const auto after = editAttorneyInteractive(db_, this, before);
    if (!after) return;
    const bool wasTicked = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.checked.count(before.id) > 0; });
    reloadDirectory();  // drops the tick if they moved firms
    if (wasTicked && after->firmId != before.firmId)
        for (auto& e : entries_)
            if (e.firmId == after->firmId) e.checked.insert(after->id);  // moved to another firm on this case
    populateEntries();
    showEntry();
}
