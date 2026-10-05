#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;

#ifdef FINRENAMER_HAVE_QTPDF
class QPdfDocument;
class QPdfView;
#endif

// Shows a PDF inside the app. The file is read into memory and the file
// itself is closed straight away, so the preview never stops Windows from
// renaming or moving the file.
//
// Built with Qt PDF when CMake finds it (FINRENAMER_HAVE_QTPDF); otherwise the
// pane just says the preview isn't available.
class PdfPreview : public QWidget {
    Q_OBJECT
public:
    explicit PdfPreview(QWidget* parent = nullptr);
    ~PdfPreview() override;

    static bool isAvailable();

    // Shows `path` (does nothing if it's already showing). Empty clears the pane.
    void showFile(const QString& path);
    void clear();

    QString currentFile() const { return file_; }

private:
    void showMessage(const QString& text);
    void onDocumentStatus();
    void updateControls();
    void goToPage(int delta);
    void setFit(bool fitWidth);
    void zoomBy(double factor);

    QString file_;
    QByteArray bytes_;
    QBuffer buffer_;

    QStackedWidget* stack_ = nullptr;
    QLabel* message_ = nullptr;
    QWidget* toolbar_ = nullptr;
    QToolButton* prevBtn_ = nullptr;
    QToolButton* nextBtn_ = nullptr;
    QLabel* pageLabel_ = nullptr;
    QToolButton* fitWidthBtn_ = nullptr;
    QToolButton* fitPageBtn_ = nullptr;

#ifdef FINRENAMER_HAVE_QTPDF
    QPdfDocument* document_ = nullptr;
    QPdfView* view_ = nullptr;
#endif
};
