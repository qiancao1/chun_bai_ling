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