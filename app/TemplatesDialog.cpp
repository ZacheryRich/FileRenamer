#include "TemplatesDialog.h"

#include <QCheckBox>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

namespace {

QString exampleName(const DocumentTemplate& t)
{
    return (t.useDate ? QStringLiteral("2026.01.31 ") : QString()) + qstr(sampleLabel(t.pattern)) +
           QStringLiteral(".pdf");
}

// Add or change one template.
class TemplateEditor : public QDialog {
public:
    TemplateEditor(const DocumentTemplate& t, const std::vector<DocumentTemplate>& others, QWidget* parent)
        : QDialog(parent), others_(others), id_(t.id)
    {
        setWindowTitle(t.id ? tr("Edit Template") : tr("New Template"));
        name_ = new QLineEdit(qstr(t.name));
        name_->setObjectName("templateName");
        name_->setPlaceholderText(tr("e.g. Email"));
        pattern_ = new QLineEdit(qstr(t.pattern));
        pattern_->setObjectName("templatePattern");
        pattern_->setPlaceholderText(tr("Email from {Sender} to {Recipient}[ ({Description})]"));
        useDate_ = new QCheckBox(tr("Start the name with a date"));
        useDate_->setObjectName("useDate");
        useDate_->setChecked(t.useDate);
        period_ = new QCheckBox(tr("Also allow a period (start and end date), not just one date"));
        period_->setObjectName("allowPeriod");
        period_->setChecked(t.allowPeriod);
        boxes_ = new QLabel;
        boxes_->setWordWrap(true);
        example_ = new QLabel;
        example_->setWordWrap(true);
        example_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        problem_ = new QLabel;
        problem_->setWordWrap(true);
        problem_->setStyleSheet("color: #c42b1c;");
        auto* help = new QLabel(tr(
            "<p>Write the file name the way you want it, <b>without the date</b> (the date is always added "
            "in front). Put each thing you'll type for every file in <b>{ }</b>, for example "
            "<code>{Sender}</code>.</p>"
            "<p>Put anything that is sometimes left out in <b>[ ]</b>: it disappears, fixed words and all, "
            "when its boxes are blank. A box outside [ ] is required.</p>"
            "<p>To print a real [ ] { } or \\, put a \\ before it.</p>"));
        help->setWordWrap(true);
        help->setStyleSheet("color: palette(placeholder-text);");
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

        auto* form = new QVBoxLayout(this);
        form->addWidget(new QLabel(tr("Name (shown in the Type list):")));
        form->addWidget(name_);
        form->addWidget(new QLabel(tr("File name pattern:")));
        form->addWidget(pattern_);
        form->addWidget(help);
        form->addWidget(useDate_);
        form->addWidget(period_);
        form->addWidget(boxes_);
        form->addWidget(example_);
        form->addWidget(problem_);
        form->addWidget(buttons_);
        resize(560, 420);

        connect(name_, &QLineEdit::textChanged, this, [this] { update(); });
        connect(pattern_, &QLineEdit::textChanged, this, [this] { update(); });
        connect(useDate_, &QCheckBox::toggled, this, [this] { update(); });
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        update();
    }

    DocumentTemplate result() const
    {
        return {id_, stdstr(name_->text().trimmed()), stdstr(pattern_->text()), period_->isChecked() && useDate_->isChecked(),
                useDate_->isChecked()};
    }

private:
    void update()
    {
        period_->setEnabled(useDate_->isChecked());
        const DocumentTemplate t = result();
        std::optional<std::string> error = validateTemplate(t);
        if (!error)
            for (const auto& o : others_)
                if (o.id != id_ && fieldKey(o.name) == fieldKey(t.name))
                    error = "There is already a template named \"" + o.name + "\".";
        problem_->setText(t.name.empty() && t.pattern.empty() ? QString() : (error ? qstr(*error) : QString()));
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(!error);

        QStringList names;
        for (const TemplateField& f : templateFields(t.pattern))
            names << (f.optional ? tr("%1 (optional)").arg(qstr(f.name)) : qstr(f.name));
        boxes_->setText(names.isEmpty() ? QString() : tr("Boxes on the screen: %1").arg(names.join(QStringLiteral(", "))));
        example_->setText(error ? QString() : tr("Example: <b>%1</b>").arg(exampleName(t).toHtmlEscaped()));
    }

    std::vector<DocumentTemplate> others_;
    std::int64_t id_ = 0;
    QLineEdit* name_ = nullptr;
    QLineEdit* pattern_ = nullptr;
    QCheckBox* useDate_ = nullptr;
    QCheckBox* period_ = nullptr;
    QLabel* boxes_ = nullptr;
    QLabel* example_ = nullptr;
    QLabel* problem_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

}  // namespace

TemplatesDialog::TemplatesDialog(Database& db, QWidget* parent) : QDialog(parent), db_(db)
{
    setWindowTitle(tr("File Name Templates - %1").arg(appTitle()));
    resize(720, 400);

    auto* intro = new QLabel(tr("Templates are the types of file you can pick besides Statement and Other, "
                                "e.g. Email or Text. Each one is a pattern with boxes you fill in for every file."));
    intro->setWordWrap(true);
    intro->setStyleSheet("color: palette(placeholder-text);");

    table_ = new QTableWidget(0, 3);
    table_->setObjectName("templates");
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Example"), tr("Dates")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setColumnWidth(0, 170);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* addBtn = new QPushButton(tr("New Template..."));
    addBtn->setObjectName("addTemplate");
    editBtn_ = new QPushButton(tr("Edit..."));
    editBtn_->setObjectName("editTemplate");
    copyBtn_ = new QPushButton(tr("Duplicate"));
    copyBtn_->setObjectName("duplicateTemplate");
    deleteBtn_ = new QPushButton(tr("Delete"));
    deleteBtn_->setObjectName("deleteTemplate");
    auto* examplesBtn = new QPushButton(tr("Add Examples"));
    examplesBtn->setObjectName("addExamples");
    examplesBtn->setToolTip(tr("Adds Email, Text, Text Conversation, Group Text and Letter (skipping any you already have)"));
    auto* exportBtn = new QPushButton(tr("Export..."));
    exportBtn->setObjectName("exportTemplates");
    exportBtn->setToolTip(tr("Save all templates to a file, to share or keep"));
    auto* importBtn = new QPushButton(tr("Import..."));
    importBtn->setObjectName("importTemplates");
    importBtn->setToolTip(tr("Add templates from a file saved with Export"));
    for (auto* b : {addBtn, editBtn_, copyBtn_, deleteBtn_, examplesBtn, exportBtn, importBtn}) b->setAutoDefault(false);

    auto* side = new QVBoxLayout;
    side->addWidget(addBtn);
    side->addWidget(editBtn_);
    side->addWidget(copyBtn_);
    side->addWidget(deleteBtn_);
    side->addSpacing(12);
    side->addWidget(examplesBtn);
    side->addSpacing(12);
    side->addWidget(exportBtn);
    side->addWidget(importBtn);
    side->addStretch();
    auto* middle = new QHBoxLayout;
    middle->addWidget(table_, 1);
    middle->addLayout(side);

    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setObjectName("close");
    closeBtn->setAutoDefault(false);
    auto* footer = new QHBoxLayout;
    footer->addStretch();
    footer->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(middle, 1);
    layout->addLayout(footer);

    connect(addBtn, &QPushButton::clicked, this, &TemplatesDialog::addTemplate);
    connect(editBtn_, &QPushButton::clicked, this, &TemplatesDialog::editTemplate);
    connect(copyBtn_, &QPushButton::clicked, this, &TemplatesDialog::duplicateTemplate);
    connect(deleteBtn_, &QPushButton::clicked, this, &TemplatesDialog::deleteTemplate);
    connect(examplesBtn, &QPushButton::clicked, this, &TemplatesDialog::addExamples);
    connect(exportBtn, &QPushButton::clicked, this, &TemplatesDialog::exportTemplates);
    connect(importBtn, &QPushButton::clicked, this, &TemplatesDialog::importTemplates);
    connect(table_, &QTableWidget::currentCellChanged, this, &TemplatesDialog::updateButtons);
    connect(table_, &QTableWidget::cellDoubleClicked, this, &TemplatesDialog::editTemplate);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    reload();
}

TemplatesDialog::~TemplatesDialog()
{
    disconnectChildren(this);
}

std::optional<DocumentTemplate> TemplatesDialog::current() const
{
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(templates_.size())) return std::nullopt;
    return templates_[static_cast<std::size_t>(row)];
}

void TemplatesDialog::reload(std::int64_t select)
{
    const auto keep = current();
    runGuarded(this, [&] { templates_ = db_.listTemplates(); });
    const std::int64_t want = select ? select : (keep ? keep->id : 0);

    const QSignalBlocker block(table_);
    table_->setRowCount(0);
    int selectRow = 0;
    for (const DocumentTemplate& t : templates_) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(qstr(t.name)));
        table_->setItem(row, 1, new QTableWidgetItem(exampleName(t)));
        table_->setItem(row, 2, new QTableWidgetItem(!t.useDate ? tr("None") : t.allowPeriod ? tr("Date or period") : tr("Date")));
        if (t.id == want) selectRow = row;
    }
    if (table_->rowCount() > 0) table_->setCurrentCell(selectRow, 0);
    updateButtons();
}

void TemplatesDialog::updateButtons()
{
    const bool selected = current().has_value();
    editBtn_->setEnabled(selected);
    copyBtn_->setEnabled(selected);
    deleteBtn_->setEnabled(selected);
}

void TemplatesDialog::addTemplate()
{
    TemplateEditor editor(DocumentTemplate{}, templates_, this);
    if (editor.exec() != QDialog::Accepted) return;
    std::int64_t id = 0;
    if (runGuarded(this, [&] { id = db_.createTemplate(editor.result()); })) reload(id);
}

void TemplatesDialog::editTemplate()
{
    const auto t = current();
    if (!t) return;
    TemplateEditor editor(*t, templates_, this);
    if (editor.exec() != QDialog::Accepted) return;
    if (runGuarded(this, [&] { db_.updateTemplate(editor.result()); })) reload(t->id);
}

void TemplatesDialog::duplicateTemplate()
{
    const auto t = current();
    if (!t) return;
    DocumentTemplate copy = *t;
    copy.id = 0;
    copy.name = t->name + " copy";
    TemplateEditor editor(copy, templates_, this);
    if (editor.exec() != QDialog::Accepted) return;
    std::int64_t id = 0;
    if (runGuarded(this, [&] { id = db_.createTemplate(editor.result()); })) reload(id);
}

void TemplatesDialog::deleteTemplate()
{
    const auto t = current();
    if (!t) return;
    if (QMessageBox::question(this, tr("Delete Template"),
                              tr("Delete the template \"%1\"?\n\nFiles already renamed are not affected. Files on the "
                                 "rename screen that use it (and haven't been renamed yet) keep what you typed as "
                                 "an \"Other\" name.")
                                  .arg(qstr(t->name))) != QMessageBox::Yes)
        return;
    if (runGuarded(this, [&] { db_.deleteTemplate(t->id); })) reload();
}

void TemplatesDialog::addExamples()
{
    int added = 0;
    runGuarded(this, [&] {
        for (const DocumentTemplate& example : exampleTemplates()) {
            const bool exists = std::any_of(templates_.begin(), templates_.end(), [&](const DocumentTemplate& t) {
                return fieldKey(t.name) == fieldKey(example.name);
            });
            if (exists) continue;
            db_.createTemplate(example);
            ++added;
        }
    });
    reload();
    if (added == 0) QMessageBox::information(this, windowTitle(), tr("All the examples are already on the list."));
}

namespace {

QString templatesFolder()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QString dir = settings.value("templates/lastDir").toString();
    return (!dir.isEmpty() && QFileInfo(dir).isDir()) ? dir
                                                       : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void rememberTemplatesFolder(const QString& file)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue("templates/lastDir", QFileInfo(file).absolutePath());
}

}  // namespace

void TemplatesDialog::exportTemplates()
{
    if (templates_.empty()) {
        QMessageBox::information(this, windowTitle(), tr("There are no templates to export yet."));
        return;
    }
    const QString suggested = QDir(templatesFolder()).filePath(QStringLiteral("File name templates.xml"));
    const QString target = QFileDialog::getSaveFileName(this, tr("Export Templates"), suggested, tr("Template file (*.xml)"));
    if (target.isEmpty()) return;
    const std::string xml = templatesToXml(templates_);
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(xml.data(), static_cast<qint64>(xml.size())) != static_cast<qint64>(xml.size())) {
        QMessageBox::warning(this, windowTitle(), tr("Could not write the file:\n%1").arg(file.errorString()));
        return;
    }
    file.close();
    rememberTemplatesFolder(target);
    QMessageBox::information(this, windowTitle(),
                             tr("Saved %1 template(s) to:\n%2").arg(templates_.size()).arg(QDir::toNativeSeparators(target)));
}

void TemplatesDialog::importTemplates()
{
    const QString source = QFileDialog::getOpenFileName(this, tr("Import Templates"), templatesFolder(),
                                                        tr("Template file (*.xml)"));
    if (source.isEmpty()) return;
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not read the file:\n%1").arg(file.errorString()));
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    rememberTemplatesFolder(source);
    const TemplateImport parsed = templatesFromXml(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));

    // Same name as one already here: identical ones are skipped; different ones are the user's call.
    auto sameName = [&](const DocumentTemplate& t) -> const DocumentTemplate* {
        for (const DocumentTemplate& mine : templates_)
            if (fieldKey(mine.name) == fieldKey(t.name)) return &mine;
        return nullptr;
    };
    std::vector<const DocumentTemplate*> toAdd;
    std::vector<std::pair<const DocumentTemplate*, DocumentTemplate>> differing;  // existing, incoming
    int identical = 0;
    for (const DocumentTemplate& t : parsed.templates) {
        const DocumentTemplate* mine = sameName(t);
        if (!mine) toAdd.push_back(&t);
        else if (mine->pattern == t.pattern && mine->allowPeriod == t.allowPeriod && mine->useDate == t.useDate) ++identical;
        else differing.emplace_back(mine, t);
    }

    bool replace = false;
    if (!differing.empty()) {
        QStringList names;
        for (const auto& d : differing) names << qstr(d.first->name);
        QMessageBox box(QMessageBox::Question, windowTitle(),
                        tr("%1 template(s) in the file have the same name as one of yours but a different pattern:\n\n%2\n\n"
                           "Replace yours with the ones from the file?")
                            .arg(differing.size()).arg(names.join('\n')),
                        QMessageBox::NoButton, this);
        auto* replaceBtn = box.addButton(tr("Replace Mine"), QMessageBox::AcceptRole);
        auto* keepBtn = box.addButton(tr("Keep Mine"), QMessageBox::RejectRole);
        auto* cancelBtn = box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == cancelBtn || box.clickedButton() == nullptr) return;
        replace = box.clickedButton() == replaceBtn;
        (void)keepBtn;
    }

    int added = 0, replaced = 0;
    runGuarded(this, [&] {
        for (const DocumentTemplate* t : toAdd) {
            db_.createTemplate(*t);
            ++added;
        }
        if (replace)
            for (const auto& d : differing) {
                DocumentTemplate updated = d.second;
                updated.id = d.first->id;
                db_.updateTemplate(updated);
                ++replaced;
            }
    });
    reload();

    QStringList lines;
    lines << tr("Added %1, replaced %2, already the same %3, kept yours %4.")
                 .arg(added).arg(replaced).arg(identical).arg(replace ? 0 : static_cast<int>(differing.size()));
    for (const std::string& problem : parsed.problems) lines << qstr(problem);
    QMessageBox::information(this, windowTitle(), lines.join('\n'));
}
