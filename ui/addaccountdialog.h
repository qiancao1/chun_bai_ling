#ifndef ADDACCOUNTDIALOG_H
#define ADDACCOUNTDIALOG_H

#include <QDialog>
#include <QRadioButton>
#include "accountinfo.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class QTextEdit;
class QGroupBox;
class QStackedWidget;
class QListWidget;

class AddAccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddAccountDialog(const AccountInfo &info = AccountInfo(), QWidget *parent = nullptr);

    AccountInfo getAccountInfo() const;
    void setEmbeddedMode(bool embedded);


private:
    void setupUI();
    void setupWsIntentsGroup();
    int computeIntentsMask() const;
    void setIntentsMask(int mask);
    QStringList getDisabledPlugins() const;
    void setDisabledPlugins(const QStringList &disabled);

    // 基础信息控件
    QLineEdit *m_appidEdit;
    QLineEdit *m_secretEdit;
    QLineEdit *m_botqqEdit;
    QLineEdit *m_wsAddressEdit;

    QLineEdit *m_botsettextEdit;
    QRadioButton *m_wsRadio;
    QRadioButton *m_webhookRadio;


    // 回复设置控件
    //QTextEdit *m_welcomeEdit;
    //QTextEdit *m_fallbackEdit;

    QCheckBox* m_arkCheckBox;
    QCheckBox* m_markdownCheckBox;
    // 动态配置区域
    QStackedWidget *m_stackedConfig;
    QWidget *m_wsConfigWidget;
    QWidget *m_webhookConfigWidget;

    // WS 特有
    QGroupBox *m_wsIntentsGroup;
    QList<QCheckBox*> m_intentCheckboxes;



};

#endif // ADDACCOUNTDIALOG_H