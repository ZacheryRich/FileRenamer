#include "CaseAttorneysDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "DirectoryEditors.h"
#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

namespace {

constexpr int kFirmRole = Qt::UserRole;          // firm id (on every row)
constexpr int kAttorneyRole = Qt::UserRole + 1;  // attorney id (attorney rows only)

}  // namespace

// ---- CaseAttorneysDialog ----------------------------------------------------------------------------

CaseAttorneysDialog::CaseAttorneysDialog(Database& db, const QString& caseName, const std::set<std::int64_t>& assigned,
                                         QWidget* parent)
    : QDialog(parent), db_(db), checked_(assigned)
{
    setWindowTitle(tr("Assign Attorneys - %1").arg(caseName));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    auto* intro = new QLabel(tr("Tick the attorneys on this case, from our firm and from the opposing firms."));
    intro->setWordWrap(true);
    search_ = new QLineEdit;
    search_->setObjectName("search");
    search_->setPlaceholderText(tr("Search firms and attorneys"));
    search_->setClearButtonEnabled(true);

    tree_ = new QTreeWidget;
    tree_->setObjectName("attorneyTree");
    tree_->setHeaderLabels({tr("Firm / attorney"), tr("Bar Number"), tr("Email")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    auto* newFirmBtn = new QPushButton(tr("New Firm..."));
    newFirmBtn->setObjectName("newFirm");
    newAttorneyBtn_ = new QPushButton(tr("New Attorney..."));
    newAttorneyBtn_->setObjectName("newAttorney");
    newAttorneyBtn_->setToolTip(tr("Add an attorney to the selected firm"));
    editBtn_ = new QPushButton(tr("Edit..."));
    editBtn_->setObjectName("edit");
    for (auto* b : {newFirmBtn, newAttorneyBtn_, editBtn_}) b->setAutoDefault(false);
    auto* row = new QHBoxLayout;
    row->addWidget(newFirmBtn);
    row->addWidget(newAttorneyBtn_);
    row->addWidget(editBtn_);
    row->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("ok");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(search_);
    layout->addWidget(tree_, 1);
    layout->addLayout(row);
    layout->addWidget(buttons);

    connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (populating_ || column != 0) return;
        const auto id = item->data(0, kAttorneyRole).toLongLong();
        if (!id) return;
        if (item->checkState(0) == Qt::Checked) checked_.insert(id);
        else checked_.erase(id);
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (item && (column != 0 || !item->data(0, kAttorneyRole).toLongLong())) editSelected();
    });
    connect(newFirmBtn, &QPushButton::clicked, this, &CaseAttorneysDialog::newFirm);
    connect(newAttorneyBtn_, &QPushButton::clicked, this, &CaseAttorneysDialog::newAttorney);
    connect(editBtn_, &QPushButton::clicked, this, &CaseAttorneysDialog::editSelected);

    resize(640, 560);
    reload();
}

CaseAttorneysDialog::~CaseAttorneysDialog()
{
    disconnectChildren(this);
}

std::vector<std::int64_t> CaseAttorneysDialog::chosen() const
{
    std::vector<std::int64_t> out;
    for (const auto& a : attorneys_)
        if (checked_.count(a.id)) out.push_back(a.id);
    return out;
}

void CaseAttorneysDialog::setChecked(std::int64_t attorneyId, bool on)
{
    if (on) checked_.insert(attorneyId);
    else checked_.erase(attorneyId);
    populating_ = true;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* firm = tree_->topLevelItem(i);
        for (int j = 0; j < firm->childCount(); ++j)
            if (firm->child(j)->data(0, kAttorneyRole).toLongLong() == attorneyId)
                firm->child(j)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    }
    populating_ = false;
}

void CaseAttorneysDialog::reload()
{
    const std::int64_t keepFirm = selectedFirmId();
    const std::int64_t keepAttorney = selectedAttorneyId();
    runGuarded(this, [&] {
        firms_ = db_.listFirms();
        attorneys_ = db_.listAllAttorneys();
    });
    // Ticks for attorneys deleted meanwhile are dropped.
    for (auto it = checked_.begin(); it != checked_.end();)
        it = std::any_of(attorneys_.begin(), attorneys_.end(), [&](const Attorney& a) { return a.id == *it; })
                 ? std::next(it)
                 : checked_.erase(it);

    std::vector<const Firm*> order;
    for (const auto& f : firms_) order.push_back(&f);
    std::stable_sort(order.begin(), order.end(), [](const Firm* a, const Firm* b) { return a->ours && !b->ours; });

    populating_ = true;
    tree_->clear();
    QTreeWidgetItem* reselect = nullptr;
    for (const Firm* f : order) {
        auto* firmItem = new QTreeWidgetItem(tree_);
        firmItem->setText(0, qstr(f->name) + "  -  " + (f->ours ? tr("our firm") : tr("opposing counsel")));
        firmItem->setData(0, kFirmRole, QVariant::fromValue<qlonglong>(f->id));
        QFont bold = firmItem->font(0);
        bold.setBold(true);
        firmItem->setFont(0, bold);
        firmItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (!keepAttorney && f->id == keepFirm) reselect = firmItem;
        int count = 0;
        for (const auto& a : attorneys_) {
            if (a.firmId != f->id) continue;
            ++count;
            auto* item = new QTreeWidgetItem(firmItem);
            item->setText(0, qstr(a.name));
            item->setText(1, qstr(a.barNumber));
            item->setText(2, qstr(a.email));
            item->setData(0, kFirmRole, QVariant::fromValue<qlonglong>(f->id));
            item->setData(0, kAttorneyRole, QVariant::fromValue<qlonglong>(a.id));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(0, checked_.count(a.id) ? Qt::Checked : Qt::Unchecked);
            if (a.id == keepAttorney) reselect = item;
        }
        if (count == 0) {
            auto* none = new QTreeWidgetItem(firmItem);
            none->setText(0, tr("(no attorneys yet - select the firm and click New Attorney...)"));
            none->setData(0, kFirmRole, QVariant::fromValue<qlonglong>(f->id));
            none->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            none->setForeground(0, palette().placeholderText());
        }
    }
    tree_->expandAll();
    if (firms_.empty()) {
        auto* hint = new QTreeWidgetItem(tree_);
        hint->setText(0, tr("The Attorney Directory is empty. Click New Firm... to add our firm or an opposing firm."));
        hint->setFlags(Qt::ItemIsEnabled);
    }
    if (reselect) tree_->setCurrentItem(reselect);
    populating_ = false;
    applyFilter();
    updateButtons();
}

void CaseAttorneysDialog::applyFilter()
{
    const QString s = search_->text().trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* firm = tree_->topLevelItem(i);
        const bool firmHit = s.isEmpty() || firm->text(0).contains(s, Qt::CaseInsensitive);
        bool anyChild = false;
        for (int j = 0; j < firm->childCount(); ++j) {
            auto* c = firm->child(j);
            const bool hit = firmHit || c->text(0).contains(s, Qt::CaseInsensitive) ||
                             c->text(1).contains(s, Qt::CaseInsensitive) || c->text(2).contains(s, Qt::CaseInsensitive);
            c->setHidden(!hit);
            anyChild = anyChild || hit;
        }
        firm->setHidden(!firmHit && !anyChild);
    }
}

std::int64_t CaseAttorneysDialog::selectedFirmId() const
{
    const auto* item = tree_ ? tree_->currentItem() : nullptr;
    return item ? item->data(0, kFirmRole).toLongLong() : 0;
}

std::int64_t CaseAttorneysDialog::selectedAttorneyId() const
{
    const auto* item = tree_ ? tree_->currentItem() : nullptr;
    return item ? item->data(0, kAttorneyRole).toLongLong() : 0;
}

void CaseAttorneysDialog::updateButtons()
{
    newAttorneyBtn_->setEnabled(selectedFirmId() != 0);
    editBtn_->setEnabled(selectedFirmId() != 0);
    editBtn_->setText(selectedAttorneyId() ? tr("Edit Attorney...") : tr("Edit Firm..."));
}

void CaseAttorneysDialog::newFirm()
{
    if (const auto f = createFirmInteractive(db_, this, false)) {
        search_->clear();
        reload();
        for (int i = 0; i < tree_->topLevelItemCount(); ++i)
            if (tree_->topLevelItem(i)->data(0, kFirmRole).toLongLong() == f->id) tree_->setCurrentItem(tree_->topLevelItem(i));
    }
}

void CaseAttorneysDialog::newAttorney()
{
    const auto firmId = selectedFirmId();
    if (!firmId) return;
    if (const auto a = createAttorneyInteractive(db_, this, firmId)) {
        checked_.insert(a->id);  // added from here: they're on this case
        search_->clear();
        reload();
        setChecked(a->id, true);
    }
}

void CaseAttorneysDialog::editSelected()
{
    if (const auto attorneyId = selectedAttorneyId()) {
        auto it = std::find_if(attorneys_.begin(), attorneys_.end(), [&](const Attorney& a) { return a.id == attorneyId; });
        if (it != attorneys_.end() && editAttorneyInteractive(db_, this, *it)) reload();
        return;
    }
    const auto firmId = selectedFirmId();
    auto it = std::find_if(firms_.begin(), firms_.end(), [&](const Firm& f) { return f.id == firmId; });
    if (it != firms_.end() && editFirmInteractive(db_, this, *it)) reload();
}
