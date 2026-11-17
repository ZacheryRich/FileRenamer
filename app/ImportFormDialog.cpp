#include "ImportFormDialog.h"

#include <QButtonGroup>
#include <QDate>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>
#include <algorithm>

#include "CreateDocumentDialog.h"  // loadCaseFile
#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

namespace {

constexpr int kChangeRole = Qt::UserRole;  // index into changes_

}  // namespace

ImportFormDialog::ImportFormDialog(Database& db, std::int64_t caseId, const QString& pdf, QWidget* parent)
    : QDialog(parent), db_(db), caseId_(caseId)
{
    setWindowTitle(tr("Import Information Form"));
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    std::map<std::string, std::string> fields;
    try {
        fields = readPdfFormFields(toPath(pdf));
    } catch (const std::exception& e) {
        QMessageBox::warning(parent, tr("Import Information Form"), qstr(e.what()));
        return;
    }
    form_ = parseInfoForm(fields);
    if (form_.fieldsFound == 0) {
        QMessageBox::warning(parent, tr("Import Information Form"),
                             fields.empty() ? tr("This PDF has no fillable fields.")
                                            : tr("This PDF's fields aren't the Information Form's, so nothing can be "
                                                 "imported from it."));
        return;
    }
    std::vector<Attorney> directory;
    if (!runGuarded(parent, [&] {
            if (caseId_) file_ = loadCaseFile(db_, caseId_);
            directory = db_.listAllAttorneys();
        }))
        return;
    const QString heading = tr("From the Information Form (%1, imported %2):")
                                .arg(QFileInfo(pdf).fileName(),
                                     QLocale(QLocale::English).toString(QDate::currentDate(), "MMMM d, yyyy"));
    changes_ = planFormImport(file_, form_, directory, stdstr(heading));
    loaded_ = true;

    auto* intro = new QLabel(
        caseId_ ? tr("<b>%1</b> &rarr; %2<br>Tick what to put into the case. Values that would replace something "
                     "different already in the case start unticked.")
                      .arg(QFileInfo(pdf).fileName().toHtmlEscaped(), qstr(caseDisplayName(file_.info)).toHtmlEscaped())
                : tr("<b>%1</b> &rarr; a new case<br>Untick anything you don't want in the case.")
                      .arg(QFileInfo(pdf).fileName().toHtmlEscaped()));
    intro->setWordWrap(true);

    // The client's side: only asked when the client is a new party.
    bool clientExists = false;
    for (const auto& p : file_.parties)
        if (p.ourClient || (!form_.client.name.empty() && QString::compare(qstr(p.name), qstr(form_.client.name), Qt::CaseInsensitive) == 0))
            clientExists = true;
    petitioner_ = new QRadioButton(tr("Petitioner"));
    petitioner_->setObjectName("clientPetitioner");
    respondent_ = new QRadioButton(tr("Respondent"));
    respondent_->setObjectName("clientRespondent");
    auto* sides = new QButtonGroup(this);
    sides->addButton(petitioner_);
    sides->addButton(respondent_);
    petitioner_->setChecked(true);
    auto* sideRow = new QHBoxLayout;
    auto* sideLabel = new QLabel(tr("Our client is the:"));
    sideRow->addWidget(sideLabel);
    sideRow->addWidget(petitioner_);
    sideRow->addWidget(respondent_);
    sideRow->addStretch();
    if (clientExists) {
        sideLabel->setText(tr("Our client is already a party on this case; their side stays as it is."));
        petitioner_->hide();
        respondent_->hide();
    }

    tree_ = new QTreeWidget;
    tree_->setObjectName("changes");
    tree_->setHeaderLabels({tr("Field"), tr("From the form"), tr("In the case now")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    tree_->setWordWrap(true);
    tree_->setUniformRowHeights(false);

    summary_ = new QLabel;
    summary_->setObjectName("summary");
    summary_->setWordWrap(true);

    auto* tickAll = new QPushButton(tr("Tick All"));
    tickAll->setObjectName("tickAll");
    auto* untickAll = new QPushButton(tr("Untick All"));
    untickAll->setObjectName("untickAll");
    for (auto* b : {tickAll, untickAll}) b->setAutoDefault(false);
    connect(tickAll, &QPushButton::clicked, this, [this] { setAll(true); });
    connect(untickAll, &QPushButton::clicked, this, [this] { setAll(false); });

    auto* buttons = new QDialogButtonBox;
    auto* importBtn = buttons->addButton(caseId_ ? tr("Import") : tr("Create Case"), QDialogButtonBox::AcceptRole);
    importBtn->setObjectName("import");
    buttons->addButton(QDialogButtonBox::Cancel)->setObjectName("cancel");
    buttons->addButton(tickAll, QDialogButtonBox::ResetRole);
    buttons->addButton(untickAll, QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (importNow()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(sideRow);
    layout->addWidget(tree_, 1);
    layout->addWidget(summary_);
    layout->addWidget(buttons);

    fillTree();
    resize(980, 680);
}

ImportFormDialog::~ImportFormDialog()
{
    disconnectChildren(this);
}

void ImportFormDialog::fillTree()
{
    tree_->clear();
    std::map<std::string, QTreeWidgetItem*> sections;
    int unticked = 0;
    for (std::size_t i = 0; i < changes_.size(); ++i) {
        const FormChange& c = changes_[i];
        QTreeWidgetItem*& section = sections[c.section];
        if (!section) {
            section = new QTreeWidgetItem(tree_, {qstr(c.section)});
            QFont bold = section->font(0);
            bold.setBold(true);
            section->setFont(0, bold);
            section->setFlags(Qt::ItemIsEnabled);
        }
        auto* item = new QTreeWidgetItem(section, {qstr(c.label), qstr(c.value), qstr(c.current)});
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, c.apply ? Qt::Checked : Qt::Unchecked);
        item->setData(0, kChangeRole, static_cast<int>(i));
        if (!c.apply) {
            ++unticked;
            item->setToolTip(0, tr("Unticked: this would replace what the case has now"));
            item->setForeground(2, QColor(0x9a, 0x5b, 0x00));
        }
    }
    tree_->expandAll();
    if (changes_.empty())
        summary_->setText(tr("Nothing on this form is new to the case."));
    else
        summary_->setText(tr("%n item(s) from the form.", nullptr, static_cast<int>(changes_.size())) +
                          (unticked ? " " + tr("%n would replace something already in the case and start unticked.", nullptr, unticked)
                                    : QString()));
}

void ImportFormDialog::setAll(bool on)
{
    for (int s = 0; s < tree_->topLevelItemCount(); ++s)
        for (int i = 0; i < tree_->topLevelItem(s)->childCount(); ++i)
            tree_->topLevelItem(s)->child(i)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
}

bool ImportFormDialog::importNow()
{
    if (!loaded_) return false;
    for (int s = 0; s < tree_->topLevelItemCount(); ++s)
        for (int i = 0; i < tree_->topLevelItem(s)->childCount(); ++i) {
            QTreeWidgetItem* item = tree_->topLevelItem(s)->child(i);
            changes_[static_cast<std::size_t>(item->data(0, kChangeRole).toInt())].apply = item->checkState(0) == Qt::Checked;
        }
    const PartyRole role = respondent_->isChecked() ? PartyRole::Respondent : PartyRole::Petitioner;
    CaseFile file = file_;
    const auto newAttorneys = applyFormChanges(file, form_, changes_, role);

    return runGuarded(this, [&] {
        if (caseId_ == 0) {
            caseId_ = db_.createCase(file.info, defaultCasePeople());
            file.info.id = caseId_;
        } else {
            db_.updateCase(file.info);
        }
        for (CaseParty p : file.parties) {
            p.caseId = caseId_;
            if (p.id == 0) db_.addParty(p);
            else db_.updateParty(p);
        }
        for (CaseChild k : file.children) {
            k.caseId = caseId_;
            if (k.id == 0) db_.addChild(k);
            else db_.updateChild(k);
        }
        if (!newAttorneys.empty()) {
            std::vector<std::int64_t> ids;
            for (const auto& a : db_.listCaseAttorneys(caseId_)) ids.push_back(a.id);
            for (const auto id : newAttorneys)
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            db_.setCaseAttorneys(caseId_, ids);
        }
    });
}
