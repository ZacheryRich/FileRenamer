#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// "Bates Log": two jobs on one spreadsheet (no case needed).
//   1. List Files -- writes the file names of a folder and all its subfolders into
//      the spreadsheet (a new one, or added after what is already there).
//   2. Add Bates Numbers -- reads Adobe Acrobat's Bates log and fills Begin / End
//      Bates (columns B and C) for the files listed.
// The paths are remembered in settings.ini under "bates/...".
class BatesDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatesDialog(QWidget* parent = nullptr);
    ~BatesDialog() override;

    // Also used by tests. Both return true if the work was done.
    void setWorkbook(const QString& path);
    void setFolder(const QString& path);
    void setLogFile(const QString& path);
    bool listFiles();
    bool addBatesNumbers();
    QString resultText() const;

private:
    void browseWorkbook();
    void browseFolder();
    void browseLog();
    void openWorkbook();
    void updateButtons();
    void report(const QString& text, bool error = false);
    QString workbookPath() const;

    QLineEdit* workbook_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QLineEdit* log_ = nullptr;
    QPlainTextEdit* results_ = nullptr;
    QPushButton* listBtn_ = nullptr;
    QPushButton* batesBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
};
