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

    bool 远程服务器=false;

    QString 远程端口=0;
    void set_webui(bool value);

private slots:
    bool onStartStopClicked(int port);



private:
    void setupUI();
    void loadConfig();

    QLineEdit  *m_ffmpegpath,*m_日志数量,*m_日志颜色,*webws_port,*webhook;
    QPushButton  *web_qr;
    QPushButton  *webhook_but,*m_loadbut;
    QLineEdit    *m_addrEdit,*m_loadport;
    QTextEdit    *m_admid_deit;
    QPushButton  *m_admin_qr;
    QCheckBox *Ewebhook,*Ews,*ESSL,*Eimg,*loadimg;
    QPushButton  *m_startStopBtn,*lts_but; //聊天室
};

#endif // SET_H