/********************************************************************************
** Form generated from reading UI file 'widget.ui'
**
** Created by: Qt User Interface Compiler version 5.15.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_WIDGET_H
#define UI_WIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_Widget
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *videoLabel;
    QPushButton *cameraButton;

    void setupUi(QWidget *Widget)
    {
        if (Widget->objectName().isEmpty())
            Widget->setObjectName(QString::fromUtf8("Widget"));
        Widget->resize(840, 700);
        verticalLayout = new QVBoxLayout(Widget);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        videoLabel = new QLabel(Widget);
        videoLabel->setObjectName(QString::fromUtf8("videoLabel"));
        videoLabel->setMinimumSize(QSize(800, 600));
        videoLabel->setMaximumSize(QSize(800, 600));
        videoLabel->setAlignment(Qt::AlignCenter);
        videoLabel->setStyleSheet(QString::fromUtf8("background-color: #2d2d2d; color: #888888; border: 2px solid #555555; font-size: 18px;"));

        verticalLayout->addWidget(videoLabel);

        cameraButton = new QPushButton(Widget);
        cameraButton->setObjectName(QString::fromUtf8("cameraButton"));
        cameraButton->setMinimumSize(QSize(200, 45));
        cameraButton->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"    background-color: #0078d4;\n"
"    color: white;\n"
"    border: none;\n"
"    border-radius: 6px;\n"
"    font-size: 16px;\n"
"    padding: 8px 24px;\n"
"}\n"
"QPushButton:hover {\n"
"    background-color: #1a8ae8;\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #005a9e;\n"
"}"));

        verticalLayout->addWidget(cameraButton);


        retranslateUi(Widget);

        QMetaObject::connectSlotsByName(Widget);
    } // setupUi

    void retranslateUi(QWidget *Widget)
    {
        Widget->setWindowTitle(QCoreApplication::translate("Widget", "RK3568 \346\221\204\345\203\217\345\244\264\351\207\207\351\233\206", nullptr));
        videoLabel->setText(QCoreApplication::translate("Widget", "\346\221\204\345\203\217\345\244\264\346\234\252\346\211\223\345\274\200", nullptr));
        cameraButton->setText(QCoreApplication::translate("Widget", "\346\211\223\345\274\200\346\221\204\345\203\217\345\244\264", nullptr));
    } // retranslateUi

};

namespace Ui {
    class Widget: public Ui_Widget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_WIDGET_H
