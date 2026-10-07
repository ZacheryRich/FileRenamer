#pragma once

#include <QDialog>

#include <QHash>

#include <cstdint>
#include <optional>

#include "finrenamer/Database.h"

class DateField;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// New/edit dialog for an account or a combined statement (one PDF covering
// several accounts at the same institution; chosen in the "Statement" dropdown,
// which is fixed once saved). Saves to the database itself when OK is
// pressed, so a rule violation is shown in the dialog without closing it.
class AccountDialog : public QDialog {
    Q_OBJECT
public:
    AccountDialog(finrenamer::Database& db, std::int64_t caseId,
                  std::optional<finrenamer::AccountRecord> existing, QWidget* parent = nullptr);
    ~AccountDialog() override;

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
    bool isCombined() const;
    void updateKind();

    // Combined statement: its accounts, in name order.
    void reloadMemberChoices();
    void addMember();
    void removeMember();
    void moveMember(int delta);
    const finrenamer::Account* singleAccount(std::int64_t id) const;

    // Account numbers list: newest first, the top one is current.
    std::vector<std::string> numbers() const;
    finrenamer::Database::NumberCorrections numberCorrections() const;
    void addNumber();
    void removeNumber();
    void moveNumber(int delta);
    void styleNumbers();

    finrenamer::Database& db_;
    std::int64_t caseId_;
    std::optional<finrenamer::AccountRecord> existing_;
    std::int64_t savedId_ = 0;
    bool addedPeople_ = false;

    QComboBox* kind_ = nullptr;
    QWidget* singlePage_ = nullptr;
    QWidget* combinedPage_ = nullptr;
    std::vector<finrenamer::Account> singles_;  // this case's single accounts
    QListWidget* members_ = nullptr;
    QComboBox* memberChoice_ = nullptr;
    QPushButton* addMemberBtn_ = nullptr;
    QPushButton* removeMemberBtn_ = nullptr;
    QPushButton* memberUpBtn_ = nullptr;
    QPushButton* memberDownBtn_ = nullptr;

    QLineEdit* institution_ = nullptr;
    QLineEdit* institutionDisplay_ = nullptr;
    QHash<QString, QString> knownDisplayNames_;  // lowercased institution -> last display name used
    bool displayEditedByUser_ = false;           // stop autofilling once the user types their own
    QLineEdit* type_ = nullptr;
    DateField* openedOn_ = nullptr;   // optional
    DateField* closedOn_ = nullptr;   // optional
    QListWidget* numbers_ = nullptr;  // each item remembers its saved text, to spot corrections
    QPushButton* removeNumberBtn_ = nullptr;
    QPushButton* numberUpBtn_ = nullptr;
    QPushButton* numberDownBtn_ = nullptr;
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
