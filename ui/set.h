#ifndef SET_H
#define SET_H

#include <QWidget>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <qcheckbox.h>
#include <qtablewidget.h>
#include "PlaceholderLineEdit.h"
#include "PlaceholderTextEdit.h"

#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit



class set : public QWidget
{
    Q_OBJECT
public:
    explicit set(QWidget *parent = nullptr);
    ~set();
    void incrementTokenUsage(const QString &token);
    bool 远程服务器=false;
    QString 远程链接,远程token;

private slots:
    void onStartStopClicked();
    void onModeToggled(bool checked);


    void onAddTokenRow();
    void onDeleteTokenRow();
    void onWhitelistChanged();   // 保存按钮槽
    void onTokenItemChanged(QTableWidgetItem *item); // 监听 Token 列编辑完成


private:
    void setupUI();
    void loadConfig();
    void saveModeConfig();
    void saveRemoteConfig();

    void saveAutoStartFlag(bool autoStart);
    void updateControlEnable();
    void stopLocalServerIfRunning();



    // UI 控件
    QRadioButton *m_remoteRadio;
    QRadioButton *m_localRadio;
    QLineEdit    *m_urlEdit , *m_ffmpegpath,*m_日志数量,*m_日志颜色,*webws_port,*webhook,*webhook_ssl;
    QLineEdit    *m_token;
    QPushButton  *web_qr;
    QPushButton  *m_confirmBtn,*admin_but,*python3_14t_but,*webhook_but,*webhook_ssl_but;
    QLineEdit    *m_addrEdit,*admin_Edit,*python3_14t;
    QCheckBox *Ewebhook,*Ews,*ESSL;
    QPushButton  *m_startStopBtn;
    QTableWidget *m_tokenTable;
    QPushButton *m_addTokenBtn;
    QPushButton *m_delTokenBtn;
    QPushButton *m_saveTokenBtn;   // 保存按钮
    bool m_serverRunning;   // 当前是否正在运行
};

#endif // SET_H