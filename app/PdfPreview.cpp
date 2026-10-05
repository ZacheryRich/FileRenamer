#include "PdfPreview.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef FINRENAMER_HAVE_QTPDF
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#endif

#include "QtHelpers.h"

namespace {

QToolButton* toolButton(const QString& text, const QString& tip)
{
    auto* b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);  // keep keyboard focus in the editor
    return b;
}

}  // namespace

bool PdfPreview::isAvailable()
{
#ifdef FINRENAMER_HAVE_QTPDF
    return true;
#else
    return false;
#endif
}

PdfPreview::PdfPreview(QWidget* parent) : QWidget(parent)
{
    // ---- Toolbar ----
    prevBtn_ = toolButton(QStringLiteral("◀"), tr("Previous page"));
    nextBtn_ = toolButton(QStringLiteral("▶"), tr("Next page"));
    pageLabel_ = new QLabel;
    fitWidthBtn_ = toolButton(tr("Fit Width"), tr("Zoom so the page fills the width"));
    fitPageBtn_ = toolButton(tr("Whole Page"), tr("Zoom so a whole page is visible"));
    fitWidthBtn_->setCheckable(true);
    fitPageBtn_->setCheckable(true);
    auto* zoomOut = toolButton(QStringLiteral("−"), tr("Zoom out"));
    auto* zoomIn = toolButton(QStringLiteral("+"), tr("Zoom in"));

    toolbar_ = new QWidget;
    auto* bar = new QHBoxLayout(toolbar_);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(prevBtn_);
    bar->addWidget(pageLabel_);
    bar->addWidget(nextBtn_);
    bar->addStretch();
    bar->addWidget(fitWidthBtn_);
    bar->addWidget(fitPageBtn_);
    bar->addWidget(zoomOut);
    bar->addWidget(zoomIn);

    // ---- Body: message or document ----
    message_ = new QLabel;
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setStyleSheet("color: palette(placeholder-text); padding: 24px;");

    stack_ = new QStackedWidget;
    stack_->addWidget(message_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(toolbar_);
    layout->addWidget(stack_, 1);

#ifdef FINRENAMER_HAVE_QTPDF
    document_ = new QPdfDocument(this);
    view_ = new QPdfView;
    view_->setDocument(document_);
    view_->setPageMode(QPdfView::PageMode::MultiPage);  // scroll through all pages
    view_->setFocusPolicy(Qt::NoFocus);
    stack_->addWidget(view_);

    QSettings settings(ui::settingsFile(), QSettings::IniFormat);
    setFit(settings.value("preview/fitWidth", true).toBool());

    connect(view_->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this,
            &PdfPreview::updateControls);
    connect(document_, &QPdfDocument::pageCountChanged, this, &PdfPreview::updateControls);
    // Loading can finish after load() returns, so react to status changes.
    connect(document_, &QPdfDocument::statusChanged, this, &PdfPreview::onDocumentStatus);
    connect(prevBtn_, &QToolButton::clicked, this, [this] { goToPage(-1); });
    connect(nextBtn_, &QToolButton::clicked, this, [this] { goToPage(+1); });
    connect(fitWidthBtn_, &QToolButton::clicked, this, [this] { setFit(true); });
    connect(fitPageBtn_, &QToolButton::clicked, this, [this] { setFit(false); });
    connect(zoomIn, &QToolButton::clicked, this, [this] { zoomBy(1.25); });
    connect(zoomOut, &QToolButton::clicked, this, [this] { zoomBy(0.8); });

    showMessage(tr("Select a file to preview it here."));
#else
    toolbar_->hide();
    showMessage(tr("The PDF preview isn't included in this build because the Qt PDF module "
                   "wasn't found.\n\nInstall it with the Qt Maintenance Tool (your Qt version > "
                   "Additional Libraries > Qt PDF), then rebuild. Until then, use Open PDF."));
#endif
}

PdfPreview::~PdfPreview()
{
    ui::disconnectChildren(this);
#ifdef FINRENAMER_HAVE_QTPDF
    // Delete these now rather than with the other children: the document reads
    // from buffer_, which is destroyed before the children are.
    delete view_;
    delete document_;
#endif
}

void PdfPreview::showFile(const QString& path)
{
    if (path == file_) return;
    clear();
    if (path.isEmpty()) return;
    file_ = path;

#ifdef FINRENAMER_HAVE_QTPDF
    // Read the whole file now and let go of it, so it can still be renamed.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showMessage(tr("Couldn't open this file:\n%1").arg(file.errorString()));
        return;
    }
    bytes_ = file.readAll();
    file.close();

    buffer_.setData(bytes_);
    buffer_.open(QIODevice::ReadOnly);
    showMessage(tr("Loading..."));
    document_->load(&buffer_);
    onDocumentStatus();  // in case it finished loading already
#endif
}

void PdfPreview::onDocumentStatus()
{
#ifdef FINRENAMER_HAVE_QTPDF
    if (file_.isEmpty()) return;

    if (document_->status() == QPdfDocument::Status::Ready) {
        if (stack_->currentWidget() != view_) {
            stack_->setCurrentWidget(view_);
            view_->pageNavigator()->jump(0, QPointF(), view_->pageNavigator()->currentZoom());
        }
        updateControls();
    } else if (document_->status() == QPdfDocument::Status::Error) {
        switch (document_->error()) {
        case QPdfDocument::Error::IncorrectPassword:
        case QPdfDocument::Error::UnsupportedSecurityScheme:
            showMessage(tr("This PDF is password-protected, so it can't be previewed here.\n\n"
                           "Use Open PDF to view it in your PDF reader."));
            break;
        default:
            showMessage(tr("This file couldn't be read as a PDF. It may be damaged.\n\n"
                           "Try Open PDF to view it in your PDF reader."));
            break;
        }
    }
#endif
}

void PdfPreview::clear()
{
    file_.clear();
#ifdef FINRENAMER_HAVE_QTPDF
    document_->close();
    showMessage(tr("Select a file to preview it here."));
#endif
    buffer_.close();
    buffer_.setData(QByteArray());
    bytes_.clear();
    updateControls();
}

void PdfPreview::showMessage(const QString& text)
{
    message_->setText(text);
    stack_->setCurrentWidget(message_);
    updateControls();
}

void PdfPreview::updateControls()
{
#ifdef FINRENAMER_HAVE_QTPDF
    const bool showing = stack_->currentWidget() == view_;
    const int pages = showing ? document_->pageCount() : 0;
    const int page = showing ? view_->pageNavigator()->currentPage() : 0;

    pageLabel_->setText(pages > 0 ? tr("Page %1 of %2").arg(page + 1).arg(pages) : QString());
    prevBtn_->setEnabled(pages > 0 && page > 0);
    nextBtn_->setEnabled(pages > 0 && page < pages - 1);
    for (QToolButton* b : {fitWidthBtn_, fitPageBtn_}) b->setEnabled(showing);
#endif
}

void PdfPreview::goToPage(int delta)
{
#ifdef FINRENAMER_HAVE_QTPDF
    QPdfPageNavigator* nav = view_->pageNavigator();
    const int target = nav->currentPage() + delta;
    if (target < 0 || target >= document_->pageCount()) return;
    nav->jump(target, QPointF(), nav->currentZoom());
#else
    Q_UNUSED(delta);
#endif
}

void PdfPreview::setFit(bool fitWidth)
{
#ifdef FINRENAMER_HAVE_QTPDF
    view_->setZoomMode(fitWidth ? QPdfView::ZoomMode::FitToWidth : QPdfView::ZoomMode::FitInView);
    fitWidthBtn_->setChecked(fitWidth);
    fitPageBtn_->setChecked(!fitWidth);

    QSettings settings(ui::settingsFile(), QSettings::IniFormat);
    settings.setValue("preview/fitWidth", fitWidth);
#else
    Q_UNUSED(fitWidth);
#endif
}

void PdfPreview::zoomBy(double factor)
{
#ifdef FINRENAMER_HAVE_QTPDF
    view_->setZoomMode(QPdfView::ZoomMode::Custom);
    view_->setZoomFactor(qBound(0.25, view_->zoomFactor() * factor, 5.0));
    fitWidthBtn_->setChecked(false);
    fitPageBtn_->setChecked(false);
#else
    Q_UNUSED(factor);
#endif
}
