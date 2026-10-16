#pragma once

#include <QDialog>

#include "finrenamer/Database.h"

class QPushButton;
class QTableWidget;

// "Templates..." on the rename screen: the file name patterns (Email, Text, Letter, Court Order...)
// offered in the Type list. Add, change, copy and delete; every change is saved at once.
class TemplatesDialog : public QDialog {
    Q_OBJECT
public:
    explicit TemplatesDialog(finrenamer::Database& db, QWidget* parent = nullptr);
    ~TemplatesDialog() override;

private:
    void reload(std::int64_t select = 0);
    void addTemplate();
    void editTemplate();
    void duplicateTemplate();
    void deleteTemplate();
    void addExamples();
    void exportTemplates();
    void importTemplates();
    void updateButtons();
    std::optional<finrenamer::DocumentTemplate> current() const;

    finrenamer::Database& db_;
    std::vector<finrenamer::DocumentTemplate> templates_;
    QTableWidget* table_ = nullptr;
    QPushButton* editBtn_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
};
