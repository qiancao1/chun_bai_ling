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
#include "placeholderlineedit.h"
#include "placeholdertextedit.h"

#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit



class set : public QWidget
{
    Q_OBJECT
public:
    explicit set(QWidget *parent = nullptr);
    ~set();

    bool 远程服务器=false;
    QCheckBox *m_cnb,*m_cos;
    QString 远程端口=0;
    void set_webui(bool value);

private slots:
    bool onStartStopClicked(int port);



private:
    void setupUI();
    void loadConfig();

    QLineEdit  *m_ffmpegpath,*m_日志数量,*m_日志颜色,*webws_port,*webhook,*m_xcs;
    QPushButton  *web_qr;
    QPushButton  *webhook_but,*m_loadbut,*m_test_api;
    QLineEdit    *m_addrEdit,*m_loadport;
    QTextEdit    *m_admid_deit;
    QPushButton  *m_admin_qr;
    QCheckBox *Ewebhook,*Ews,*ESSL,*Eimg,*loadimg;

    QLineEdit    *m_cnb_repo,*m_cnb_token;
    QPushButton *m_cnb_qr,*m_cnb_cs;

    QLineEdit    *m_cos_secretId,*m_cos_secretKey,*m_cos_host;
    QPushButton *m_cos_qr,*m_cos_cs;
    QPushButton  *m_startStopBtn,*lts_but; //聊天室
};

#endif // SET_H