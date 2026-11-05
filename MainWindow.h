/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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


class HomePage;
class AccountPage;
class LogPage;
class PluginPage;
class ChatPage;
#ifdef _WIN32
class SharedMemoryBridge;
#endif


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 切到「聊天」页（等价于用户点左侧导航的「聊天」按钮）。
    // 给「日志」页右键菜单的「转到聊天室」用 —— 那边拿不到私有的导航按钮。
    void gotoChatPage();

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




private:
    void setupUi();
    void createTitleBar();

    // ── 无边框窗口的边缘缩放 ──
    // 窗口是 FramelessWindowHint，系统那套边框拖拽全没了，只能自己判边缘：
    //   edgesAt()      全局坐标 → 落在哪几条边上（角落会同时带两条）
    //   applyEdgeCursor() 按边上/边下切换鼠标指针（走 setOverrideCursor，因为光标形状由光标下的子控件决定）
    //   beginResize()  记下按下时的几何，performResize() 按位移算新矩形
    Qt::Edges edgesAt(const QPoint &globalPos) const;
    void applyEdgeCursor(Qt::Edges edges);
    void beginResize(Qt::Edges edges, const QPoint &globalPos);
    void performResize(const QPoint &globalPos);
    void setWindowShadowMargin(int m);   // 最大化/全屏时把外圈阴影带收掉
    void updateCurrentBotInfo();
    void cycleCurrentBot();
    void showBotSelectorMenu();
    void switchToBot(int index);
    // 切「配置目标」账号（g_appid）。头部机器人下拉删除后，由标题栏选择器统一触发。
    bool applyConfigAppId(int appid);
    void syncConfigAppIdFromTitleBot();
    void xr();
    void checkUpdate();

    void showUpdateDialog(const QString &version, const QString &releaseNotes);

    QPixmap generateBotAvatar(const QString &initial, const QString &colorHex) const;
    QNetworkAccessManager *networkManager=nullptr;

    void applyStyleSheet();
    // UI 组件

    QWidget *navBar=nullptr;      // 顶部导航条（原左侧 sideBar）


    QPushButton *btnHome, *btnAccount, *btnLog, *btnPlugin, *btnChat,*checkUpdateBtn;
    QButtonGroup *btnGroup;
    QLabel *m_kantoumusume=nullptr;
    // 标题栏
    QWidget *titleBar;
    QWidget *botStatusWidget;
    QLabel *titleAvatarLabel;
    QLabel *titleBotNameLabel;
    QLabel *titleBotStatusLabel;
    QToolButton *minBtn;
    QToolButton *maxBtn;
    QToolButton *closeBtn;
    QToolButton *restartBtn;      // 标题栏「快速重启」

    // 窗口缩放与移动
    bool resizing;
    Qt::Edges resizeEdge;
    QPoint dragStartPos;
    QSize startSize;
    QPoint startGlobalPos;
    int edgeMargin;
    QRect resizeStartRect;       // 开始缩放时的窗口几何（performResize 在这上面加位移）
    bool edgeCursorActive = false;   // 是否已经 push 过 override cursor（必须成对 restore）
    QWidget *m_windowRoot = nullptr; // 包住 central 的透明根：画阴影 + 当拖边热区
    //QWidget *aiContainer;      // 用来承载 ai.ui 的容器
    //Ui::Ai aiUi;              // UI 对象
    // 其他


    QTimer *m_heartbeatTimer;

    QString m_appliedLogBotId;

};

#endif // MAINWINDOW_H