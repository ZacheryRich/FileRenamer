#pragma once

#include <QDialog>

// "Licences..." on the start screen: shows THIRD-PARTY-NOTICES.txt (compiled in as a resource).
class NoticesDialog : public QDialog {
    Q_OBJECT
public:
    explicit NoticesDialog(QWidget* parent = nullptr);
    QString text() const;  // also used by tests
};
