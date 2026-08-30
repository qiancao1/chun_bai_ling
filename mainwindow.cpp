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



#include "mainwindow.h"
#include "aiwidget.h"
#include "blacklistpage.h"
#include "buttoneditor.h"
#include "homepage.h"
#include "accountpage.h"
#include "keywordpunishconfigwidget.h"
#include "nickreviewwidget.h"
#include "pluginpage.h"
#include <QGraphicsOpacityEffect>
#include "logpage.h"
#include "scheduleconfigwidget.h"
#include "screenshotsyncclient.h"
#include "botruleconfigwidget.h"

#include "chatpage.h"
#include "forbiddenwordpage.h"
#include "global.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QCursor>
#include <QEvent>
#include <QToolButton>
#include <QLabel>
#include <QMenu>
#include <QIcon>

#include <QStyle>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPainter>
#include <QFile>
#include <QtCore>
#include <qgroupbox.h>
#include "htmltoimagewidget.h"
#include "menupanelwidget.h"
#include "plts.h"
#include "qunguan.h"
#include "sandboxwindow.h"
#include "set.h"
#include "textreplaceconfigwidget.h"
#include "keywordmatchconfigwidget.h"
#include <QNetworkReply>
#include <QProgressDialog>
#include <qmessagebox.h>


#define APP_VERSION_STR "v1.2.11.52"
#define APP_BUILD_NUMBER 52
QStackedWidget *stackedWidget=nullptr;
QString Homev=R"(
# 更新日志🌸
## v1.2.11.52 (2026-08-24)
- 增加 分群 违禁词撤回 全局违禁词撤回<-用不到应该
- 增加 入群验证

## v1.2.10.50 (2026-08-22)
- 修复 cos图床不可用问题
- 优化 内网并发上传

## v1.2.9.49 (2026-08-21)
- 优化 禁言 撤回 等指令匹配方式 不需要按照特定格式来执行了
- 增加 内置群管 无管理权限回复

## v1.2.8.48 (2026-08-20)
- 优化 订阅主动推送 会触发限制问题
- 优化 内置AI图片
- 增加 关键词回复 允许自动按照空格分割这里
- 增加 api请求处理格式：结果1：%1 结果2：%2 [get url=xxx json.xx json.xx] 其中 json.xx 代表json路径 当然可不传
- 增加 根据关键词自动同意加群
- 增加 入群提醒 管理员可设置是否放行
- 修复 在群里无法清除AI上下文问题

## v1.2.7.43 (2026-08-16)
- 修复 Ai把我 @event 等装饰器 等代码删了导致无法 注册指令
- 增加 查看py注册指令按钮
- 修复 按钮挂载启动时不自动加载问题
- 修复 内存泄漏 移除 部分 QEventLoop 的使用 运行时 可能100m 缓存上去可能200m然后固定
- 优化 指令修改面板
- 优化 Ai艾特不转换问题
- 修复 v1.2.5.37 修改的图片 产生的bug
- 优化 日志刷新界面 为1秒刷新一次 不再每次刷新

## v1.2.6.39 (2026-08-12)
- 修复 内置AI决策模型的一些问题
- 增加 AI每个 角色专属表情包库
- 修复 Token过期 问题
- 增加 指令面板设置 可以快捷修改指令
- 增加 数据库缓存 增加 数据统计 可以实时查看机器人 数据（缓存的附属品）

## v1.2.5.37 (2026-08-12)
- 内置一些快捷这里 方便查看内置属性
- 修复 32位进程问题
- 修复 cnb cos测试异常问题
- 修复 机器人不能使用其他机器人缓存问题
- 修复 Ai挖的坑 增加 查看框架发送的原始JSON
- 优化 图片上传 然后你是内网 将执行并发上传 也就是上传10张和 上传1张耗时一样

## v1.2.0.32 (2026-08-07)
- 优化 图片视频 文件类如果是腾讯 广州服务器 自动走内网
- 增加 cos桶内网上传 (没自动删除)
- 增加 cnb图床 一样走的内网 带自动删除
- 增加 禁言 获取禁言列表 处理加群 获取加群列表 等api
- 更新 日志对群昵称的显示

## v1.1.9.30 (2026-08-05)
- 修复 处于日志界面 一直添加日志会导致内存一直叠加占用 并且会我操作时 自动切换到其他页面 防止无意义刷新ui
- 修复 聊天室中 卡顿问题

- 日志 添加新的菜单
- 增加 超级管理员 可用操作设置
- 增加 自动回应回调
- 增加 自动安装JS插件 引用包
- 增加 查看原始json选择项
- 增加 python 自动pip包

## v1.1.7.25 (2026-07-29)
- 增加了一个托盘 为了避免和当前版本冲突 只能通过托盘隐藏窗口
- 修复 无指令回复 允许跨框架只回复一个
- 修复 内置ai的一些bug 增加决策ai 需要一定的token
- 修复 本地图床不可用问题，共享图床改为本地
- 增加 异步方法 增加强制 异步 可以提高框架并发能力


## v1.1.5.20 (2026-07-25)
- 用户 ai生成插件改为流式 增加事件订阅 添加按钮sdk
- 增加 插件市场 怎么上传 先发群里面吧
- 优化 日志数据库
- 优化 api请求方式
- 优化 python 针对异步的 支持 增加 新的指令注册方式 <--经过测试 似乎py有专门异步线程
- 优化 python 不再需要安装python 因为框架自带完全环境
- 优化 聊天室 发送消息规则 msgid -> 主动 -> 召回
- 优化 接收到消息自动删除/字符
- 修复 日志数据库 满了不自动扩容问题
- 修复 python热重载 缓存未清理问题
- 增加 一个内置指令#纯白铃铛 #纯白铃
- 为内置关键词回复 添加 提示

## v1.1.3.17 (2026-07-17)
- 优化 原版mc插件的支持
- 优化 数据库 一个原子变量到内存 减少一次数据库读取
- 优化 ai生成插件添加 按钮模板 增加更多工具 增加加载卸载插件 针对某个函数 读写修改
- 修复 富媒体分片上传 错误问题
- 修复 在全量群 发送语音 视频 文件 等 没msgid时 不重试使用主动
- 修复 py插件重载 指令正则类未清空
- 修复 发送普通信息 图片无效问题
- 修复 按钮生成小按钮 问题
- 修复 被Ai删除的 \\n 文本
- 修复 pysdk 中有一行代码缩进有问题
- 修复 按钮回调 数据读取错误问题
- 修复 移除32的try写出错误文件
- 修复 聊天室打开时主动获取msgid 是旧的问题
- webhook 疑似会丢信息 待验证
- 增加 一个常用 退群提示 使用主动发送 仅全量群可用
- 增加 加群提示 支持等待添加多个成员后一起回复
- 增加 webui 可以添加 编辑机器人 登录 下线机器人 账号（有接口 但是js没写）
- 增加 管理员可用
- addbot{appid,secret,type,markdown,wsIntents} //添加机器人到账号列表
- delbot{appid} 删除某个机器人
- login{appid}  登录某个机器人
- logout{appid} 下线某个机器人
- botlist  查看机器人列表
- boterr{appid} 查看最后错误 等指令 可用快捷添加 机器人到框架


## v1.1.2.14 (2026-07-05)
- 增加 webhook 的支持
- 增加 webui 面板 可以在 手机或浏览器查看日志 或 回复内容
- 优化 管理列表将指定到机器人
- 增加 ai一键生成插件面板
- 增加 附加Ai模型 可以使用指令触发制定模型
- 增加 Ai 随机回复开关 不再是每条都回复 由随机 固定条数触发
- 修复 所有[image,path=]方式 图片缓存读取鼠标问题
- 修复 手动下线后 重启框架 会自动登录问题
- 增加 对频道的基本消息支持 普通消息 原生MD信息 模板暂时未支持
- 增加 AI对向量数据库的支持 可以自己本地部署模型 或付费使用别人的
- 部署链接[链接](https://ollama.com/download/windows) 如果不想下载这个 也可以用其他
- 或者 [硅基流动](https://www.siliconflow.cn/) 有提供的免费向量模型 "BAAI/bge-m3"
- 优化 ai 部分每小时清理内存
- 修复 部分机器人没union_openid 导致用户id为空

## v1.1.0.10 (2026-06-26)
- 修复 回复设置未保存问题
- 增加 刷屏检测
- 增加 ai白名单 模式 以及设置开启等指令
- 重写 日志系统 减少内存消耗
- 移除 使用了 GPLv3 协议的库(强制开源) 框架整体使用LGPL协议(非强制开源)
- 修复 聊天室 图片不加载问题
- 修复 聊天室 全量群发送信息失败
- 优化 发送图片格式 [image,path=xx] 改为 !\[img](路径|链接) [image,path=xx] 仍然可用

## v1.0.4.5 (2026-06-23)
- 好好好 茜草改名为 纯白铃铛
- 添加 订阅 也可以叫定时 可以推送订阅信息的群 支持自定义提交参数
- 修复 js子进程不会自动退出问题
- 优化 适配云崽 【部分】单js插件
- 添加 Html制图
- 添加 常用功能Ai 内置 联网函数 特殊函数执行py代码 可以控制你电脑 拟人状态可以自己添加删除表情包
- 添加 批量推送信息
- 修复 部分机器人 未下发unid at_you变量一直是true
- 更新 加密算法 使用宏 所以需要更新密码
- 增加 内置入群欢迎
- 修复 发送图片时 缓存更新未更新导致发送图片无法查看
- 修复 聊天室 发送内容异常问题 忘记什么时候改了

## v1.0.3.4 (2026-06-14)
- 增加对 JS 插件的支持
- 修复 发送失败时 无错误信息
- 其他 python 启用多线程 请注意编写py代码时多线程有没有问题
- 修复 沙盒保存到插件 编码问题
- 修复 被添加好友 删除好友时崩溃问题

## v1.0.2.3 (2026-06-10)
- 修复 代理类ws 不可用问题
- 修复 检查更新 下载失败问题

## v1.0.1.2 (2026-06-08)
- 降低版本 指qt 5.15.3 兼容多类 低版本心跳

## v1.0.0.1 (2026-06-07)
- 修复\[]\()语法无效问题

## v1.0.0 (2026-06-06)
- 初始版本发布

)";

// 全局指针（保持与你原有代码一致）
HomePage *homePage = nullptr;
set *setA=nullptr;
AccountPage *accountPage = nullptr;
LogPage *logPage = nullptr;
PluginPage *pluginPage = nullptr;
ChatPage *chatPage = nullptr;
SandboxWindow *Sandbox = nullptr;
ButtonEditor *buttonEditorPage=nullptr;
BotRuleConfigWidget *RuleConfigWidget=nullptr;
TextReplaceConfigWidget *TextReplace=nullptr;
KeywordMatchConfigWidget *keyword=nullptr;
KeywordPunishConfigWidget *keyword_Punish=nullptr;
BlacklistPage *Black=nullptr;
ForbiddenWordPage *forbidden=nullptr;
ScheduleConfigWidget *schedule=nullptr;
HtmlToImageWidget *htmltoimg =nullptr;
ScreenshotSyncClient *ScreenA=nullptr;
AiWidget *ai_ui = nullptr;
qunguan *ui_qunguan=nullptr;
MenuPanelWidget *ui_MenuPanel=nullptr;

QListWidget *robotListWidget=nullptr;
QTabWidget *configTabWidget2=nullptr;
NickReviewWidget *m_nickReviewWidget=nullptr;

int m_currentBotIndex = -1;
int 定时检查变量=0;
extern int ts_m_appid;
extern bool ts_m_stopPush;


quint32 getLastTimestamp(const UserStat &stat) {
    if (stat.count == 0) return 0;  // 无数据，当作过期
    int capacity = stat.buffer.size();
    int lastIdx = (stat.head + stat.count - 1) % capacity;
    return stat.buffer[lastIdx];
}
quint32 getTimestampMs();
void cleanInactiveUsers(QHash<int,UserStat> *hash, quint32 now, quint32 expireMs) {
    if (!hash) return;
    QMutableHashIterator<int,UserStat> it(*hash);
    while (it.hasNext()) {
        it.next();
        const UserStat &stat = it.value();
        quint32 last = getLastTimestamp(stat);

        if (last == 0 || (now - last) > expireMs) {
            it.remove();
        }
    }
}
MainWindow::MainWindow(QWidget *parent): QMainWindow(parent), resizing(false), edgeMargin(5)
{
    // 无边框窗口
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    resize(1040, 660);
    setMinimumSize(900, 560);
    setWindowTitle("纯白铃铛");

    setupUi();
    xr();
    applyStyleSheet();



    // 默认选中首页
    btnHome->setChecked(true);
    stackedWidget->setCurrentIndex(0);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(3000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if(框架退出) return;

        g_cnb.qcjs++;
        if(logPage->m_active){
            logPage->leiji++;
            if(logPage->leiji>200)
            {
                stackedWidget->setCurrentIndex(3);
                logPage->setActive(false);

            }
        }
        auto ss = stackedWidget->currentWidget();
        if (ss == accountPage) {
            for (CardWidget* card : std::as_const(g_CW)) {
                if (card) card->onTimeRefresh();
            }
        }
        processPendingEvents();
        schedule->jiancha();
        if(ss==homePage) homePage->refreshRuntimeStats();
        #ifdef _WIN32
        if (!bridge) return;
        if (miaomiao32 >= 2)
            AppendEventLog("与加载器通讯失败了.." + QString::number(miaomiao32));
        bridge->writeResponseToBlock(1, "{\"type\":7}");
        miaomiao32++;
        miaomiao++;
        if (miaomiao32 >= 4) {
            if (bridge->restartYiProcess()) {
                miaomiao32 = 0;
                miaomiao = 7;
            }
        }
        if ((miaomiao & 7) == 0)
            pluginPage->syncPluginsTo32();
        #endif
    });
    m_heartbeatTimer->start();


    QTimer *cleanTimer = new QTimer();
    QObject::connect(cleanTimer, &QTimer::timeout, [=]() {
        quint32 now = getTimestampMs();
        for(auto &acc : m_accounts)
        {
            cleanInactiveUsers(&acc->stat, now, 30 * 60 * 1000); // 30分钟
        }
    });
    cleanTimer->start(60000); // 每60秒检查一次

}

void MainWindow::xr()
{
    m_kantoumusume = new QLabel(this);
    QPixmap pixmap(":/icons/qiancao1.png");
    m_kantoumusume->setPixmap(pixmap);
    m_kantoumusume->resize(pixmap.size());
    QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect();
    m_kantoumusume->setStyleSheet("background: transparent;");
    effect->setOpacity(0.5);
    m_kantoumusume->setGraphicsEffect(effect);
    m_kantoumusume->move(width() - m_kantoumusume->width() - 10,
                       height() - m_kantoumusume->height() + 40);
    m_kantoumusume->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void showClickableLicenseInfo() {
    QMessageBox msgBox;
    msgBox.setWindowTitle("关于");
    msgBox.setIcon(QMessageBox::Information);

    QString richText =
        "<h3>纯白铃铛 - QQ 机器人管理平台</h3>"
        "本项目主体采用 <a href=\"https://www.gnu.org/licenses/lgpl-3.0.html\">LGPLv3 协议</a> 开源。<br>"
        "完整源代码（含所有修改）请访问：<br>"
        "<a href=\"https://github.com/qiancao1/chun_bai_ling\">GitHub</a> 或 "
        "<a href=\"https://gitee.com/linglan2/chun-bai-ling-dang\">Gitee</a><br><br>"

        "<b>使用的第三方库及许可：</b><br><br>"

        "<b>Qt 5.15.3</b>（动态链接）<br>"
        "Copyright (C) The Qt Company Ltd. and other contributors.<br>"
        "• Widgets、Network、WebSockets 模块：<a href=\"https://www.gnu.org/licenses/lgpl-3.0.html\">LGPLv3</a><br>"
        "（动态链接下，LGPL 要求提供 Qt 库的源代码，并允许用户修改库后重新链接；无需提供您的目标文件）<br>"
        "Qt 库源代码：<a href=\"https://code.qt.io/cgit/qt/\">https://code.qt.io/cgit/qt/</a><br><br>"

        "<b>pybind11</b><br>"
        "Copyright (c) 2016–2023 The pybind11 authors.<br>"
        "采用 <a href=\"https://opensource.org/licenses/BSD-3-Clause\">BSD 3-Clause License</a><br><br>"

        "<b>LMDB (Lightning Memory-Mapped Database)</b><br>"
        "Copyright (c) 2011–2021, Howard Chu, Symas Corp.<br>"
        "采用 <a href=\"https://www.openldap.org/software/release/license.html\">OpenLDAP Public License</a>（Version 2.8）<br><br>"

        "<b>hnswlib</b><br>"
        "Copyright (c) 2016 Yury Malkov and contributors.<br>"
        "采用 <a href=\"https://www.apache.org/licenses/LICENSE-2.0\">Apache License, Version 2.0</a><br><br>"

        "<b>📦 关于 DLL 插件（LGPL 合规说明）：</b><br>"
        "本程序支持动态加载第三方 DLL 插件。<br>"
        "• 若插件与主程序采用动态链接（通过接口交互），则插件无需开源，<br>"
        "  其许可证由插件作者自行决定。<br>"
        "• 若插件与主程序静态链接，则需遵守 LGPL 条款（提供重新链接所需的目标文件）。<br>"
        "用户自行下载和使用插件的，相关责任由插件作者及用户承担。<br><br>"

        "<b>其他资源：</b><br>"
        "图标来源：<a href=\"https://icons8.com\">icons8.com</a><br>"
        "AI 中转服务：<a href=\"https://allgpt.xianyuw.cn\">咸鱼Ai中转</a><br>"
        "官方 QQ 群：<a href=\"https://qm.qq.com/q/pPykIoOqGW\">827737534</a><br><br>"

        "本软件为免费开源项目，仅供学习交流使用。";

    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(richText);
    msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
    msgBox.setCursor(Qt::PointingHandCursor);
    msgBox.exec();
}

MainWindow::~MainWindow()
{
    delete ScreenA;
    ScreenA = nullptr;


    delete keyword;
    keyword = nullptr;
    delete keyword_Punish;
    keyword_Punish=nullptr;
    delete TextReplace;
    TextReplace = nullptr;
    delete RuleConfigWidget;
    RuleConfigWidget = nullptr;
    delete schedule;
    schedule = nullptr;
    delete forbidden;
    forbidden = nullptr;
    delete Black;
    Black = nullptr;

    delete htmltoimg;
    htmltoimg = nullptr;

    delete buttonEditorPage;
    buttonEditorPage = nullptr;

    delete Sandbox;
    Sandbox = nullptr;

    delete ui_qunguan;
    ui_qunguan = nullptr;



    // 这些可能是成员指针，由父窗口管理，但为了安全也清理
    delete robotListWidget;
    robotListWidget = nullptr;
    delete configTabWidget2;
    configTabWidget2 = nullptr;

    // 核心页面（最后清理）
    delete homePage;
    homePage = nullptr;

    delete setA;
    setA = nullptr;

    delete accountPage;
    accountPage = nullptr;

    delete logPage;
    logPage = nullptr;

    delete pluginPage;
    pluginPage = nullptr;

    delete chatPage;
    chatPage = nullptr;
}

bool _g_qieh=false;
void MainWindow::setupUi()
{
    robotListWidget = new QListWidget;
    ScreenA= new ScreenshotSyncClient;
    networkManager = new QNetworkAccessManager(this);
    homePage = new HomePage;
    accountPage = new AccountPage;
    logPage = new LogPage;
    pluginPage = new PluginPage;
    chatPage = new ChatPage;
    Sandbox = new SandboxWindow;
    setA = new set;
    buttonEditorPage = new ButtonEditor;
    RuleConfigWidget = new BotRuleConfigWidget;
    TextReplace = new TextReplaceConfigWidget;
    keyword = new KeywordMatchConfigWidget;
    keyword_Punish = new KeywordPunishConfigWidget;
    Black = new BlacklistPage;
    forbidden = new ForbiddenWordPage;
    schedule =new ScheduleConfigWidget;
    htmltoimg = new HtmlToImageWidget(this);
    ai_ui = new AiWidget;
    ui_qunguan = new qunguan;
    ui_MenuPanel = new MenuPanelWidget;



    m_nickReviewWidget = new NickReviewWidget(this);

    connect(m_nickReviewWidget, &NickReviewWidget::approveRequested,
            this, [this](const QList<QPair<uint32_t,uint32_t>>& pairs, const QStringList& nicks) {
                for (int i = 0; i < pairs.size(); ++i) {
                    uint32_t appId = pairs[i].first;
                    uint32_t seq = pairs[i].second;
                    BotDB* db = g_botdb.value(appId);
                    if (!db) continue;
                    db->updateUserBySeqId(seq, [&](UserRecord& rec) {
                        strncpy(rec.name, nicks[i].toUtf8().constData(), sizeof(rec.name)-1);
                        rec.name[sizeof(rec.name)-1] = '\0';
                    });
                }
            });

    connect(m_nickReviewWidget, &NickReviewWidget::batchApproveRequested,
            this, [this](const QList<uint32_t>& seqIds, const QStringList& nicks) {
                BotDB* db = g_botdb.value(g_appid);
                if (!db) return;
                for (int i = 0; i < seqIds.size(); ++i) {
                    db->updateUserBySeqId(seqIds[i], [&](UserRecord& rec) {
                        strncpy(rec.name, nicks[i].toUtf8().constData(), sizeof(rec.name)-1);
                        rec.name[sizeof(rec.name)-1] = '\0';
                    });
                }
            });

    connect(m_nickReviewWidget, &NickReviewWidget::batchRejectRequested,
            this, [this](const QList<uint32_t>& seqIds) {
                BotDB* db = g_botdb.value(g_appid);
                if (!db) return;
                for (uint32_t seq : seqIds) {
                    db->updateUserBySeqId(seq, [&](UserRecord& rec) {
                        rec.name[0] = '\0';
                    });
                }
            });

    connect(m_nickReviewWidget, &NickReviewWidget::batchCancelRequested,
            this, [this](const QList<uint32_t>& seqIds) {
                BotDB* db = g_botdb.value(g_appid);
                if (!db) return;
                for (uint32_t seq : seqIds) {
                    db->updateUserBySeqId(seq, [&](UserRecord& rec) {
                        rec.name[0] = '\0';
                    });
                }
            });


    plts *myPlts = new plts(this);   // 创建 plts 对象
    myPlts->show();





    QGroupBox *configGroupBox = new QGroupBox();   // 分组框，标题可自定义

    configGroupBox->setStyleSheet("QGroupBox { padding-top: 5px; margin-top: 0px; border: 0px; }");
    QWidget *nwid = new QWidget;
    QHBoxLayout *hxzsy = new QHBoxLayout(nwid);           // 选择夹
    hxzsy->setContentsMargins(0,0,0,0);


    robotListWidget->setMinimumWidth(100);
    robotListWidget->setMaximumWidth(220);
    hxzsy->addWidget(robotListWidget);
    configTabWidget2 = new QTabWidget;           // 选择夹

    configTabWidget2->addTab(ui_MenuPanel,"面板");
    configTabWidget2->addTab(ui_qunguan,"基础");
    configTabWidget2->addTab(m_nickReviewWidget,"昵称审核");
    configTabWidget2->addTab(myPlts, "批量推送");
    configTabWidget2->addTab(RuleConfigWidget, "按钮挂载");
    configTabWidget2->addTab(TextReplace, "自定义替换");
    configTabWidget2->addTab(keyword, "关键词回复");
    configTabWidget2->addTab(keyword_Punish, "关键词撤回");
    configTabWidget2->addTab(schedule, "订阅|定时");
    configTabWidget2->addTab(ai_ui, "Ai");

    connect(configTabWidget2, &QTabWidget::currentChanged,
            [this](){
                if(!ui_MenuPanel->m_botClient)
                {
                    _g_qieh=true;
                }
                if(configTabWidget2->currentIndex()==0 && _g_qieh){
                    _g_qieh=false;
                    ui_MenuPanel->switchBot();

                }
    });
    hxzsy->addWidget(configTabWidget2);



    QTabWidget *configTabWidget = new QTabWidget;           // 选择夹
    configTabWidget->addTab(setA, "基础设置");
    configTabWidget->addTab(buttonEditorPage, "按钮生成");
    configTabWidget->addTab(nwid, "内置功能");
    configTabWidget->addTab(Black, "黑名单管理");
    configTabWidget->addTab(forbidden, "违禁词过滤");
    configTabWidget->addTab(htmltoimg, "HTML制图");

    connect(configTabWidget, &QTabWidget::currentChanged,
            [configTabWidget](){
                if(框架退出) return ;
                if(configTabWidget->currentIndex()==0 ){

                    if(setA->m_cnb->isChecked()!=g_cnb.e){
                        setA->m_cnb->setChecked(g_cnb.e);
                        QMessageBox::warning(nullptr,"临时关闭","cnb 图床因连续16次上传失败 已临时停用，如果需要开启请打勾开个点击保存按钮,"
                                                                  "\n\n注意 重启后仍然自动启用，你需要点击一次保存 才会关闭");
                    }
                    if(setA->m_cos->isChecked()!=g_cos.e){

                        setA->m_cos->setChecked(g_cos.e);
                        QMessageBox::warning(nullptr,"临时关闭","cos 图床因连续16次上传失败 已临时停用，如果需要开启请打勾开个点击保存按钮,"
                                                                  "\n\n注意 重启后仍然自动启用，你需要点击一次保存 才会关闭");
                    }
                }
            });
    // 将选择夹放入分组框
    QVBoxLayout *groupLayout = new QVBoxLayout(configGroupBox);
    groupLayout->setContentsMargins(0, 0, 0, 0);

    groupLayout->addWidget(configTabWidget);



    // ========== 堆叠窗口 ==========
    stackedWidget = new QStackedWidget;
    stackedWidget->addWidget(homePage);          // index 0
    stackedWidget->addWidget(accountPage);       // index 1
    stackedWidget->addWidget(logPage);           // index 2
    stackedWidget->addWidget(pluginPage);        // index 3
    stackedWidget->addWidget(chatPage);          // index 4
    stackedWidget->addWidget(Sandbox);           // index 5
    stackedWidget->addWidget(configGroupBox); // index 6

    stackedWidget->setObjectName("contentStack");


    sideBar = new QWidget;
    sideBar->setFixedWidth(148);
    sideBar->setObjectName("sideBar");
    sideBar->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(16, 0, 16, 18);
    sideLayout->setSpacing(8);
    sideLayout->setAlignment(Qt::AlignTop);


    QWidget *brandWidget = new QWidget;
    brandWidget->setObjectName("brandWidget");
    QHBoxLayout *brandLayout = new QHBoxLayout(brandWidget);
    brandLayout->setContentsMargins(0, 16, 0, 10);
    brandLayout->setSpacing(8);

    sideLayout->addWidget(brandWidget);


    auto createNavButton = [](const QString &text, const QIcon &icon) {
        QPushButton *btn = new QPushButton(text);
        btn->setIcon(icon);
        btn->setIconSize(QSize(20, 20));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setObjectName("navBtn");
        btn->setMinimumHeight(40);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return btn;
    };


    btnHome = createNavButton("首页", QIcon(":/icons/home.png"));
    btnAccount = createNavButton("账号", QIcon(":/icons/account.png"));
    btnLog = createNavButton("日志", QIcon(":/icons/log.png"));
    btnPlugin = createNavButton("插件", QIcon(":/icons/plugin.png"));
    btnChat = createNavButton("聊天", QIcon(":/icons/chat.png"));
    QPushButton *Sandbox2 = createNavButton("沙盒", QIcon(":/icons/sandbox.png"));

    QPushButton *btnAdvancedConfig = createNavButton("高级配置", QIcon(":/icons/advanced.png"));
    // *btn_newui = createNavButton("扩展页面", QIcon(":/icons/advanced.png"));
    btnGroup = new QButtonGroup(this);
    btnGroup->setExclusive(true);
    btnGroup->addButton(btnHome, 0);
    btnGroup->addButton(btnAccount, 1);
    btnGroup->addButton(btnLog, 2);
    btnGroup->addButton(btnPlugin, 3);
    btnGroup->addButton(btnChat, 4);
    btnGroup->addButton(Sandbox2, 5);
    btnGroup->addButton(btnAdvancedConfig, 6);
    //btnGroup->addButton(btn_newui, 7);

    connect(btnGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            [this](int id) {
                updateCurrentBotInfo();
                stackedWidget->setCurrentIndex(id);
                if (logPage)
                {
                    logPage->setActive(id == 2);
                }
                chatPage->存在 = (id==4);
            });
    connect(robotListWidget, &QListWidget::currentRowChanged, [this](){
        QListWidgetItem *item = robotListWidget->currentItem();
        if(ts_m_stopPush)
        {
            QMessageBox::warning(this,"批量推送中","批量推送信息中展示不能切换 账号");
            return ;
        }
        g_appid = item->data(Qt::UserRole).toInt();
        RuleConfigWidget->列表行被单击();
        TextReplace->列表行被单击();
        keyword->列表行被单击();
        schedule->列表行被单击();
        ai_ui->list_c();

        ui_qunguan->列表行被单击();
        m_nickReviewWidget->setAppId(g_appid);

        if(configTabWidget2->currentIndex()==0){
                ui_MenuPanel->switchBot();
                _g_qieh=false;
                return;
        }
        _g_qieh=true;
    });
    sideLayout->addWidget(btnHome);
    sideLayout->addWidget(btnAccount);
    sideLayout->addWidget(btnLog);
    sideLayout->addWidget(btnPlugin);
    sideLayout->addWidget(btnChat);
    sideLayout->addWidget(Sandbox2);
    sideLayout->addWidget(btnAdvancedConfig);   // 新按钮
    //QLabel *kzui = new QLabel("———————");
    //kzui->setAlignment(Qt::AlignCenter);
    //sideLayout->addWidget(kzui);   // 新按钮
    //sideLayout->addWidget(btn_newui);   // 新按钮
    sideLayout->addStretch();


    checkUpdateBtn = new QPushButton("检查更新");
    checkUpdateBtn->setFlat(true);
    checkUpdateBtn->setObjectName("aaaaaaaa");
    checkUpdateBtn->setStyleSheet("QPushButton:hover { color: blue; }");
    connect(checkUpdateBtn, &QPushButton::clicked, [this](){

        checkUpdate();
    });
    sideLayout->addWidget(checkUpdateBtn);
    QPushButton *btnLicense = new QPushButton("关于");
    btnLicense->setObjectName("aaaaaaaa");
    btnLicense->setCursor(Qt::PointingHandCursor);
    btnLicense->setMinimumHeight(24);

    btnLicense->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sideLayout->addWidget(btnLicense);

    // 点击按钮时弹出 Qt 许可证声明
    connect(btnLicense, &QPushButton::clicked, this, [](){
        showClickableLicenseInfo();
    });
    createTitleBar();

    QHBoxLayout *mainContentLayout = new QHBoxLayout;
    mainContentLayout->setContentsMargins(0, 0, 0, 0);
    mainContentLayout->setSpacing(0);
    mainContentLayout->addWidget(sideBar);
    mainContentLayout->addWidget(stackedWidget, 1);

    QWidget *contentWidget = new QWidget;
    contentWidget->setObjectName("contentWidget");
    contentWidget->setLayout(mainContentLayout);

    QVBoxLayout *totalLayout = new QVBoxLayout;
    totalLayout->setContentsMargins(4, 4, 4, 4);
    totalLayout->setSpacing(4);
    totalLayout->addWidget(titleBar);
    totalLayout->addWidget(contentWidget, 1);

    QWidget *central = new QWidget;
    central->setObjectName("centralRoot");
    central->setLayout(totalLayout);

    central->setStyleSheet(
        "QWidget#centralRoot {"
        "   background: #FFF8EF;"
        "   border-radius: 10px;"
        "   border: 1px solid #A176C2;"      // 您可以根据喜好调整
        "}"
        );
    setCentralWidget(central);
    robotListWidget->setCurrentRow(0);


}
constexpr unsigned int hash(const char* str, unsigned int h = 0) {
    return *str ? hash(str + 1, (h * 31) + static_cast<unsigned int>(*str)) : h;
}

constexpr unsigned int getRandomNumber() {
    return hash(__TIME__) % 100;
}

void MainWindow::createTitleBar()
{
    titleBar = new QWidget;
    titleBar->setFixedHeight(46);
    titleBar->setObjectName("titleBar");
    titleBar->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(4);

    // ========== 新增左侧区域：图标 + 双标签 ==========
    QWidget *leftWidget = new QWidget;
    leftWidget->setObjectName("leftInfoWidget");
    QHBoxLayout *leftLayout = new QHBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    int randomIndex = QRandomGenerator::global()->bounded(1, 11);  // 生成 1~23
    QLabel *iconLabel = new QLabel("🔔");

    QString imagePath = QString(":/icons/log (%1).jpg").arg(randomIndex);
    QPixmap pixmap(imagePath);

    if (pixmap.isNull()) {
        pixmap = QPixmap(":/icons/log.jpg"); // 保险起见
    }


    iconLabel->setPixmap(pixmap);
    iconLabel->setFixedSize(40, 40);
    iconLabel->setScaledContents(true);   // 拉伸填满 40x40
    iconLabel->setAlignment(Qt::AlignCenter);
    // 注意：不再需要 resize(pixmap.size())，因为 setFixedSize 已经固定
    // 右侧垂直标签
    QVBoxLayout *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    QLabel *mainLabel = new QLabel(QString("纯白铃 %1").arg(APP_VERSION_STR));
    mainLabel->setObjectName("leftMainLabel");


    QLabel *subLabel = new QLabel("编写于2026年5月15号，");
    subLabel->setObjectName("leftSubLabel");

    leftWidget->setStyleSheet("background: transparent;");
    iconLabel->setStyleSheet("background: transparent; font-size: 20px;");
    mainLabel->setStyleSheet("background: transparent; font-size: 14px; font-weight: bold; color: #333;");
    subLabel->setStyleSheet("background: transparent; font-size: 11px; color: #888;");
    textLayout->addWidget(mainLabel);
    textLayout->addWidget(subLabel);

    leftLayout->addWidget(iconLabel);
    leftLayout->addLayout(textLayout);

    // ========== 原有机器人状态区域 ==========
    botStatusWidget = new QWidget;
    botStatusWidget->setObjectName("botStatusWidget");
    botStatusWidget->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *botStatusLayout = new QHBoxLayout(botStatusWidget);
    botStatusLayout->setContentsMargins(8, 4, 10, 4);
    botStatusLayout->setSpacing(8);

    titleAvatarLabel = new QLabel("B");
    titleAvatarLabel->setObjectName("titleAvatar");
    titleAvatarLabel->setFixedSize(34, 34);
    titleAvatarLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout *statusLayout = new QVBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);
    titleBotNameLabel = new QLabel("未选择机器人");
    titleBotNameLabel->setObjectName("titleUserName");
    titleBotStatusLabel = new QLabel("点击选择机器人");
    titleBotStatusLabel->setObjectName("titleOnline");
    statusLayout->addWidget(titleBotNameLabel);
    statusLayout->addWidget(titleBotStatusLabel);
    botStatusLayout->addWidget(titleAvatarLabel);
    botStatusLayout->addLayout(statusLayout);

    // ========== 窗口控制按钮 ==========
    const int btnSize = 26;
    minBtn = new QToolButton;
    minBtn->setFixedSize(btnSize, btnSize);
    minBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    minBtn->setToolTip("最小化");
    connect(minBtn, &QToolButton::clicked, this, &QMainWindow::showMinimized);

    maxBtn = new QToolButton;
    maxBtn->setFixedSize(btnSize, btnSize);
    maxBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    maxBtn->setToolTip("最大化");
    connect(maxBtn, &QToolButton::clicked, this, &MainWindow::onMaximizeClicked);

    closeBtn = new QToolButton;
    closeBtn->setFixedSize(btnSize, btnSize);
    closeBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    closeBtn->setToolTip("关闭");
    connect(closeBtn, &QToolButton::clicked, [this](){
        QApplication::quit();
    });

    minBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    maxBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));

    int iconSize = 14;
    minBtn->setIconSize(QSize(iconSize, iconSize));
    maxBtn->setIconSize(QSize(iconSize, iconSize));
    closeBtn->setIconSize(QSize(iconSize, iconSize));

    QString btnStyle = R"(
    QToolButton {
        background-color: transparent;
        border: none;
        border-radius: 8px;
    }
    QToolButton:hover {
        background-color: #FFF0DE;
    }
    QToolButton:pressed {
        background-color: #FFE0BF;
    }
    )";
    minBtn->setStyleSheet(btnStyle);
    maxBtn->setStyleSheet(btnStyle);
    closeBtn->setStyleSheet(btnStyle + "QToolButton:hover { background-color: #FFE2DF; color: #D83B32; }");

    // ========== 组装布局 ==========
    layout->addWidget(leftWidget);            // 新增的左侧区域
    layout->addStretch();                     // 弹性空间，将后续元素推到右侧
    layout->addWidget(botStatusWidget);       // 机器人状态区域（保持原有位置）
    layout->addSpacing(12);
    layout->addWidget(minBtn);
    layout->addWidget(maxBtn);
    layout->addWidget(closeBtn);

    titleBar->installEventFilter(this);
    botStatusWidget->installEventFilter(this);
    updateCurrentBotInfo();
}


void MainWindow::updateCurrentBotInfo()
{
    if (!titleBotNameLabel || !titleBotStatusLabel || !titleAvatarLabel) return;
    if (m_currentBotIndex == -1) {
        titleAvatarLabel->setText("A");
        titleAvatarLabel->setStyleSheet(
            "background: #8A94A6; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;");
        titleBotNameLabel->setText("全部机器人");
        titleBotStatusLabel->setText("显示所有账号的日志");

        if (logPage) {
            logPage->setCurrentBot(0, QString());
        }

        return;
    }
    if (m_accounts.isEmpty()) {
        m_currentBotIndex = -1;
        titleAvatarLabel->setText("B");
        titleAvatarLabel->setStyleSheet(
            "background: #8A94A6; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;");
        titleBotNameLabel->setText("全部机器人");
        titleBotStatusLabel->setText("暂无账号，日志显示全部数据");
        if (logPage && !m_appliedLogBotId.isEmpty()) {
            m_appliedLogBotId.clear();
            logPage->setCurrentBot(0, QString());
        }

        return;
    }

    if (m_currentBotIndex < 0 || m_currentBotIndex >= m_accounts.size()) {
        m_currentBotIndex = 0;
        for (int i = 0; i < m_accounts.size(); ++i) {
            if (m_accounts.at(i)->online) {
                m_currentBotIndex = i;
                break;
            }
        }
    }

    const auto &info = m_accounts.at(m_currentBotIndex);

    const QString initial = info->nickname.isEmpty() ? "B" : info->nickname.left(1).toUpper();
    const QString avatarColor = info->online ? "#65B85A" : "#D83B32";

    if (!info->avatarPath.isEmpty() && QFile::exists(info->avatarPath)) {
        QPixmap pix(info->avatarPath);
        if (!pix.isNull()) {
            titleAvatarLabel->clear();
            titleAvatarLabel->setPixmap(pix.scaled(34, 34, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            titleAvatarLabel->setStyleSheet(
                "border-radius: 17px; border: none;");
        } else {
            titleAvatarLabel->setPixmap(QPixmap());
            titleAvatarLabel->setText(initial);
            titleAvatarLabel->setStyleSheet(QString(
                "background: %1; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;").arg(avatarColor));
        }
    } else {
        titleAvatarLabel->setPixmap(QPixmap());
        titleAvatarLabel->setText(initial);
        titleAvatarLabel->setStyleSheet(QString(
            "background: %1; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;").arg(avatarColor));
    }

    titleBotNameLabel->setText(info->nickname.isEmpty() ? "未命名机器人" : info->nickname);
    titleBotStatusLabel->setText(QString("%1 · %2")
                                     .arg(info->online ? "● 在线" : "○ 离线"
                                     ,info->appid.isEmpty() ? "未配置 AppID" : info->appid));
    if (logPage && m_appliedLogBotId != info->appid) {
        m_appliedLogBotId = info->appid;
        logPage->setCurrentBot(info->appid_int, info->nickname);
    }

}

void MainWindow::cycleCurrentBot()
{
    if (m_accounts.isEmpty()) {
        m_currentBotIndex = -1;
    } else {
        m_currentBotIndex = (m_currentBotIndex + 1) % m_accounts.size();
    }
    m_appliedLogBotId.clear();
    updateCurrentBotInfo();
}

void MainWindow::showBotSelectorMenu()
{
    QMenu menu(this);
    menu.setObjectName("botSelectorMenu");
    menu.setStyleSheet(R"(
        QMenu {
            background: #FFFFFF; border: 1px solid #E0E0E0; border-radius: 8px;
            padding: 4px; font-size: 13px;
        }
        QMenu::item {
            padding: 8px 28px 8px 12px; border-radius: 4px; margin: 2px 4px;
            color: #333333;
        }
        QMenu::item:selected { background: #F5F5F5; color: #333333; }
        QMenu::item:disabled { color: #AAAAAA; }
        QMenu::separator { height: 1px; background: #F0F0F0; margin: 4px 8px; }
    )");
    if (m_accounts.isEmpty()) {
        QAction *act = menu.addAction("暂无账号");
        act->setEnabled(false);
    } else {
        for (int i = 0; i < m_accounts.size(); ++i) {
            const auto &info = m_accounts.at(i);
            const QString name = info->nickname.isEmpty()
                                     ? (info->botqq.isEmpty() ? "未命名" : info->botqq)
                                     : info->nickname;
            const QString botId = info->appid.isEmpty() ? "未配置AppID" : info->appid;
            const QString statusText = info->online ? "在线" : "离线";
            const QString statusColor = info->online ? "#65B85A" : "#D83B32";
            const QString initial = name.isEmpty() ? "B" : name.left(1).toUpper();

            QIcon menuIcon;
            if (!info->avatarPath.isEmpty() && QFile::exists(info->avatarPath)) {
                QPixmap pix(info->avatarPath);
                if (!pix.isNull()) {
                    menuIcon = QIcon(pix.scaled(28, 28, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                }
            }
            if (menuIcon.isNull()) {
                menuIcon = QIcon(generateBotAvatar(initial, statusColor).scaled(28, 28, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            }

            QAction *act = menu.addAction(menuIcon, QString("%1 · %2  [%3]")
                                                        .arg(name,botId,statusText));

            act->setData(i);
            if (i == m_currentBotIndex) {
                act->setCheckable(true);
                act->setChecked(true);
                QFont f = act->font(); f.setBold(true); act->setFont(f);
            }
        }
        menu.addSeparator();
        QAction *allAct = menu.addAction("全部机器人（显示所有日志）");
        allAct->setData(-1);
        if (m_currentBotIndex < 0) {
            allAct->setCheckable(true); allAct->setChecked(true);
            QFont f = allAct->font(); f.setBold(true); allAct->setFont(f);
        }
    }

    QAction *selected = menu.exec(botStatusWidget->mapToGlobal(QPoint(0, botStatusWidget->height())));
    if (selected && selected->data().isValid()) {
        switchToBot(selected->data().toInt());
    }
}


void MainWindow::switchToBot(int index)
{
    if (index == m_currentBotIndex) return;
    m_currentBotIndex = index;
    m_appliedLogBotId.clear();
    updateCurrentBotInfo();
}

QPixmap MainWindow::generateBotAvatar(const QString &initial, const QString &colorHex) const
{
    const int size = 68;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    p.setBrush(QColor(colorHex));
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(28);
    f.setBold(true);
    p.setFont(f);
    p.drawText(pix.rect(), Qt::AlignCenter, initial.left(1));
    p.end();
    return pix;
}


void MainWindow::onMaximizeClicked()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

// 监听窗口状态变化（最大化/还原时更新按钮图标）
void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (isMaximized()) {
            maxBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
            maxBtn->setToolTip("还原");
        } else {
            maxBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
            maxBtn->setToolTip("最大化");
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (qApp) qApp->quit();
#ifdef Q_OS_WIN
    ::TerminateProcess(::GetCurrentProcess(), 0);
#endif
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (m_kantoumusume) {
        m_kantoumusume->move(width() - m_kantoumusume->width() - 10,
                             height() - m_kantoumusume->height() - 10);
    }
}




// 窗口缩放与侧边栏拖拽
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        if (sideBar && sideBar->geometry().contains(pos)) {
            dragStartPos = event->globalPos() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!resizing && (event->buttons() & Qt::LeftButton) && !dragStartPos.isNull()) {
        move(event->globalPos() - dragStartPos);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resizing = false;
        resizeEdge = Qt::Edges();
        dragStartPos = QPoint();
    }
    QMainWindow::mouseReleaseEvent(event);
}
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == botStatusWidget && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            showBotSelectorMenu();
            return true;
        }
    }
    if (obj == titleBar) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragStartPos = me->globalPos() - frameGeometry().topLeft();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->buttons() & Qt::LeftButton && !dragStartPos.isNull()) {
                move(me->globalPos() - dragStartPos);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            dragStartPos = QPoint();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::checkUpdate() {

    QUrl url("https://gitee.com/api/v5/repos/linglan2/chun-bai-ling-dang/releases/latest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Qt-UpdateChecker/1.0");

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater(); // 自动清理

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "检查更新", "网络请求失败: " + reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            QMessageBox::warning(this, "检查更新", "数据解析错误");
            return;
        }

        QJsonObject obj = doc.object();
        QString remoteTag = obj.value("tag_name").toString();  // 例如 "v1.0.1.12931"
        QString releaseNotes = obj.value("body").toString();   // 更新说明
        const QJsonArray assets = obj.value("assets").toArray();


        int remoteBuild = 0;
        QStringList parts = remoteTag.split('.');
        if (!parts.isEmpty()) {
            QString last = parts.last();
            last.remove(QRegularExpression("[^0-9]")); // 去掉可能的 'v'
            remoteBuild = last.toInt();
        }

        if (remoteBuild > APP_BUILD_NUMBER) {
            showUpdateDialog(remoteTag, releaseNotes);
        } else {
            QMessageBox::information(this, "检查更新", "当前已经是最新版本");
        }
    });

}
#include "netmanager.h"
extern bool __cqkj;
QString checkUpdate(const MessageEvent &ev) {

    auto f = NetManager::instance()->get("https://gitee.com/api/v5/repos/linglan2/chun-bai-ling-dang/releases/latest");
    QByteArray data = f.get();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        return  "JSON数据解析错误";
    }
    QJsonObject obj = doc.object();
    QString remoteTag = obj.value("tag_name").toString();  // 例如 "v1.0.1.12931"
    QString releaseNotes = obj.value("body").toString();   // 更新说明
    const QJsonArray assets = obj.value("assets").toArray();
    int remoteBuild = 0;
    QStringList parts = remoteTag.split('.');
    if (!parts.isEmpty()) {
        QString last = parts.last();
        last.remove(QRegularExpression("[^0-9]")); // 去掉可能的 'v'
        remoteBuild = last.toInt();
    }
    if (remoteBuild > APP_BUILD_NUMBER) {
        __cqkj=true;
        return "#"+remoteTag+"\n>"+releaseNotes+"\n\n---\n\n发送[#确认更新框架]() 来更新 注意更新需要重启 如果更新失败 可能需要手动更新";
    }
    return  "当前已经是最新版本";
}
QString getLatestDownloadUrl() {
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl("https://gitee.com/api/v5/repos/linglan2/chun-bai-ling-dang/releases/latest"));
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return QString();
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return QString();

    QJsonObject obj = doc.object();
    QJsonArray assets = obj["assets"].toArray();

    for (const QJsonValue &val : std::as_const(assets)) {
        QJsonObject asset = val.toObject();
        QString name = asset["name"].toString();
        if (name.contains("linux", Qt::CaseInsensitive) &&
            (name.endsWith(".zip") || name.endsWith(".7z"))) {
            return asset["browser_download_url"].toString();
        }
    }

    return QString();
}

QString startDownloadAndReplace() {
    #ifdef _WIN32
    QString appDir = QCoreApplication::applicationDirPath();
    QString exePath = QDir(appDir).filePath("纯白铃铛-下崽器.exe");
    if (!QFile::exists(exePath))
        return "纯白铃铛-下崽器 不存在 或 运行失败 需要这个才能更新框架";

    std::wstring exe = exePath.toStdWString();
    std::wstring args = L" 啥也没";   // 注意参数前有空格
    std::wstring cmdLine = exe + args;

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, false,
                       CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        QCoreApplication::quit();
        return QString();
    }
    return "启动失败（CreateProcess 返回错误）";
    #else
    QString unzipTool;
    if (QProcess::execute("which", QStringList() << "7z") == 0) {
        unzipTool = "7z";
    } else if (QProcess::execute("which", QStringList() << "unzip") == 0) {
        unzipTool = "unzip";
    } else {
        return "未找到 7z 或 unzip，请安装 p7zip-full（sudo apt install p7zip-full）";
    }

    // 2. 获取下载 URL（自动选择包含 "linux" 的压缩包）
    QString downloadUrl = getLatestDownloadUrl();
    if (downloadUrl.isEmpty())
        return "未找到 Linux 版本的更新包";

    // 3. 创建临时 Shell 脚本
    QString scriptPath = QDir::tempPath() + "/update.sh";
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text))
        return "无法创建临时脚本";

    QString appPath = QCoreApplication::applicationFilePath();
    QString appDir = QCoreApplication::applicationDirPath();

    // 脚本内容（根据检测到的工具生成对应的解压命令）
    QString unzipCmd;
    if (unzipTool == "7z") {
        unzipCmd = "7z x /tmp/update.zip -y -o'%2'";
    } else {
        unzipCmd = "unzip -o /tmp/update.zip -d '%2'";
    }

    QString scriptContent = QString(
                                "#!/bin/bash\n"
                                "sleep 2   # 等待主程序完全退出\n"
                                "echo '下载更新包...'\n"
                                "wget -O /tmp/update.zip '%1' || curl -L -o /tmp/update.zip '%1'\n"
                                "if [ $? -ne 0 ]; then echo '下载失败'; exit 1; fi\n"
                                "echo '解压中...'\n"
                                "%3\n"
                                "if [ $? -ne 0 ]; then echo '解压失败'; exit 1; fi\n"
                                "rm -f /tmp/update.zip\n"
                                "echo '更新完成，重启程序...'\n"
                                "exec '%4' &\n"
                                "exit 0\n"
                                ).arg(downloadUrl, appDir, unzipCmd, appPath);

    script.write(scriptContent.toUtf8());
    script.close();

    // 添加执行权限
    QFile::setPermissions(scriptPath,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadGroup | QFile::ExeGroup |
                              QFile::ReadOther | QFile::ExeOther);

    // 4. 启动脚本（detach）
    if (!QProcess::startDetached("/bin/bash", QStringList() << scriptPath)) {
        QFile::remove(scriptPath);
        return "无法启动更新脚本";
    }

    // 5. 主程序退出
    QCoreApplication::quit();
    return QString(); // 成功
    #endif
    return "其他系统暂时不支持";
}

void MainWindow::showUpdateDialog(const QString &version, const QString &releaseNotes) {
    QDialog dialog(this);
    dialog.setWindowTitle("发现新版本");
    dialog.setMinimumWidth(500);
    dialog.setMinimumHeight(400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *titleLabel = new QLabel(QString("<h3>新版本: %1</h3>").arg(version));
    layout->addWidget(titleLabel);


    QTextEdit *notesEdit = new QTextEdit(&dialog);
    if (releaseNotes.isEmpty()) {
        notesEdit->setPlainText("暂无更新说明。");
    } else {

        notesEdit->setMarkdown(releaseNotes);
    }
    notesEdit->setReadOnly(true);
    layout->addWidget(notesEdit);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *downloadBtn = new QPushButton("前往下载", &dialog);
    QPushButton *cancelBtn = new QPushButton("以后再说", &dialog);
    btnLayout->addStretch();
    btnLayout->addWidget(downloadBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);


    connect(downloadBtn, &QPushButton::clicked, [this, &dialog]() {
        // 1. 确认更新
        QMessageBox::StandardButton reply = QMessageBox::warning(&dialog,
                                                                 "确认更新",
                                                                 "更新需要关闭当前框架，确认更新吗？",
                                                                 QMessageBox::Yes | QMessageBox::No
                                                                 );
        if (reply != QMessageBox::Yes) return;

        dialog.accept();

        QString text = startDownloadAndReplace();
        if(!text.isEmpty())
            QMessageBox::warning(this,"错误",text);
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void MainWindow::applyStyleSheet()
{
    setStyleSheet(R"(
        QMainWindow {
            background: transparent;
        }
        QWidget#centralRoot {
            background: #FFF8EF;
            border-radius: 10px;
        }
        QWidget {
            color: #263241;
            font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
            font-size: 13px;
        }
        QWidget#titleBar {
            background: #FFF8EF;
            border: none;
            border-top-left-radius: 5px;
            border-top-right-radius: 5px;
        }
        QWidget#botStatusWidget {
            background: transparent;
            border: 1px solid #F2E8DE;
            border-radius: 10px;
        }
        QWidget#botStatusWidget:hover {
            background: #FFF7EA;
            border: 1px solid #FFCF9F;
        }
        QLabel#titleAvatar {
            background: #8A94A6;
            border-radius: 10px;
            color: white;
            font-size: 16px;
            font-weight: bold;
        }
        QLabel#titleUserName {
            color: #263241;
            font-weight: 700;
            font-size: 12px;
            background: transparent;
        }
        QLabel#titleOnline {
            color: #65B85A;
            font-size: 11px;
            background: transparent;
        }
        QWidget#contentWidget {
            background: #F7EFE5;
            border-bottom-left-radius: 10px;
            border-bottom-right-radius: 10px;
        }
        QWidget#sideBar {
            background: #FEFEFC;
            border-right: 1px solid #F4E8DA;
            border-top-right-radius: 10px;
            border-bottom-left-radius: 10px;
        }
        QLabel#brandLogoLabel {
            background: transparent;
            border: none;
        }
        QPushButton#navBtn {
            background: transparent;
            border: none;
            border-radius: 6px;
            color: #687589;
            font-size: 14px;
            font-weight: 600;
            padding: 0px 16px;
            text-align: left;
        }
        QPushButton#navBtn:hover {
            background: #FFF6EA;
            color: #FF914D;
        }
        QPushButton#navBtn:checked {
            background: #FFF0DE;
            color: #FF7F32;
        }
        QLabel#mascotImage {
            background: transparent;
            border: none;
        }
        QStackedWidget#contentStack {
            background: #F7EFE5;
            border: none;
            border-bottom-right-radius: 5px;
        }
        QFrame, QGroupBox {
            background: #FFFFFF;
            border: none; /* 移除残余硬边框 */
            border-radius: 5px;
        }
        QGroupBox {
            margin-top: 14px;
            padding-top: 18px;
            font-weight: 700;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 14px;
            padding: 0 8px;
            color: #263241;
        }
        QListWidget, QListView, QScrollArea {
            background: #FFFFFF;
            border: 1px solid #e0e0e0;   /* 宽度1px，实线，黑色 */
            border-radius: 4px;
            outline: none;
        }
        QListWidget::item, QListView::item {
            border: none;
            color: #596579;
        }
        QListWidget::item:selected, QListView::item:selected {
            background: #FFF0DE;
            color: #FF7F32;
            border-radius: 4px;
        }
        QListWidget::item:hover, QListView::item:hover {
            background: #FFF7EA;
            border-radius: 6px;
            /*background: transparent;*/
        }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox {
            background: #FeFeFe;

            border-radius: 4px;
            padding: 6px 10px;
            border: 1px solid #E0E0E0;   /* 宽度1px，实线，黑色 */
            selection-background-color: #FFB066;
            selection-color: #FFFFFF;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus {
            background: #FFFFFF;
            border: 1px solid #FFB066;
        }
        /* 新增 placeholder 颜色 */
        QLineEdit::placeholder, QTextEdit::placeholder, QPlainTextEdit::placeholder {
            color: #AfAfAf;
        }
        QPushButton {
            border: none;
            border-radius: 6px;
            padding: 6px 12px;
            background: #FFF0DE;
            color: #FF7F32;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #FFE5C8;
        }
        QPushButton:pressed {
            background: #FFD7A8;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 4px 2px 4px 2px;
        }
        QScrollBar::handle:vertical {
            background: #E7D9C8;
            border-radius: 4px;
            min-height: 40px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QComboBox {
            border: 1px solid #ccc;
            border-radius: 6px;
            padding: 5px 30px 5px 10px; /* 为按钮腾出右侧空间 */
            background: white;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 25px;
            border-left: 1px solid #ccc;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
            background: #f0f0f0;
        }
        QComboBox::down-arrow {
            image: url(:/arrow_down.png); /* 或自定义 */
            width: 12px;
            height: 12px;
        }
        /* 悬停效果 */
        QComboBox::drop-down:hover {
            background: #e0e0e0;
        }
#tagLabel {
    background: transparent !important;
    color: #1E90FF;
    font-size: 11px;
    padding: 0px;
    border: none;
}
        QTableWidget { border: 1px solid #AE8AB1; gridline-color: #d0d0d0; }
        QHeaderView::section { background-color: #f5f5f5; border: 1px solid #d0d0d0; }
    )");
}
