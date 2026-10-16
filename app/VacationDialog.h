#pragma once

#include <QDialog>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "finrenamer/VacationSheet.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTableWidget;

// "Vacation Letters" (no case needed): the firm's case list is an Excel sheet (the "ALL CASES"
// tab of a shared workbook) and the letter is a Word template in the same shared place.
//   - Cases are listed from the sheet (filter by attorney, paralegal, county or text).
//   - A case can be edited here; Save writes just that row back into the workbook.
//   - Tick the cases that need a letter and Create Letters merges them into the template and
//     saves one combined Word document wherever you choose.
// The workbook and template are read fresh each time they are used; nothing is copied.
// Paths are remembered in settings.ini under "vacation/...".
class VacationDialog : public QDialog {
    Q_OBJECT
public:
    explicit VacationDialog(QWidget* parent = nullptr);
    ~VacationDialog() override;

    // Also used by tests.
    void setWorkbook(const QString& path);  // loads the cases
    void setTemplate(const QString& path);
    bool reload();                          // re-reads the workbook; false if it couldn't be read
    bool saveCase();                        // writes the form to the sheet
    bool createLetters(const QString& outputPath);  // merges the ticked cases; false if nothing was made
    std::vector<std::size_t> checkedRows() const;   // sheet row numbers
    void setChecked(const std::set<std::size_t>& rows);
    QString statusText() const;

private:
    void buildUi();
    void browseWorkbook();
    void browseTemplate();
    void chooseOutputAndCreate();
    void manageSignatures();
    void populateFilters();
    void populateTable();
    void applyFilters();
    bool rowMatches(const finrenamer::VacationRow& row) const;
    void rebuildForm();
    void loadForm(std::optional<std::size_t> sheetRow);  // nullopt or 0: a blank new case
    void markDirty();
    void setDirty(bool dirty);
    bool resolveUnsaved();  // asks Save / Discard / Cancel; false = cancelled
    void rowChosen();
    void updateButtons();
    void updateCount();
    void report(const QString& text);
    void reportError(const QString& text);
    bool guard(const std::function<void()>& work, const QString& what);
    const finrenamer::VacationRow* findRow(std::size_t sheetRow) const;
    QString templatePath() const;
    QString workbookPath() const;
    int tableRowFor(std::size_t sheetRow) const;
    std::vector<std::string> formValues() const;

    QLineEdit* workbook_ = nullptr;
    QLineEdit* template_ = nullptr;
    QPushButton* reloadBtn_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* attorney_ = nullptr;
    QComboBox* paralegal_ = nullptr;
    QComboBox* county_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* count_ = nullptr;
    QPushButton* selectShown_ = nullptr;
    QPushButton* clearSelection_ = nullptr;

    QScrollArea* formArea_ = nullptr;
    QWidget* formHost_ = nullptr;
    QFormLayout* form_ = nullptr;
    std::vector<QPlainTextEdit*> fields_;
    QLabel* formTitle_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
    QPushButton* revertBtn_ = nullptr;
    QPushButton* newBtn_ = nullptr;

    QLabel* status_ = nullptr;
    QPushButton* createBtn_ = nullptr;

    finrenamer::VacationSheet sheet_;
    bool haveSheet_ = false;
    std::set<std::size_t> checked_;                       // sheet rows ticked for a letter
    std::map<std::size_t, std::vector<std::string>> seen_;  // what each ticked row held when it was ticked
    std::optional<std::size_t> editing_;                  // sheet row in the form (0 = new case)
    std::vector<std::string> formLoaded_;                 // what the form was filled with
    bool loadingForm_ = false;
    bool populating_ = false;
    bool dirty_ = false;
    std::string sheetPathLoaded_;  // the workbook path the list was read from
};
