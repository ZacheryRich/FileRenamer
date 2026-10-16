#include "NoticesDialog.h"

#include <QFile>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

NoticesDialog::NoticesDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Licences and Third-Party Notices"));
    resize(760, 620);
    auto* view = new QPlainTextEdit;
    view->setObjectName("notices");
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = view->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily("Consolas");
    view->setFont(mono);
    view->setPlainText(text());

    auto* close = new QPushButton(tr("Close"));
    close->setObjectName("close");
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* footer = new QHBoxLayout;
    footer->addStretch();
    footer->addWidget(close);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(view, 1);
    layout->addLayout(footer);
}

QString NoticesDialog::text() const
{
    QFile file(":/THIRD-PARTY-NOTICES.txt");
    if (!file.open(QIODevice::ReadOnly))
        return tr("The notices file could not be read. See THIRD-PARTY-NOTICES.txt in the program folder.");
    return QString::fromUtf8(file.readAll());
}
