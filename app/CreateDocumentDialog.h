#pragma once

#include <QDialog>

#include <cstdint>

#include "finrenamer/CaseDocument.h"
#include "finrenamer/Database.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// Loads everything about a case for a document (parties, children, other people, attorneys with
// their firms). Throws like the Database calls.
finrenamer::CaseFile loadCaseFile(finrenamer::Database& db, std::int64_t caseId);

// "Create Document..." on the Case List: fills a Word template (merge fields, Begin_/End_ blocks;
// see WordTemplate.h and caseFieldReference) with the case's information and saves the result
// wherever the user picks. The template is only read. Recently used templates are remembered
// (settings "caseDocuments/templates"), as is the signer per case and the save folder.
class CreateDocumentDialog : public QDialog {
    Q_OBJECT
public:
    CreateDocumentDialog(finrenamer::Database& db, std::int64_t caseId, QWidget* parent = nullptr);
    ~CreateDocumentDialog() override;

    // Also used by tests: fills the chosen template into `output` without asking where to save.
    bool createTo(const QString& output);
    void setTemplate(const QString& file);   // adds it to the list and chooses it
    QString templateFile() const;

private:
    void browseTemplate();
    void templateChanged();
    void showFieldNames();
    void create();
    void fillTemplateList(const QString& select);
    void rememberTemplate(const QString& file);

    finrenamer::Database& db_;
    std::int64_t caseId_;
    finrenamer::CaseFile file_;

    QComboBox* template_ = nullptr;
    QComboBox* signer_ = nullptr;
    QLabel* check_ = nullptr;
    QCheckBox* openAfter_ = nullptr;
    QPushButton* createBtn_ = nullptr;
    QLabel* status_ = nullptr;
};
