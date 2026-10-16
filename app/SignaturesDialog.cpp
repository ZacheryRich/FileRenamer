#include "SignaturesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include "QtHelpers.h"

using namespace finrenamer;
using namespace ui;

std::vector<SignatureEntry> loadSignatures()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    std::vector<SignatureEntry> out;
    const int n = settings.beginReadArray("vacation/signatures");
    for (int i = 0; i < n; ++i) {
        settings.setArrayIndex(i);
        const QString name = settings.value("attorney").toString().trimmed();
        const QString file = settings.value("file").toString();
        if (!name.isEmpty() && !file.isEmpty()) out.push_back({stdstr(name), toPath(file)});
    }
    settings.endArray();
    return out;
}

void saveSignatures(const std::vector<SignatureEntry>& entries)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.remove("vacation/signatures");
    settings.beginWriteArray("vacation/signatures", static_cast<int>(entries.size()));
    for (std::size_t i = 0; i < entries.size(); ++i) {
        settings.setArrayIndex(static_cast<int>(i));
        settings.setValue("attorney", qstr(entries[i].attorney));
        settings.setValue("file", qpath(entries[i].file));
    }
    settings.endArray();
}

namespace {

QString pictureProblem(const QString& file)
{
    if (file.trimmed().isEmpty()) return QObject::tr("Choose the signature picture.");
    if (!QFileInfo::exists(file)) return QObject::tr("That picture file was not found.");
    try {
        imageSize(toPath(file));
    } catch (const std::exception&) {
        return QObject::tr("The signature must be a PNG or JPEG picture.");
    }
    return {};
}

// Add or change one attorney.
class SignerDialog : public QDialog {
public:
    SignerDialog(const QStringList& suggestions, const QString& name, const QString& file, bool nameLocked,
                 QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(nameLocked ? tr("Change Signature") : tr("Add Attorney"));
        name_ = new QComboBox;
        name_->setObjectName("attorneyName");
        name_->setEditable(true);
        name_->setInsertPolicy(QComboBox::NoInsert);
        name_->addItems(suggestions);
        name_->setCurrentText(name);
        name_->setEnabled(!nameLocked);
        name_->setToolTip(tr("Write the name the way it appears under the signature line of the letter "
                             "(the \"Partner on case\" column), or pick it from the list."));
        file_ = new QLineEdit(file);
        file_->setObjectName("signatureFile");
        auto* browse = new QPushButton(tr("Browse..."));
        browse->setAutoDefault(false);
        auto* fileRow = new QHBoxLayout;
        fileRow->addWidget(file_, 1);
        fileRow->addWidget(browse);
        preview_ = new QLabel;
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setMinimumHeight(90);
        preview_->setFrameShape(QFrame::StyledPanel);
        problem_ = new QLabel;
        problem_->setStyleSheet("color: #c42b1c;");
        buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

        auto* form = new QFormLayout;
        form->addRow(tr("Attorney:"), name_);
        form->addRow(tr("Signature picture:"), fileRow);
        auto* layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(preview_);
        layout->addWidget(problem_);
        layout->addWidget(buttons_);
        resize(520, 280);

        connect(browse, &QPushButton::clicked, this, [this] {
            QSettings settings(settingsFile(), QSettings::IniFormat);
            QString start = QFileInfo(file_->text()).absolutePath();
            if (file_->text().trimmed().isEmpty() || !QDir(start).exists())
                start = settings.value("vacation/signatureFolder").toString();
            const QString chosen = QFileDialog::getOpenFileName(this, tr("Choose the Signature Picture"), start,
                                                                tr("Pictures (*.png *.jpg *.jpeg)"));
            if (chosen.isEmpty()) return;
            file_->setText(QDir::toNativeSeparators(chosen));
            settings.setValue("vacation/signatureFolder", QFileInfo(chosen).absolutePath());
        });
        connect(file_, &QLineEdit::textChanged, this, [this] { update(); });
        connect(name_, &QComboBox::editTextChanged, this, [this] { update(); });
        connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        update();
    }

    QString name() const { return name_->currentText().trimmed(); }
    QString file() const { return file_->text().trimmed(); }

private:
    void update()
    {
        const QString problem = pictureProblem(file());
        const bool ok = !name().isEmpty() && problem.isEmpty();
        problem_->setText(name().isEmpty() ? tr("Enter the attorney's name.") : problem);
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
        QPixmap pixmap;
        if (problem.isEmpty()) pixmap.load(file());
        if (pixmap.isNull()) preview_->setText(tr("(no picture)"));
        else preview_->setPixmap(pixmap.scaled(preview_->width() - 8, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QComboBox* name_ = nullptr;
    QLineEdit* file_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* problem_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

}  // namespace

SignaturesDialog::SignaturesDialog(const QStringList& suggestions, QWidget* parent)
    : QDialog(parent), suggestions_(suggestions), entries_(loadSignatures())
{
    setWindowTitle(tr("Attorney Signatures - %1").arg(appTitle()));
    resize(640, 420);

    table_ = new QTableWidget(0, 3);
    table_->setObjectName("signers");
    table_->setHorizontalHeaderLabels({tr("Attorney"), tr("Signature picture"), tr("")});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setColumnWidth(0, 200);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* addBtn = new QPushButton(tr("Add Attorney..."));
    addBtn->setObjectName("addSigner");
    editBtn_ = new QPushButton(tr("Change Picture..."));
    editBtn_->setObjectName("editSigner");
    removeBtn_ = new QPushButton(tr("Remove"));
    removeBtn_->setObjectName("removeSigner");
    for (auto* b : {addBtn, editBtn_, removeBtn_}) b->setAutoDefault(false);
    auto* buttons = new QVBoxLayout;
    buttons->addWidget(addBtn);
    buttons->addWidget(editBtn_);
    buttons->addWidget(removeBtn_);
    buttons->addStretch();
    auto* tableRow = new QHBoxLayout;
    tableRow->addWidget(table_, 1);
    tableRow->addLayout(buttons);

    preview_ = new QLabel;
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(100);
    preview_->setFrameShape(QFrame::StyledPanel);

    auto* hint = new QLabel(tr("Each letter is signed with the picture of the attorney named in its \"Partner on case\" "
                               "column (or \"Attorney\" if that is blank). Names match ignoring capitals and "
                               "punctuation. Remove an attorney who leaves; add new ones as they join. Pictures are used "
                               "from where they are saved."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(placeholder-text);");

    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setObjectName("close");
    closeBtn->setAutoDefault(false);
    auto* footer = new QHBoxLayout;
    footer->addStretch();
    footer->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addLayout(tableRow, 1);
    layout->addWidget(preview_);
    layout->addLayout(footer);

    connect(addBtn, &QPushButton::clicked, this, [this] { edit(true); });
    connect(editBtn_, &QPushButton::clicked, this, [this] { edit(false); });
    connect(removeBtn_, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0) return;
        const QString name = qstr(entries_[static_cast<std::size_t>(row)].attorney);
        if (QMessageBox::question(this, tr("Remove Attorney"),
                                  tr("Remove %1 from the list?\n\nThe picture file itself is not deleted.").arg(name)) ==
            QMessageBox::Yes)
            removeEntry(name);
    });
    connect(table_, &QTableWidget::currentCellChanged, this, [this] { updateButtons(); showPreview(); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this] { edit(false); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    refresh();
}

SignaturesDialog::~SignaturesDialog()
{
    disconnectChildren(this);
}

int SignaturesDialog::count() const { return static_cast<int>(entries_.size()); }

bool SignaturesDialog::addEntry(const SignatureEntry& entry)
{
    for (const auto& e : entries_)
        if (normalizeHeader(e.attorney) == normalizeHeader(entry.attorney)) return false;
    entries_.push_back(entry);
    saveSignatures(entries_);
    refresh();
    return true;
}

bool SignaturesDialog::removeEntry(const QString& attorney)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
        if (normalizeHeader(it->attorney) == normalizeHeader(stdstr(attorney))) {
            entries_.erase(it);
            saveSignatures(entries_);
            refresh();
            return true;
        }
    return false;
}

void SignaturesDialog::edit(bool adding)
{
    const int row = table_->currentRow();
    if (!adding && row < 0) return;
    QStringList names;
    for (const QString& s : suggestions_) {
        bool listed = false;
        for (const auto& e : entries_) listed = listed || normalizeHeader(e.attorney) == normalizeHeader(stdstr(s));
        if (!listed) names << s;
    }
    const SignatureEntry current = adding ? SignatureEntry{} : entries_[static_cast<std::size_t>(row)];
    SignerDialog dialog(names, qstr(current.attorney), adding ? QString() : qpath(current.file), !adding, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const SignatureEntry updated{stdstr(dialog.name()), toPath(dialog.file())};
    if (adding) {
        if (!addEntry(updated))
            QMessageBox::information(this, windowTitle(), tr("%1 is already on the list. Select them and choose "
                                                             "Change Picture instead.").arg(dialog.name()));
    } else {
        entries_[static_cast<std::size_t>(row)].file = updated.file;
        saveSignatures(entries_);
        refresh();
    }
}

void SignaturesDialog::refresh()
{
    const int keep = table_->currentRow();
    const QSignalBlocker block(table_);
    table_->setRowCount(0);
    std::vector<SignatureEntry> sorted = entries_;
    std::sort(sorted.begin(), sorted.end(), [](const SignatureEntry& a, const SignatureEntry& b) {
        return QString::compare(qstr(a.attorney), qstr(b.attorney), Qt::CaseInsensitive) < 0;
    });
    entries_ = sorted;
    for (const auto& e : entries_) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(qstr(e.attorney)));
        table_->setItem(row, 1, new QTableWidgetItem(qpath(e.file)));
        const bool ok = pictureProblem(qpath(e.file)).isEmpty();
        auto* status = new QTableWidgetItem(ok ? QString() : tr("Picture missing"));
        if (!ok) status->setForeground(QColor("#c42b1c"));
        table_->setItem(row, 2, status);
    }
    if (table_->rowCount() > 0) table_->setCurrentCell(std::clamp(keep, 0, table_->rowCount() - 1), 0);
    updateButtons();
    showPreview();
}

void SignaturesDialog::updateButtons()
{
    const bool selected = table_->currentRow() >= 0;
    editBtn_->setEnabled(selected);
    removeBtn_->setEnabled(selected);
}

void SignaturesDialog::showPreview()
{
    const int row = table_->currentRow();
    QPixmap pixmap;
    if (row >= 0) pixmap.load(qpath(entries_[static_cast<std::size_t>(row)].file));
    if (pixmap.isNull()) preview_->setText(table_->rowCount() == 0 ? tr("No attorneys yet. Click Add Attorney.") : tr("(no picture)"));
    else preview_->setPixmap(pixmap.scaled(preview_->width() - 8, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
