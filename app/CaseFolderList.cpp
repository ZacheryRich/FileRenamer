#include "CaseFolderList.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "QtHelpers.h"
#include "finrenamer/Utf8Path.h"

using namespace ui;

namespace {

// Kept from the first version of Update File Names, so lists already saved still load.
QString settingsKey(std::int64_t caseId)
{
    return QStringLiteral("updateNames/case%1/folders").arg(caseId);
}

}  // namespace

CaseFolderList::CaseFolderList(std::int64_t caseId, QWidget* parent) : QWidget(parent), caseId_(caseId)
{
    setAcceptDrops(true);

    list_ = new QListWidget;
    list_->setObjectName("folders");
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setToolTip(tr("You can also drag folders here from File Explorer."));

    auto* addBtn = new QPushButton(tr("Add Folder..."));
    addBtn->setObjectName("addFolder");
    removeBtn_ = new QPushButton(tr("Remove"));
    removeBtn_->setEnabled(false);
    for (auto* b : {addBtn, removeBtn_}) b->setAutoDefault(false);

    side_ = new QVBoxLayout;
    side_->addWidget(addBtn);
    side_->addWidget(removeBtn_);
    side_->addStretch();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(list_, 1);
    layout->addLayout(side_);

    // Folders used for this case before (that still exist).
    QSettings settings(settingsFile(), QSettings::IniFormat);
    for (const QString& f : settings.value(settingsKey(caseId_)).toStringList())
        if (QFileInfo(f).isDir()) list_->addItem(QDir::toNativeSeparators(f));

    connect(addBtn, &QPushButton::clicked, this, &CaseFolderList::promptForFolder);
    connect(removeBtn_, &QPushButton::clicked, this, &CaseFolderList::removeSelected);
    connect(list_, &QListWidget::itemSelectionChanged, this,
            [this] { removeBtn_->setEnabled(!list_->selectedItems().isEmpty()); });
}

CaseFolderList::~CaseFolderList()
{
    disconnectChildren(this);
}

QStringList CaseFolderList::folders() const
{
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) out << QDir::fromNativeSeparators(list_->item(i)->text());
    return out;
}

std::vector<std::filesystem::path> CaseFolderList::paths() const
{
    std::vector<std::filesystem::path> out;
    for (const QString& f : folders()) out.push_back(toPath(f));
    return out;
}

bool CaseFolderList::hasFolders() const
{
    return list_->count() > 0;
}

bool CaseFolderList::promptForFolder()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QStringList current = folders();
    const QString start = current.isEmpty() ? settings.value("lastFolder").toString() : current.last();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Add a folder to search"), start);
    if (folder.isEmpty()) return false;
    addFolder(folder);
    return true;
}

void CaseFolderList::addFolder(const QString& folder)
{
    for (const QString& existing : folders())
        if (QDir::cleanPath(existing).compare(QDir::cleanPath(folder), Qt::CaseInsensitive) == 0) return;
    list_->addItem(QDir::toNativeSeparators(QDir::cleanPath(folder)));
    save();
    emit changed();
}

void CaseFolderList::replaceFolder(const std::filesystem::path& from, const std::filesystem::path& to)
{
    bool any = false;
    for (int i = 0; i < list_->count(); ++i) {
        if (finrenamer::caseFoldKey(toPath(QDir::fromNativeSeparators(list_->item(i)->text()))) ==
            finrenamer::caseFoldKey(from)) {
            list_->item(i)->setText(QDir::toNativeSeparators(qpath(to)));
            any = true;
        }
    }
    if (!any) return;
    save();
    emit changed();
}

void CaseFolderList::addSideWidget(QWidget* widget)
{
    side_->addWidget(widget);
}

void CaseFolderList::removeSelected()
{
    qDeleteAll(list_->selectedItems());
    save();
    emit changed();
}

void CaseFolderList::save() const
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    settings.setValue(settingsKey(caseId_), folders());
}

void CaseFolderList::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void CaseFolderList::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isDir()) addFolder(path);
    }
    event->acceptProposedAction();
}
