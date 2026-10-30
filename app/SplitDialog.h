#pragma once

#include <QDialog>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "finrenamer/PdfSplit.h"

class PdfPreview;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// "PDF Splitter" (no case needed): page through one PDF, mark the pages where a new file starts
// (for example each month of a year of statements saved as one big PDF) and save the pieces as
// separate PDFs. The original is never changed and nothing is overwritten. The pieces are named
// "<name> Part 01" ... by default; each name can be edited. They go to a folder the user picks
// (the original's folder to begin with). Page 1 always starts the first file.
class SplitDialog : public QDialog {
    Q_OBJECT
public:
    explicit SplitDialog(QWidget* parent = nullptr);
    ~SplitDialog() override;

    // Also used by tests. loadPdf never asks questions; Browse asks for a password if needed.
    bool loadPdf(const QString& path, const QString& password = QString());
    int pageCount() const { return pageCount_; }
    void setStartsNewFile(int page, bool on);  // 1-based; page 1 can't be changed
    void setStartsEvery(int step);             // replaces all marks
    std::set<int> starts() const { return starts_; }
    std::vector<finrenamer::SplitPart> parts() const;
    void setPartName(int index, const QString& name);  // 0-based
    void setOutputFolder(const QString& folder);
    std::vector<finrenamer::SplitPartResult> splitNow();
    QString summaryText() const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;  // Space marks the selected page

private:
    void buildUi();
    void browsePdf();
    void browseOutput();
    void openOutput();
    void toggleCurrent();
    void askEveryN();
    void clearMarks();
    void refreshPages();
    void refreshParts();
    void updateButtons();
    void report(const QString& text, bool error = false);
    int currentPage() const;  // 1-based, 0 if none
    QString defaultName(int index, int total) const;

    std::filesystem::path source_;
    std::string password_;
    int pageCount_ = 0;
    std::set<int> starts_{1};
    std::map<int, QString> customNames_;  // first page of a file -> the name the user typed
    bool dirty_ = false;                  // something changed since the last split
    bool refreshing_ = false;

    QLineEdit* pdfFile_ = nullptr;
    QTableWidget* pages_ = nullptr;
    QTableWidget* partsTable_ = nullptr;
    PdfPreview* preview_ = nullptr;
    QPushButton* startHereBtn_ = nullptr;
    QPushButton* everyBtn_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
    QLineEdit* output_ = nullptr;
    QPushButton* splitBtn_ = nullptr;
    QPushButton* openOutputBtn_ = nullptr;
    QLabel* summary_ = nullptr;
};
