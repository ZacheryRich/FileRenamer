#pragma once

#include <QDialog>

#include <map>

#include <QHash>

#include <cstdint>
#include <optional>

#include "finrenamer/Database.h"

class DateField;
class QComboBox;
class QGroupBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

// New/edit dialog for an account or a combined statement (one PDF covering
// several accounts at the same institution: one institution plus a typed list of
// account types and numbers; chosen in the "Statement" dropdown, which is fixed once saved). Saves to the database itself when OK is
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
    finrenamer::Account currentAccount() const;
    bool isCombined() const;
    void updateKind();

    // Combined statement: a typed list of (type, number), in name order.
    void addPart(const QString& type = {}, const QString& number = {},
                 const std::vector<std::int64_t>& owners = {});
    void loadPeople();
    void setPartOwners(int row, const std::vector<std::int64_t>& ids);
    std::vector<std::int64_t> partOwners(int row) const;
    void editPartOwners(int row);
    void removePart();
    void movePart(int delta);
    std::vector<finrenamer::CombinedPart> parts() const;

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
    QTableWidget* parts_ = nullptr;  // columns: Account type, Number, Owners
    QPushButton* removePartBtn_ = nullptr;
    QPushButton* partOwnersBtn_ = nullptr;
    QGroupBox* ownersBox_ = nullptr;
    std::map<qlonglong, finrenamer::Person> people_;  // the case's people, by id
    QPushButton* partUpBtn_ = nullptr;
    QPushButton* partDownBtn_ = nullptr;

    QLineEdit* institution_ = nullptr;
    QLineEdit* institutionDisplay_ = nullptr;
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
