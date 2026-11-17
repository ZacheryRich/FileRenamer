#pragma once

#include <QDialog>

#include <string>
#include <utility>
#include <vector>

#include "finrenamer/Models.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;

// Editors for a case's parties, children and other people. Every field is optional (user's rule).
// They only collect input; the caller saves.

// A party: name, Petitioner/Respondent, "Our client", and the details the information sheet asks for.
class PartyDialog : public QDialog {
    Q_OBJECT
public:
    PartyDialog(const finrenamer::CaseParty& initial, QWidget* parent = nullptr);
    finrenamer::CaseParty result() const;

private:
    finrenamer::CaseParty party_;
    QComboBox* role_ = nullptr;
    QCheckBox* client_ = nullptr;
    std::vector<std::pair<QLineEdit*, std::string finrenamer::CaseParty::*>> edits_;
};

class ChildDialog : public QDialog {
    Q_OBJECT
public:
    ChildDialog(const finrenamer::CaseChild& initial, QWidget* parent = nullptr);
    finrenamer::CaseChild result() const;

private:
    finrenamer::CaseChild child_;
    std::vector<std::pair<QLineEdit*, std::string finrenamer::CaseChild::*>> edits_;
};

// Anyone else on the case: prior counsel, a third party, an expert...
class ContactDialog : public QDialog {
    Q_OBJECT
public:
    ContactDialog(const finrenamer::CaseContact& initial, QWidget* parent = nullptr);
    finrenamer::CaseContact result() const;

private:
    finrenamer::CaseContact contact_;
    QComboBox* role_ = nullptr;
    QPlainTextEdit* address_ = nullptr;
    std::vector<std::pair<QLineEdit*, std::string finrenamer::CaseContact::*>> edits_;
};
