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

    void getAccountInfo(AccountInfo *info) const;
    // 把界面上的数据重新载入表单（嵌入模式复用同一个实例时使用）
    void setAccountInfo(const AccountInfo &info);
    // 嵌入到 AccountPage 右侧：去掉窗口属性、隐藏底部的 确定/取消 按钮条
    void setEmbeddedMode(bool embedded);
    // 让 AppID 输入框获得焦点
    void focusAppId();

protected:
    // 监听 Secret 输入框的 获得/失去 焦点，切换「明文 / ***」
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUI();
    // Secret 框显示刷新：聚焦显示明文，失焦显示 ***
    void refreshSecretDisplay();
    void setupWsIntentsGroup();
    int computeIntentsMask() const;
    void setIntentsMask(int mask);
    QStringList getDisabledPlugins() const;
    void setDisabledPlugins(const QStringList &disabled);

    // 基础信息控件
    QLineEdit *m_appidEdit;
    QLineEdit *m_secretEdit;
    // Secret 的真实值（输入框失焦时只显示 ***，真实值存在这里）
    QString m_secretReal;
    bool m_secretMasked = false;



    QRadioButton *m_wsRadio;
    QRadioButton *m_webhookRadio;


    // 回复设置控件
    //QTextEdit *m_welcomeEdit;
    //QTextEdit *m_fallbackEdit;


    QCheckBox* m_markdownCheckBox;
    QCheckBox* m_markdownCheckBox_pd,*m_markdownCheckBox_pd_mb;
    QCheckBox* m_sandboxCheckBox = nullptr;   // 连接沙盒（状态存到 AccountInfo::sandbox）
    // 动态配置区域
    QStackedWidget *m_stackedConfig;

    QWidget *m_webhookConfigWidget;
    QWidget *m_buttonBar = nullptr;      // 底部按钮条（嵌入模式隐藏）

    // WS 特有
    QGroupBox *m_wsIntentsGroup;
    QList<QCheckBox*> m_intentCheckboxes;



};

#endif // ADDACCOUNTDIALOG_H