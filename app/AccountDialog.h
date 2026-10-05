#pragma once

#include <QDialog>

#include <QHash>

#include <cstdint>
#include <optional>

#include "finrenamer/Database.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// New/edit dialog for an account. Saves to the database itself when OK is
// pressed, so a rule violation is shown in the dialog without closing it.
class AccountDialog : public QDialog {
    Q_OBJECT
public:
    AccountDialog(finrenamer::Database& db, std::int64_t caseId,
                  std::optional<finrenamer::AccountRecord> existing, QWidget* parent = nullptr);

    std::int64_t savedAccountId() const { return savedId_; }

    // True if "New Person..." added someone, so the caller should refresh its list.
    bool addedPeople() const { return addedPeople_; }

public slots:
    void accept() override;

private:
    void reloadPeopleChoices();
    void addSelectedPerson();
    void addNewPerson();
    void removeOwner();
    void moveOwner(int delta);
    void updateState();
    void autofillInstitutionDisplay();
    finrenamer::Account currentAccount() const;

    finrenamer::Database& db_;
    std::int64_t caseId_;
    std::optional<finrenamer::AccountRecord> existing_;
    std::int64_t savedId_ = 0;
    bool addedPeople_ = false;

    QLineEdit* institution_ = nullptr;
    QLineEdit* institutionDisplay_ = nullptr;
    QHash<QString, QString> knownDisplayNames_;  // lowercased institution -> last display name used
    bool displayEditedByUser_ = false;           // stop autofilling once the user types their own
    QLineEdit* type_ = nullptr;
    QLineEdit* lastFour_ = nullptr;
    QListWidget* owners_ = nullptr;
    QComboBox* peopleChoice_ = nullptr;
    QPushButton* addOwnerBtn_ = nullptr;
    QPushButton* removeOwnerBtn_ = nullptr;
    QPushButton* upBtn_ = nullptr;
    QPushButton* downBtn_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* error_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
