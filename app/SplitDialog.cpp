#include "SplitDialog.h"

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
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QKeyEvent>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "PdfPreview.h"
#include "QtHelpers.h"
#include "finrenamer/Utf8Path.h"

using namespace finrenamer;
using namespace ui;

namespace {

enum PageColumn { PgPage = 0, PgStarts, PgFile, PgCount };
enum PartColumn { PtNumber = 0, PtPages, PtName, PtCount };

QPushButton* button(const QString& text, const QString& objectName)
{
    auto* b = new QPushButton(text);
    b->setObjectName(objectName);
    b->setAutoDefault(false);
    return b;
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

}  // namespace

SplitDialog::SplitDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("PDF Splitter - %1").arg(appTitle()));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    setAcceptDrops(true);
    buildUi();
    updateButtons();
    report(tr("Choose a PDF to split."));
}

SplitDialog::~SplitDialog()
{
    disconnectChildren(this);
}

void SplitDialog::buildUi()
{
    // ---- Source ----
    pdfFile_ = new QLineEdit;
    pdfFile_->setObjectName("pdfFile");
    pdfFile_->setReadOnly(true);
    pdfFile_->setPlaceholderText(tr("Choose or drag in the PDF to split"));
    auto* browsePdfBtn = button(tr("Browse..."), "browsePdf");
    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new QLabel(tr("PDF:")));
    sourceRow->addWidget(pdfFile_, 1);
    sourceRow->addWidget(browsePdfBtn);

    // ---- Pages ----
    auto* pagesBox = new QGroupBox(tr("Pages"));
    auto* pagesLayout = new QVBoxLayout(pagesBox);
    auto* hint = new QLabel(tr("Click a page to see it. Tick \"New file\" (or press Space) on the first page of each new file; "
                               "the shading shows which file each page goes into."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(placeholder-text);");
    pagesLayout->addWidget(hint);

    pages_ = new QTableWidget(0, PgCount);
    pages_->setObjectName("pages");
    pages_->setHorizontalHeaderLabels({tr("Page"), tr("New file"), tr("Goes into file")});
    pages_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pages_->setSelectionMode(QAbstractItemView::SingleSelection);
    pages_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pages_->verticalHeader()->setVisible(false);
    pages_->horizontalHeader()->setStretchLastSection(true);
    pages_->setColumnWidth(PgPage, 60);
    pages_->setColumnWidth(PgStarts, 80);
    pagesLayout->addWidget(pages_, 1);

    startHereBtn_ = button(tr("Start New File Here"), "startHere");
    startHereBtn_->setToolTip(tr("Mark or unmark the selected page as the first page of a new file"));
    everyBtn_ = button(tr("Every N Pages..."), "everyN");
    everyBtn_->setToolTip(tr("For statements that are all the same length: mark a new file every N pages"));
    clearBtn_ = button(tr("Clear Marks"), "clearMarks");
    auto* pageButtons = new QHBoxLayout;
    pageButtons->addWidget(startHereBtn_);
    pageButtons->addWidget(everyBtn_);
    pageButtons->addStretch();
    pageButtons->addWidget(clearBtn_);
    pagesLayout->addLayout(pageButtons);

    // ---- Files that will be made ----
    auto* partsBox = new QGroupBox(tr("Files that will be made"));
    auto* partsLayout = new QVBoxLayout(partsBox);
    partsTable_ = new QTableWidget(0, PtCount);
    partsTable_->setObjectName("parts");
    partsTable_->setHorizontalHeaderLabels({tr("#"), tr("Pages"), tr("File name (double-click to change)")});
    partsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    partsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    partsTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    partsTable_->verticalHeader()->setVisible(false);
    partsTable_->horizontalHeader()->setStretchLastSection(true);
    partsTable_->setColumnWidth(PtNumber, 40);
    partsTable_->setColumnWidth(PtPages, 80);
    partsLayout->addWidget(partsTable_);

    auto* left = new QSplitter(Qt::Vertical);
    left->addWidget(pagesBox);
    left->addWidget(partsBox);
    left->setStretchFactor(0, 3);
    left->setStretchFactor(1, 2);

    // ---- Preview ----
    auto* previewBox = new QGroupBox(tr("Preview"));
    auto* previewLayout = new QVBoxLayout(previewBox);
    preview_ = new PdfPreview;
    preview_->setObjectName("preview");
    previewLayout->addWidget(preview_);

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(left);
    split->addWidget(previewBox);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    // ---- Output ----
    output_ = new QLineEdit;
    output_->setObjectName("outputFolder");
    auto* browseOutBtn = button(tr("Browse..."), "browseOutput");
    auto* outBox = new QGroupBox(tr("Save the new files in"));
    auto* outLayout = new QVBoxLayout(outBox);
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(output_, 1);
    outRow->addWidget(browseOutBtn);
    outLayout->addLayout(outRow);
    auto* outHint = new QLabel(tr("The original is never changed. If a name is already taken in this folder, \" (2)\" is added. "
                                  "The new files have no password or restrictions."));
    outHint->setWordWrap(true);
    outHint->setStyleSheet("color: palette(placeholder-text);");
    outLayout->addWidget(outHint);

    // ---- Footer ----
    summary_ = new QLabel;
    summary_->setObjectName("summary");
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    openOutputBtn_ = button(tr("Open Output Folder"), "openOutput");
    splitBtn_ = button(tr("Split PDF"), "split");
    auto* closeBtn = button(tr("Close"), "close");
    auto* footer = new QHBoxLayout;
    footer->addWidget(summary_, 1);
    footer->addWidget(openOutputBtn_);
    footer->addWidget(splitBtn_);
    footer->addWidget(closeBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(sourceRow);
    layout->addWidget(split, 1);
    layout->addWidget(outBox);
    layout->addLayout(footer);

    connect(browsePdfBtn, &QPushButton::clicked, this, &SplitDialog::browsePdf);
    connect(browseOutBtn, &QPushButton::clicked, this, &SplitDialog::browseOutput);
    connect(openOutputBtn_, &QPushButton::clicked, this, &SplitDialog::openOutput);
    connect(splitBtn_, &QPushButton::clicked, this, [this] { splitNow(); });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(startHereBtn_, &QPushButton::clicked, this, &SplitDialog::toggleCurrent);
    connect(everyBtn_, &QPushButton::clicked, this, &SplitDialog::askEveryN);
    connect(clearBtn_, &QPushButton::clicked, this, &SplitDialog::clearMarks);
    connect(output_, &QLineEdit::textChanged, this, [this] {
        if (!refreshing_) dirty_ = true;
        updateButtons();
    });

    pages_->installEventFilter(this);

    // Clicking a page shows it; scrolling the preview selects its page.
    connect(pages_, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (!refreshing_ && row >= 0) preview_->showPage(row);
        updateButtons();
    });
    connect(preview_, &PdfPreview::currentPageChanged, this, [this](int page) {
        if (refreshing_ || page < 0 || page >= pages_->rowCount() || pages_->currentRow() == page) return;
        pages_->setCurrentCell(page, PgPage);
    });
    connect(pages_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (refreshing_ || !item || item->column() != PgStarts) return;
        setStartsNewFile(item->row() + 1, item->checkState() == Qt::Checked);
    });
    connect(partsTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (refreshing_ || !item || item->column() != PtName) return;
        const int index = item->row();
        const auto ranges = rangesFromStarts(pageCount_, starts_);
        if (index < 0 || index >= static_cast<int>(ranges.size())) return;
        customNames_[ranges[static_cast<std::size_t>(index)].first] = item->text();
        dirty_ = true;
        updateButtons();
    });

    resize(1180, 760);
}

// ---- opening a PDF --------------------------------------------------------------------------

bool SplitDialog::loadPdf(const QString& path, const QString& password)
{
    const auto file = toPath(QFileInfo(path).absoluteFilePath());
    const auto info = readPdfPages(file, stdstr(password));
    if (!info.ok) {
        report(qstr(info.message), true);
        return false;
    }
    source_ = file;
    password_ = stdstr(password);
    pageCount_ = info.pageCount;
    starts_ = {1};
    customNames_.clear();
    pdfFile_->setText(QDir::toNativeSeparators(path));

    refreshing_ = true;
    output_->setText(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    refreshing_ = false;

    preview_->showFile(QFileInfo(path).absoluteFilePath(), password);
    refreshPages();
    refreshParts();
    pages_->setCurrentCell(0, PgPage);
    dirty_ = true;
    updateButtons();
    report(tr("%n page(s). Mark the first page of each new file.", nullptr, pageCount_));
    return true;
}

void SplitDialog::browsePdf()
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose the PDF to split"), settings.value("split/lastDir").toString(),
                                                      tr("PDF files (*.pdf);;All files (*)"));
    if (path.isEmpty()) return;
    settings.setValue("split/lastDir", QFileInfo(path).absolutePath());

    QString password;
    for (;;) {
        const auto info = readPdfPages(toPath(path), stdstr(password));
        if (!info.needsPassword) break;
        bool ok = false;
        password = QInputDialog::getText(this, tr("Password needed"),
                                         info.wrongPassword ? tr("That password didn't work. Enter the password to open this PDF:")
                                                            : tr("This PDF asks for a password to open. Enter it:"),
                                         QLineEdit::Password, QString(), &ok);
        if (!ok) return;
    }
    loadPdf(path, password);
}

bool SplitDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == pages_ && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space) {
        toggleCurrent();
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void SplitDialog::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void SplitDialog::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && url.toLocalFile().endsWith(".pdf", Qt::CaseInsensitive)) {
            loadPdf(url.toLocalFile());
            return;
        }
    }
}

// ---- marking pages --------------------------------------------------------------------------

int SplitDialog::currentPage() const
{
    const int row = pages_->currentRow();
    return row >= 0 && row < pageCount_ ? row + 1 : 0;
}

void SplitDialog::setStartsNewFile(int page, bool on)
{
    if (page <= 1 || page > pageCount_) return;
    if (on) starts_.insert(page);
    else starts_.erase(page);
    dirty_ = true;
    refreshPages();
    refreshParts();
    updateButtons();
}

void SplitDialog::setStartsEvery(int step)
{
    starts_ = startsEvery(pageCount_, step);
    dirty_ = true;
    refreshPages();
    refreshParts();
    updateButtons();
}

void SplitDialog::toggleCurrent()
{
    const int page = currentPage();
    if (page > 1) setStartsNewFile(page, !starts_.count(page));
}

void SplitDialog::askEveryN()
{
    if (pageCount_ < 2) return;
    bool ok = false;
    const int step = QInputDialog::getInt(this, tr("Every N Pages"),
                                          tr("Start a new file every how many pages?\n(This replaces the marks you have now.)"), 4, 1,
                                          pageCount_, 1, &ok);
    if (ok) setStartsEvery(step);
}

void SplitDialog::clearMarks()
{
    setStartsEvery(0);
}

// ---- tables ---------------------------------------------------------------------------------

void SplitDialog::refreshPages()
{
    refreshing_ = true;
    const int keep = pages_->currentRow();
    const auto ranges = rangesFromStarts(pageCount_, starts_);
    pages_->setRowCount(pageCount_);
    const QColor shades[2] = {palette().color(QPalette::Base), palette().color(QPalette::AlternateBase)};
    int part = 0;
    for (int row = 0; row < pageCount_; ++row) {
        const int page = row + 1;
        if (static_cast<std::size_t>(part) + 1 < ranges.size() && page >= ranges[static_cast<std::size_t>(part) + 1].first) ++part;

        auto* number = readOnlyItem(QString::number(page));
        auto* mark = new QTableWidgetItem;
        mark->setFlags(page == 1 ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        mark->setCheckState(starts_.count(page) || page == 1 ? Qt::Checked : Qt::Unchecked);
        auto* file = readOnlyItem(tr("File %1").arg(part + 1));
        for (QTableWidgetItem* item : {number, mark, file}) item->setBackground(shades[part % 2]);
        pages_->setItem(row, PgPage, number);
        pages_->setItem(row, PgStarts, mark);
        pages_->setItem(row, PgFile, file);
    }
    if (keep >= 0 && keep < pageCount_) pages_->setCurrentCell(keep, PgPage);
    refreshing_ = false;
}

QString SplitDialog::defaultName(int index, int total) const
{
    return qstr(defaultPartName(utf8FromPath(source_.stem()), index, total));
}

void SplitDialog::refreshParts()
{
    refreshing_ = true;
    const auto ranges = rangesFromStarts(pageCount_, starts_);
    const int total = static_cast<int>(ranges.size());
    partsTable_->setRowCount(total);
    for (int i = 0; i < total; ++i) {
        const PageRange& range = ranges[static_cast<std::size_t>(i)];
        const auto custom = customNames_.find(range.first);
        auto* name = new QTableWidgetItem(custom != customNames_.end() ? custom->second : defaultName(i + 1, total));
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        partsTable_->setItem(i, PtNumber, readOnlyItem(QString::number(i + 1)));
        partsTable_->setItem(i, PtPages, readOnlyItem(qstr(rangeText(range))));
        partsTable_->setItem(i, PtName, name);
    }
    refreshing_ = false;
}

std::vector<SplitPart> SplitDialog::parts() const
{
    std::vector<SplitPart> result;
    const auto ranges = rangesFromStarts(pageCount_, starts_);
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        QString name = partsTable_->item(static_cast<int>(i), PtName) ? partsTable_->item(static_cast<int>(i), PtName)->text() : QString();
        result.push_back({ranges[i], stdstr(name.trimmed())});
    }
    return result;
}

void SplitDialog::setPartName(int index, const QString& name)
{
    if (index < 0 || index >= partsTable_->rowCount()) return;
    partsTable_->item(index, PtName)->setText(name);  // itemChanged remembers it
}

// ---- output ---------------------------------------------------------------------------------

void SplitDialog::setOutputFolder(const QString& folder)
{
    output_->setText(folder);
}

void SplitDialog::browseOutput()
{
    const QString start = output_->text().trimmed();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Save the new files in"), start);
    if (!folder.isEmpty()) output_->setText(QDir::toNativeSeparators(folder));
}

void SplitDialog::openOutput()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(output_->text().trimmed()));
}

std::vector<SplitPartResult> SplitDialog::splitNow()
{
    const QString outText = output_->text().trimmed();
    if (pageCount_ < 1 || outText.isEmpty()) return {};

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto split = parts();
    const auto results = splitPdf(source_, split, toPath(outText), password_);
    QApplication::restoreOverrideCursor();

    int made = 0;
    QStringList problems;
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (results[i].ok) ++made;
        else problems << tr("%1: %2").arg(qstr(split[i].name), qstr(results[i].message));
    }
    if (problems.isEmpty()) {
        report(tr("Made %n file(s) in %1.", nullptr, made).arg(QDir::toNativeSeparators(outText)));
        dirty_ = false;
    } else {
        report(tr("Made %1 of %2 files. Problems:\n%3").arg(made).arg(results.size()).arg(problems.join('\n')), true);
    }
    updateButtons();
    return results;
}

void SplitDialog::updateButtons()
{
    const bool loaded = pageCount_ > 0;
    const int page = currentPage();
    startHereBtn_->setEnabled(page > 1);
    everyBtn_->setEnabled(pageCount_ > 1);
    clearBtn_->setEnabled(starts_.size() > 1);
    splitBtn_->setEnabled(loaded && dirty_ && !output_->text().trimmed().isEmpty());
    openOutputBtn_->setEnabled(!output_->text().trimmed().isEmpty());
    if (loaded) {
        const bool marked = starts_.count(page) > 0;
        startHereBtn_->setText(page > 1 && marked ? tr("Don't Start a New File Here") : tr("Start New File Here"));
    }
}

void SplitDialog::report(const QString& text, bool error)
{
    summary_->setText(text);
    summary_->setStyleSheet(error ? "QLabel { color: #c42b1c; }" : QString());
}

QString SplitDialog::summaryText() const
{
    return summary_->text();
}
