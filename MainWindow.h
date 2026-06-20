#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QTimer>
#include <QLabel>
#include <QToolButton>
#include <QString>
#include <qnetworkaccessmanager.h>
#include "ui_ai.h"
#include "ui_plts.h"

class HomePage;
class AccountPage;
class LogPage;
class PluginPage;
class ChatPage;
class SharedMemoryBridge;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;   // 用于监听窗口状态变化
    void closeEvent(QCloseEvent *event) override;  // 强制清理进程
    void resizeEvent(QResizeEvent *event) override;  // 调整mascotImage位置
    //void showEvent(QShowEvent *event) override;  // 窗口显示时调整mascotImage位置
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onMaximizeClicked();



    void on_pushButton_clicked(bool checked);

private:
    void setupUi();
    void createTitleBar();
    void updateCurrentBotInfo();
    void cycleCurrentBot();
    void showBotSelectorMenu();
    void switchToBot(int index);
    void xr();
    void checkUpdate();
    void showUpdateDialog(const QString &version, const QString &releaseNotes, const QString &downloadUrl);
    void startDownloadAndReplace(const QString &version, const QString &downloadUrl) ;
    QPixmap generateBotAvatar(const QString &initial, const QString &colorHex) const;
    QNetworkAccessManager *networkManager=nullptr;

    void applyStyleSheet();
    // UI 组件

    QWidget *sideBar;


    QPushButton *btnHome, *btnAccount, *btnLog, *btnPlugin, *btnChat,*checkUpdateBtn;
    QButtonGroup *btnGroup;
    QLabel *m_kantoumusume;
    // 标题栏
    QWidget *titleBar;
    QWidget *botStatusWidget;
    QLabel *titleAvatarLabel;
    QLabel *titleBotNameLabel;
    QLabel *titleBotStatusLabel;
    QToolButton *minBtn;
    QToolButton *maxBtn;
    QToolButton *closeBtn;

    // 窗口缩放与移动
    bool resizing;
    Qt::Edges resizeEdge;
    QPoint dragStartPos;
    QSize startSize;
    QPoint startGlobalPos;
    int edgeMargin;
    //QWidget *aiContainer;      // 用来承载 ai.ui 的容器
    //Ui::Ai aiUi;              // UI 对象
    // 其他


    QTimer *m_heartbeatTimer;

    QString m_appliedLogBotId;

};

#endif // MAINWINDOW_H