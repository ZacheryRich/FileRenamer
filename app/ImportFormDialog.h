#pragma once

#include <QDialog>

#include <cstdint>
#include <vector>

#include "finrenamer/Database.h"
#include "finrenamer/InfoForm.h"

class QLabel;
class QRadioButton;
class QTreeWidget;

// "New from Form..." / "Fill from Form...": reads a filled-in client Information Form (fillable
// PDF) and shows, field by field, what it would put into the case, each with a tick box. Values
// that would replace something different already in the case start unticked. Everything the
// case has no place for goes into the case's notes. Import saves the ticked ones (creating the
// case first when `caseId` is 0).
class ImportFormDialog : public QDialog {
    Q_OBJECT
public:
    ImportFormDialog(finrenamer::Database& db, std::int64_t caseId, const QString& pdf, QWidget* parent = nullptr);
    ~ImportFormDialog() override;

    bool loaded() const { return loaded_; }       // false: the PDF couldn't be read, or isn't the form (already told)
    std::int64_t savedCaseId() const { return caseId_; }
    bool importNow();                              // also used by tests; true when saved

private:
    void fillTree();
    void setAll(bool on);

    finrenamer::Database& db_;
    std::int64_t caseId_;
    bool loaded_ = false;
    finrenamer::InfoFormData form_;
    finrenamer::CaseFile file_;
    std::vector<finrenamer::FormChange> changes_;

    QTreeWidget* tree_ = nullptr;
    QRadioButton* petitioner_ = nullptr;
    QRadioButton* respondent_ = nullptr;
    QLabel* summary_ = nullptr;
};
