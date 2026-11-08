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
//#include "lmdbbrowserwidget.h"
#include "nickreviewwidget.h"
#include "pluginpage.h"
#include <QGraphicsOpacityEffect>
#include "logpage.h"
#include "qq_bind_login.h"
#include "scheduleconfigwidget.h"
#include "screenshotsyncclient.h"
#include "botruleconfigwidget.h"

#include "chatpage.h"
#include "forbiddenwordpage.h"
#include "helppage.h"
#include "global.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QTabBar>
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
#include <QPainterPath>
#include <QFile>
#include <QtCore>
#include <qgroupbox.h>
#include "menupanelwidget.h"
#include "plts.h"
#include "qunguan.h"
#include "set.h"
#include "themecolors.h"
#include "textreplaceconfigwidget.h"
#include "keywordmatchconfigwidget.h"
#include <QNetworkReply>
#include <QProgressDialog>
#include <qmessagebox.h>
#include <QDialog>
#include <QGuiApplication>
#include <QApplication>     // qApp->installEventFilter / setOverrideCursor
#include <QPushButton>
#include <QTextBrowser>     // ⚠ 不能用 QTextEdit：本 TU 里被 #define 成 PlaceholderTextEdit
#include <QScreen>


#define APP_VERSION_STR "v1.5.6.188"
#define APP_BUILD_NUMBER 188
QStackedWidget *stackedWidget=nullptr;
QString Homev=R"(
# 更新日志🌸
## v1.5.6.188 (2026-11-08)
- 修复 msgid判定范围导致 机器人加群msgid无效问题
- 优化 数据库清理机制
- 优化 linux 运行 之前没测试现在运行会强制开webhook 和ws端口
- 优化 首页显示的内存占用
- 增加 更新 部分webui内容

## v1.5.5.183 (2026-11-05)
- 更新 新的UI 移除部分界面 黑名单改为分bot执行了
- 修复 事件用户id获取失败问题
- 增加 #^强制普通信息 #$强制MD信息
- 增加 拖动窗口调整大小(实际上是改了好久的)
- 增加 快速重启按钮
- 修复 纯白铃32.exe zyjson不支持多线程问题

## v1.5.3.176 (2026-11-02)
- 优化 问答 按钮类 行高过高问题
- 修复 全局样式 倒是输入框 提示灰字不可用问题
- 增加 签到指令 每次1000积分
- 修复 入群提醒 重复提醒无效问题
- 增加 应该 插件缓存格式 plugin:{data}|本地路径 下次只传 plugin:{data} 就可以使用缓存 例子![img](plugin:{data}|xxx) 支持语音 视频 文件 plugin:{data} 不可用时直接返回应该json 因此易语言 等不可用 JS异步可用
- 修复 一条消息秒回复两条时 有一条小概率失败 有概率推迟问题
- 增加 附加按钮开关 私聊环境可用

## v1.5.2.169 (2026-11-02)
- 增加 插件测试 一键测试注入 用于检查指令是否会复读用户指令
- 修复 多线程共用应该静态变量问题 改为线程绑定
- 增加 广告奖励 商店 插件注册后可购买 完善sdk系统 等开放 这个是预先完善
- 优化 ai相关问题
- 修复 机器人入群提示不可用问题
- 尝试 优化机器人重连机制(部分清空下无法重连问题) 5池重试改 无限次 最大15分钟一次
- 修复 数据库在高并发环境可能出问题
- 修复 入群欢迎 被ai修改为使用主动问题
- 优化 机器人入群欢迎 分离出 群私聊
- 修复 用户入群退群 欢迎 变量错误 ai搞的
- 增加 插件sdk 自动撤回 方便插件进行自动撤回
- 优化 消息尾巴 不再附加在 富媒体文件上
- 优化 插件市场增加一键更新

## v1.5.1.156 (2026-10-26)
- 优化 ai拟人 提示词 让他不再强制性格
- 增加 图片不存在替补图片
- 优化 内置ai 现在如果有管理员权限 可以撤回消息 禁言某人
- 增加 新的UI界面 以及ai可以调插件内容了
- 优化 问答 按钮 等 表格可以 多行输入了

...历史更新记录太长被省略...
)";

// 全局指针（保持与你原有代码一致）
HomePage *homePage = nullptr;
set *setA=nullptr;
AccountPage *accountPage = nullptr;
LogPage *logPage = nullptr;
PluginPage *pluginPage = nullptr;
ChatPage *chatPage = nullptr;

ButtonEditor *buttonEditorPage=nullptr;
BotRuleConfigWidget *RuleConfigWidget=nullptr;
TextReplaceConfigWidget *TextReplace=nullptr;
KeywordMatchConfigWidget *keyword=nullptr;
KeywordPunishConfigWidget *keyword_Punish=nullptr;
BlacklistPage *Black=nullptr;
ForbiddenWordPage *forbidden=nullptr;
HelpPage *helpPage=nullptr;          // 顶部导航「帮助」：一段可编辑的引导文本
ScheduleConfigWidget *schedule=nullptr;
ScreenshotSyncClient *ScreenA=nullptr;
AiWidget *ai_ui = nullptr;
qunguan *ui_qunguan=nullptr;
MenuPanelWidget *ui_MenuPanel=nullptr;

// 账号（配置目标）现在统一由标题栏的机器人选择器决定，顶部那条「机器人 [下拉]」已删除
QListWidget *configNav=nullptr;          // 高级配置页：二级竖排导航
QStackedWidget *configStack=nullptr;     // 高级配置页：二级内容区
NickReviewWidget *m_nickReviewWidget=nullptr;
MaterialWidget *material_ui=nullptr;

bool 强制审核昵称=false;
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
//刷屏检测
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

namespace {

// ── 左侧二级导航的图标 ──
// 13 个纯文字按钮并排，靠文字找页面很费眼，所以每项配一个轮廓差异明显的小图标。
// 画法和标题栏那套一致：16×16 坐标系、1.4px 圆头细线，Normal / hover / 选中 三态各出一张图。
enum NavGlyph {
    NavAi, NavMenu, NavCmd, NavMedia, NavBase, NavFaq, NavButton,
    NavBan, NavReview, NavPush, NavReplace, NavClock, NavRecall
};

QPixmap navGlyphPixmap(NavGlyph glyph, const QColor &color, int px)
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 16.0, px / 16.0);        // 统一在 16×16 里画，尺寸不同只改 px
    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (glyph) {
    case NavAi: {                        // 四角星
        QPainterPath star;
        star.moveTo(8.0, 2.2);
        star.quadTo(9.4, 6.6, 13.8, 8.0);
        star.quadTo(9.4, 9.4, 8.0, 13.8);
        star.quadTo(6.6, 9.4, 2.2, 8.0);
        star.quadTo(6.6, 6.6, 8.0, 2.2);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(star);
        break;
    }
    case NavMenu:                        // 三条横线
        p.drawLine(QPointF(3.0, 4.6), QPointF(13.0, 4.6));
        p.drawLine(QPointF(3.0, 8.0), QPointF(13.0, 8.0));
        p.drawLine(QPointF(3.0, 11.4), QPointF(10.6, 11.4));
        break;
    case NavCmd: {                       // 终端窗口：方框 + > _
        p.drawRoundedRect(QRectF(2.6, 3.6, 10.8, 8.8), 2.0, 2.0);
        QPainterPath arrow;
        arrow.moveTo(5.4, 6.4);
        arrow.lineTo(7.4, 8.0);
        arrow.lineTo(5.4, 9.6);
        p.drawPath(arrow);
        p.drawLine(QPointF(8.8, 9.6), QPointF(10.6, 9.6));
        break;
    }
    case NavMedia: {                     // 图片：框 + 太阳 + 山
        p.drawRoundedRect(QRectF(2.6, 3.6, 10.8, 8.8), 2.0, 2.0);
        p.drawEllipse(QPointF(5.8, 6.4), 1.0, 1.0);
        QPainterPath hill;
        hill.moveTo(3.5, 11.5);
        hill.lineTo(6.3, 8.6);
        hill.lineTo(8.4, 10.7);
        hill.lineTo(9.8, 9.3);
        hill.lineTo(12.5, 11.5);
        p.drawPath(hill);
        break;
    }
    case NavBase: {                      // 滑块（设置）
        const qreal ys[3] = {4.8, 8.0, 11.2};
        const qreal xs[3] = {9.6, 6.2, 10.6};
        for (int i = 0; i < 3; ++i) {
            p.drawLine(QPointF(2.8, ys[i]), QPointF(xs[i] - 2.0, ys[i]));
            p.drawLine(QPointF(xs[i] + 2.0, ys[i]), QPointF(13.2, ys[i]));
            p.drawEllipse(QPointF(xs[i], ys[i]), 1.5, 1.5);
        }
        break;
    }
    case NavFaq: {                       // 对话气泡（问答）
        p.drawRoundedRect(QRectF(2.6, 3.2, 10.8, 7.6), 2.4, 2.4);
        QPainterPath tail;
        tail.moveTo(5.4, 10.8);
        tail.lineTo(5.4, 13.2);
        tail.lineTo(7.8, 10.8);
        p.drawPath(tail);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(5.9, 7.0), 0.75, 0.75);
        p.drawEllipse(QPointF(8.0, 7.0), 0.75, 0.75);
        p.drawEllipse(QPointF(10.1, 7.0), 0.75, 0.75);
        break;
    }
    case NavButton:                      // 胶囊按钮
        p.drawRoundedRect(QRectF(2.2, 5.2, 11.6, 5.6), 2.8, 2.8);
        p.drawLine(QPointF(6.2, 8.0), QPointF(9.8, 8.0));
        break;
    case NavBan:                         // 禁止圈
        p.drawEllipse(QPointF(8.0, 8.0), 5.2, 5.2);
        p.drawLine(QPointF(4.3, 11.7), QPointF(11.7, 4.3));
        break;
    case NavReview: {                    // 审核：卡片 + 对勾
        p.drawRoundedRect(QRectF(2.6, 3.4, 10.8, 9.2), 2.0, 2.0);
        QPainterPath check;
        check.moveTo(5.2, 8.4);
        check.lineTo(7.2, 10.3);
        check.lineTo(11.0, 5.9);
        p.drawPath(check);
        break;
    }
    case NavPush: {                      // 纸飞机（推送）
        QPainterPath plane;
        plane.moveTo(2.6, 8.0);
        plane.lineTo(13.4, 3.0);
        plane.lineTo(9.8, 13.0);
        plane.lineTo(7.3, 9.1);
        plane.closeSubpath();
        p.drawPath(plane);
        p.drawLine(QPointF(13.4, 3.0), QPointF(7.3, 9.1));
        break;
    }
    case NavReplace: {                   // 双向箭头（文本替换）
        p.drawLine(QPointF(3.2, 6.3), QPointF(12.8, 6.3));
        QPainterPath a1;
        a1.moveTo(10.8, 4.3);
        a1.lineTo(12.8, 6.3);
        a1.lineTo(10.8, 8.3);
        p.drawPath(a1);
        p.drawLine(QPointF(12.8, 9.7), QPointF(3.2, 9.7));
        QPainterPath a2;
        a2.moveTo(5.2, 7.7);
        a2.lineTo(3.2, 9.7);
        a2.lineTo(5.2, 11.7);
        p.drawPath(a2);
        break;
    }
    case NavClock:                       // 时钟（订阅 / 定时）
        p.drawEllipse(QPointF(8.0, 8.0), 5.3, 5.3);
        p.drawLine(QPointF(8.0, 8.0), QPointF(8.0, 5.1));
        p.drawLine(QPointF(8.0, 8.0), QPointF(10.6, 9.4));
        break;
    case NavRecall: {                    // 回头的箭头（撤回）
        QPainterPath path;
        path.moveTo(12.6, 11.6);
        path.cubicTo(12.6, 6.4, 7.4, 4.4, 4.6, 6.2);
        p.drawPath(path);
        QPainterPath tip;
        tip.moveTo(7.6, 4.2);
        tip.lineTo(3.6, 6.4);
        tip.lineTo(7.6, 8.4);
        p.drawPath(tip);
        break;
    }
    }
    return pm;
}

// normal = 常态，active = hover，selected = 选中（选中项文字是橙色的，图标跟着变）
QIcon navGlyphIcon(NavGlyph glyph, const QColor &normal,
                   const QColor &active, const QColor &selected)
{
    QIcon icon;
    const int px = 16 * 2;               // 2 倍图，高 DPI 下不糊
    auto add = [&](const QColor &c, QIcon::Mode mode) {
        QPixmap pm = navGlyphPixmap(glyph, c, px);
        pm.setDevicePixelRatio(2.0);
        icon.addPixmap(pm, mode, QIcon::Off);
    };
    add(normal, QIcon::Normal);
    add(active, QIcon::Active);
    add(selected, QIcon::Selected);
    return icon;
}

// ── 窗口外圈的「阴影带」──
// 系统窗口的拖边热区其实不在内容里：它在窗口自带的不可见边框 / DWM 阴影上
// （那圈东西位于窗口矩形内部、可见内容之外）。FramelessWindowHint 窗口默认没有这圈，
// 热区只能往内容里挤，手感就是「得进到窗口里面才拖得动」。
// 所以给 central 外面套一层透明根、内缩 kShadowMargin：这一圈既画柔和阴影，也正好拿来当热区。
constexpr int kShadowMargin = 8;

class WindowShadowRoot : public QWidget
{
public:
    explicit WindowShadowRoot(QWidget *parent = nullptr) : QWidget(parent)
    {
        // 阴影自己画，底必须透明（主窗口已设 WA_TranslucentBackground）
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

    // 最大化 / 全屏时把这一圈收掉，否则四边会漏出桌面和阴影
    void setShadowMargin(int m)
    {
        if (m == m_margin) return;
        m_margin = m;
        if (QLayout *l = layout()) l->setContentsMargins(m, m, m, m);
        update();
    }

    int shadowMargin() const { return m_margin; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_margin <= 0) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        // 从外到内叠几层半透明圆角矩形，近似 box-shadow：越靠内容层数越多、越浓。
        // 注意这些层是叠加的：最内侧会被全部 m_margin 层覆盖，所以单层 alpha 必须很小，
        // 峰值实际是各层之和（这里约 30/255 ≈ 12%），否则会糊成一圈死黑。
        const QRectF inner = QRectF(rect()).adjusted(m_margin, m_margin, -m_margin, -m_margin);
        for (int i = m_margin; i >= 1; --i) {
            const qreal t = 1.0 - qreal(i - 1) / qreal(m_margin);      // 0（最外）→ 1（最内）
            p.setBrush(QColor(0, 0, 0, int(1 + 7 * t * t)));           // 平方衰减：外圈几乎透明
            p.drawRoundedRect(inner.adjusted(-i, -i, i, i), 10 + i, 10 + i);
        }
    }

private:
    int m_margin = 0;
};

} // namespace

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent), resizing(false), edgeMargin(kShadowMargin)
{
    // 无边框窗口
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    // 内容区外圈额外留出 kShadowMargin*2：可见内容仍和以前一样大
    resize(1040 + kShadowMargin * 2, 660 + kShadowMargin * 2);
    setMinimumSize(900 + kShadowMargin * 2, 560 + kShadowMargin * 2);
    setWindowTitle("纯白铃铛");

    setupUi();
    xr();
    applyStyleSheet();

    // 无边框窗口的边框缩放：装到 qApp 上，才能拿到被各级子控件吃掉的鼠标移动/按下事件。
    // 过滤器内部第一件事就是判 w->window() == this，别的窗口/对话框一律不管。
    qApp->installEventFilter(this);

    // 界面配色（高级设置 → 基础设置）改一个色就重新套一遍样式表
    ThemeColors::setApplyHook([this]() { applyStyleSheet(); });



    // 默认选中首页
    btnHome->setChecked(true);
    stackedWidget->setCurrentIndex(0);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(3000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if(框架退出) return;
        QQBindLogin &mgr = QQBindLogin::instance();
        mgr.poll();

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
        bridge->writeResponseToBlock(0, "{\"type\":7}");
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
            int appid = acc->appid_int;
            if(g_botdb.contains(appid)){
                g_botdb[appid]->cleanExpiredJojiyzCache(5,appid);
            }
            if(m_botClients.contains(appid)){
                m_botClients[appid]->onRefreshReplyFinished();
            }
            cleanInactiveUsers(&acc->stat, now, 30 * 60 * 1000); // 30分钟
        }


    });
    cleanTimer->start(30000); // 每30秒检查一次

}

void MainWindow::xr()
{
    m_kantoumusume = new QLabel(this);
    QPixmap pixmap(":/icons/qiancao1.png");
    m_kantoumusume->setPixmap(pixmap);
    m_kantoumusume->resize(pixmap.size());
    QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect();
    m_kantoumusume->setStyleSheet("background: transparent;");
    effect->setOpacity(0.2);
    m_kantoumusume->setGraphicsEffect(effect);
    m_kantoumusume->move(width() - m_kantoumusume->width() - 10,
                       height() - m_kantoumusume->height() + 40);
    m_kantoumusume->setAttribute(Qt::WA_TransparentForMouseEvents);
}

// ══════════════════════════════════════════════════════════════════════════
//  弹窗 2 的正文 —— 第三方组件与许可
//  （内容长，单独一个可滚动窗口装，见下面的 showThirdPartyLicenseDialog）
// ══════════════════════════════════════════════════════════════════════════
static QString thirdPartyLicenseHtml()
{
    // --- 根据平台选择 OpenSSL 许可证文本 ---
#ifdef Q_OS_WIN
    // Windows 下使用 OpenSSL 1.1.1（OpenSSL + SSLeay 双重许可）
    const QString opensslLicenseText =
        "<b>OpenSSL 1.1.1</b>（动态链接）<br>"
        "Copyright (c) 1998-2023 The OpenSSL Project and others.<br>"
        "采用 <a href=\"https://www.openssl.org/source/license-openssl-ssleay.txt\">"
        "OpenSSL License + SSLeay License</a>（BSD 风格）<br>"
        "许可证要求：分发时需保留版权声明，并在文档中致谢 OpenSSL Project。<br>"
        "OpenSSL 源代码：<a href=\"https://www.openssl.org/source/\">"
        "https://www.openssl.org/source/</a><br><br>";
#else
    // Linux 下使用 OpenSSL 3.0.2（Apache 2.0）
    const QString opensslLicenseText =
        "<b>OpenSSL 3.0.2</b>（动态链接）<br>"
        "Copyright (c) 1998-2023 The OpenSSL Project and others.<br>"
        "采用 <a href=\"https://www.apache.org/licenses/LICENSE-2.0\">"
        "Apache License, Version 2.0</a><br>"
        "OpenSSL 源代码：<a href=\"https://www.openssl.org/source/\">"
        "https://www.openssl.org/source/</a><br><br>";
#endif

    return QString(
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

        // --- 插入 OpenSSL 许可证（已根据平台选择不同文本）---
        + opensslLicenseText +

        "<b>FFmpeg</b>（LGPL-2.1）<br>"
        "音频的时长探测 / 转码 / 视频提取音频 / 超长切段由同目录下的 "
        "<code>ffmpeg.dll</code> 完成。该 DLL 由本项目基于 FFmpeg 官方源码裁剪而来"
        "（只保留音频解封装、解码、重采样与编码，<b>非</b> GPL 构建）。<br>"
        "• 它<u>不是</u>链接进本程序的静态库：程序用动态加载方式在需要时才打开它，"
        "所以你可以直接用自己的 FFmpeg 构建替换该文件（只要导出 <code>qc_*</code> 那几个函数），"
        "删掉它程序也照常启动。<br>"
        "• LGPL 义务：提供 FFmpeg 完整源代码与构建脚本（见仓库 <code>libs/ffmpeg/</code>，"
        "含 <code>qc_ffmpeg.c</code>、<code>build-win.sh</code>、configure 白名单）。<br>"
        "• 没有 <code>ffmpeg.dll</code> 时，程序会尝试从发布页补齐；补不上则退回调用外部 "
        "<code>ffmpeg.exe</code>（若你自行放在程序目录或 PATH 中）。<br>"
        "FFmpeg 源代码：<a href=\"https://ffmpeg.org/download.html\">https://ffmpeg.org/download.html</a><br><br>"
        );
}

// ══════════════════════════════════════════════════════════════════════════
//  弹窗 2：第三方组件与许可 —— 可滚动 / 可点链接 / 可选中复制
//  用 QTextBrowser 而不是 QMessageBox，是因为 QMessageBox 的高度会被屏幕
//  「挤」住：这一大段以前全塞在一个 QMessageBox 里，小屏直接显示不全且不能滚动。
// ══════════════════════════════════════════════════════════════════════════
static void showThirdPartyLicenseDialog()
{
    QDialog dlg;
    dlg.setWindowTitle("第三方组件与许可");
    dlg.setModal(true);

    // 期望尺寸，再按屏幕可用区域收窄（小屏 / 高分屏缩放都不会顶出屏幕）
    QSize want(860, 640);
    if (const QScreen *scr = QGuiApplication::primaryScreen()) {
        const QSize avail = scr->availableGeometry().size();
        want.setWidth(qMin(want.width(), qMax(480, avail.width() - 80)));
        want.setHeight(qMin(want.height(), qMax(360, avail.height() - 100)));
    }
    dlg.resize(want);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);

    QTextBrowser *browser = new QTextBrowser(&dlg);
    browser->setOpenExternalLinks(true);   // 点链接直接交给系统浏览器
    browser->setHtml(thirdPartyLicenseHtml());
    mainLayout->addWidget(browser, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    QLabel *hint = new QLabel("链接可直接点击，正文可选中复制", &dlg);
    // ⚠ 全局样式表里有 QFrame{background:#FFFFFF}，会连带命中 QLabel（QLabel 继承 QFrame）
    //   → 必须在自己身上声明透明，否则是白底方块
    hint->setStyleSheet("color:#888; background: transparent;");
    btnLayout->addWidget(hint);
    btnLayout->addStretch();

    QPushButton *closeBtn = new QPushButton("关闭", &dlg);
    closeBtn->setDefault(true);
    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    dlg.exec();
}

// ══════════════════════════════════════════════════════════════════════════
//  弹窗 1：关于（短）—— 项目主体 + 插件合规 + 其他资源
//  底部多一个「第三方组件许可」按钮，点了才弹上面那个长窗口
// ══════════════════════════════════════════════════════════════════════════
void showClickableLicenseInfo()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("关于");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(
        "<h3>纯白铃铛 - QQ 机器人管理平台</h3>"
        "本项目主体采用 <a href=\"https://www.gnu.org/licenses/lgpl-3.0.html\">LGPLv3 协议</a> 开源。<br>"
        "完整源代码（含所有修改）请访问：<br>"
        "<a href=\"https://github.com/qiancao1/chun_bai_ling\">GitHub</a> 或 "
        "<a href=\"https://gitee.com/linglan2/chun-bai-ling-dang\">Gitee</a><br><br>"

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

        "本软件为免费开源项目，仅供学习交流使用。<br>"
        "禁止灰黑赌博违法使用<br>"
        "<i>本程序使用的第三方组件及其许可证，点下方「第三方组件许可」查看。</i>");
    msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);

    QPushButton *licenseBtn = msgBox.addButton("第三方组件许可", QMessageBox::ActionRole);
    QPushButton *closeBtn   = msgBox.addButton("关闭", QMessageBox::AcceptRole);
    msgBox.setDefaultButton(closeBtn);
    msgBox.setEscapeButton(closeBtn);   // 按 Esc 只关「关于」，不会误弹长窗口

    msgBox.exec();
    if (msgBox.clickedButton() == licenseBtn)
        showThirdPartyLicenseDialog();
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
    delete helpPage;
    helpPage = nullptr;
    delete Black;
    Black = nullptr;

    delete buttonEditorPage;
    buttonEditorPage = nullptr;


    delete ui_qunguan;
    ui_qunguan = nullptr;



    // 这些可能是成员指针，由父窗口管理，但为了安全也清理
    delete configNav;
    configNav = nullptr;
    delete configStack;
    configStack = nullptr;

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

    // 解掉配色回调（捕获的是 this，别留下悬空引用）
    ThemeColors::setApplyHook(nullptr);
}

bool _g_qieh=false;
// 切到「聊天」页。直接借导航按钮的 clicked 走一遍，把 btnGroup 互斥、stackedWidget 切换、
// logPage->setActive(false)、chatPage->存在 这些副作用一次带全 —— 手工在这儿重写那几句容易漏。
void MainWindow::gotoChatPage()
{
    if (btnChat) btnChat->click();
}

void MainWindow::setupUi()
{

    ScreenA= new ScreenshotSyncClient;
    networkManager = new QNetworkAccessManager(this);
    homePage = new HomePage;
    accountPage = new AccountPage;
    logPage = new LogPage(this);
    pluginPage = new PluginPage;
    chatPage = new ChatPage;

    setA = new set;
    buttonEditorPage = new ButtonEditor;
    RuleConfigWidget = new BotRuleConfigWidget;
    TextReplace = new TextReplaceConfigWidget;
    keyword = new KeywordMatchConfigWidget;
    keyword_Punish = new KeywordPunishConfigWidget;
    Black = new BlacklistPage;
    forbidden = new ForbiddenWordPage;
    helpPage = new HelpPage;
    schedule =new ScheduleConfigWidget;
    // HTML制图页已下线，不再创建 HtmlToImageWidget


    m_nickReviewWidget = new NickReviewWidget(this);
    // 素材页：每个机器人各自一套（配额/链接/有效期都按 appid 隔离），切号时靠 setAppId 换数据



    ai_ui = new AiWidget;//要慢于其他使用ai的功能创建
    ui_qunguan = new qunguan;
    ui_MenuPanel = new MenuPanelWidget;





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





    QWidget *nwid = new QWidget;
    QVBoxLayout *nwLayout = new QVBoxLayout(nwid);        // 竖导航 + 内容（账号选择器已在顶部导航条）
    nwLayout->setContentsMargins(10, 10, 10, 10);
    nwLayout->setSpacing(8);

    // ===== 主体：左侧二级竖排导航 + 右侧内容（替代原横向 10 个 Tab） =====
    QHBoxLayout *bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(10);

    configNav = new QListWidget;
    configNav->setObjectName("configNav");
    configNav->setFixedWidth(126);           // 从 112 加宽：要给 14px 图标留位置，否则「关键词撤回」会被省略
    configNav->setIconSize(QSize(16, 16));   // 与自绘坐标系 1:1，避免缩放发虚
    configNav->setFrameShape(QFrame::NoFrame);
    configNav->setCursor(Qt::PointingHandCursor);
    configNav->setSpacing(2);
    configNav->setUniformItemSizes(true);
    configNav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    configStack = new QStackedWidget;
    configStack->setObjectName("configStack");

    // 「菜单 / 指令 / 素材」原本挤在 MenuPanelWidget 内部的 QTabWidget 里（当时位置不够），
    // 现在拆成左侧导航三项：三个 nav 行都指向同一个 stack 页，只切内层 tab。
    if (ui_MenuPanel->m_tabWidget && ui_MenuPanel->m_tabWidget->tabBar())
        ui_MenuPanel->m_tabWidget->tabBar()->hide();
    // 内层选择夹已经由左侧导航接管，这里去掉它自己的边框，避免和内容卡片双层描边
    if (ui_MenuPanel->m_tabWidget) {
        ui_MenuPanel->m_tabWidget->setStyleSheet(
            "QTabWidget::pane { border: none; background: transparent; top: 0px; }");
    }



    // UserRole = stack 页下标；UserRole+1 = 页内子 tab 下标（-1 表示普通页）
    // 图标三态取色与 QSS 一致：常态 #687589 系、hover 橙、选中深橙
    auto addNavRow = [](const QString &title, int stackIndex, int subTab, NavGlyph glyph) {
        QListWidgetItem *navItem = new QListWidgetItem(title, configNav);
        navItem->setIcon(navGlyphIcon(glyph, QColor(0x8A94A6),
                                      QColor("#FF914D"), QColor("#FF7F32")));
        navItem->setSizeHint(QSize(0, 36));
        navItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        navItem->setData(Qt::UserRole, stackIndex);
        navItem->setData(Qt::UserRole + 1, subTab);
    };
    auto addConfigPage = [&addNavRow](const QString &title, QWidget *page, NavGlyph glyph) {
        addNavRow(title, configStack->addWidget(page), -1, glyph);
    };

    addConfigPage("Ai", ai_ui, NavAi);
    const int menuPanelStackIndex = configStack->addWidget(ui_MenuPanel);
    // 子 tab 下标必须跟着 m_tabWidget 实际的 addTab 顺序走（0=菜单 / 1=指令 / 2=素材），
    // 不能从 1 起数：越界时 setCurrentIndex 静默无效，就会停在上一个 tab 上（点「指令」却显示「素材」）。
    addNavRow("菜单", menuPanelStackIndex, 0, NavMenu);
    addNavRow("指令", menuPanelStackIndex, 1, NavCmd);
    addNavRow("素材", menuPanelStackIndex, 2, NavMedia);
    addConfigPage("基础", ui_qunguan, NavBase);
    addConfigPage("问答", keyword, NavFaq);
    addConfigPage("按钮", RuleConfigWidget, NavButton);
    addConfigPage("黑名单", Black, NavBan);     // 原顶部导航的「黑名单」，移到这里（数据本来就按账号隔离）
    addConfigPage("昵称审核", m_nickReviewWidget, NavReview);
    addConfigPage("批量推送", myPlts, NavPush);
    addConfigPage("文本替换", TextReplace, NavReplace);
    addConfigPage("订阅|定时", schedule, NavClock);
    addConfigPage("关键词撤回", keyword_Punish, NavRecall);


    connect(configNav, &QListWidget::currentRowChanged, this,
            [this](int row){
                if (row < 0 || !configStack) return;
                QListWidgetItem *navItem = configNav->item(row);
                if (!navItem) return;
                const int stackIndex = navItem->data(Qt::UserRole).toInt();
                const int subTab = navItem->data(Qt::UserRole + 1).toInt();
                configStack->setCurrentIndex(stackIndex);
                if (subTab >= 0 && ui_MenuPanel->m_tabWidget)
                    ui_MenuPanel->m_tabWidget->setCurrentIndex(subTab);

                // 只有「菜单」页需要在切账号时重新拉取面板数据
                if (subTab != 0) {
                    _g_qieh = true;
                    return;
                }
                if(!ui_MenuPanel->m_botClient)
                {
                    _g_qieh=true;
                }
                if(_g_qieh){
                    _g_qieh=false;
                    ui_MenuPanel->switchBot();

                }
    });

    // 初始选中「菜单」，但不触发切换逻辑（与原来第一个 tab 的情形一致）
    {
        QSignalBlocker navBlocker(configNav);
        configNav->setCurrentRow(0);
    }
    configStack->setCurrentIndex(0);
    if (ui_MenuPanel->m_tabWidget)
        ui_MenuPanel->m_tabWidget->setCurrentIndex(0);

    bodyLayout->addWidget(configNav);
    bodyLayout->addWidget(configStack, 1);
    nwLayout->addLayout(bodyLayout, 1);


    // 原「高级配置」这一层（configGroupBox + 6 个 tab）已经取消：
    // 基础设置 / 按钮生成 / 内置功能 / 黑名单管理 / 违禁词过滤 直接变成顶部导航项，
    // HTML制图页按需求下线。原来挂在 configTabWidget::currentChanged 上的
    // 「cnb / cos 临时停用」提示逻辑，已挪到 btnGroup 的 idClicked（id==5）。



    // ========== 堆叠窗口 ==========
    stackedWidget = new QStackedWidget;
    stackedWidget->addWidget(homePage);          // 0 首页
    stackedWidget->addWidget(accountPage);       // 1 账号
    stackedWidget->addWidget(logPage);           // 2 日志
    stackedWidget->addWidget(pluginPage);        // 3 插件
    stackedWidget->addWidget(chatPage);          // 4 聊天
    stackedWidget->addWidget(setA);              // 5 基础
    stackedWidget->addWidget(buttonEditorPage);  // 6 按钮
    stackedWidget->addWidget(nwid);              // 7 内置（黑名单已并入其中的左侧导航）
    stackedWidget->addWidget(forbidden);         // 8 违禁词
    stackedWidget->addWidget(helpPage);          // 9 帮助（可编辑的引导文本）

    stackedWidget->setObjectName("contentStack");


    // ========== 顶部导航条（原左侧 sideBar 整体挪到标题栏下面） ==========
    navBar = new QWidget;
    navBar->setObjectName("navBar");
    navBar->setAttribute(Qt::WA_StyledBackground, true);
    navBar->setFixedHeight(32);          // 按钮 30 + 上下各 1px，选中态的底部橙线才贴住导航条下缘

    QHBoxLayout *navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(10, 0, 12, 0);
    navLayout->setSpacing(2);


    auto createNavButton = [](const QString &text, const QIcon &icon) {
        QPushButton *btn = new QPushButton(text);
        btn->setIcon(icon);
        btn->setIconSize(QSize(16, 16));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setObjectName("navBtn");
        btn->setFixedHeight(30);          // 导航条里的按钮固定 30px（原来最小 44，整条太厚）
        btn->setMinimumWidth(icon.isNull() ? 0 : 74);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        return btn;
    };


    btnHome = createNavButton("首页", QIcon(":/icons/home.png"));
    btnAccount = createNavButton("账号", QIcon(":/icons/account.png"));
    btnLog = createNavButton("日志", QIcon(":/icons/log.png"));
    btnPlugin = createNavButton("插件", QIcon(":/icons/plugin.png"));
    btnChat = createNavButton("聊天", QIcon(":/icons/chat.png"));

    // ===== 原来的「高级配置」6 个 tab，拍平到这一行（HTML制图已下线，黑名单挪进「内置」的左侧导航） =====
    QPushButton *btnCfgBasic   = createNavButton("基础",   QIcon());
    QPushButton *btnCfgButton  = createNavButton("按钮",   QIcon());
    QPushButton *btnCfgBuiltin = createNavButton("内置",   QIcon());
    QPushButton *btnCfgForbid  = createNavButton("违禁词", QIcon());
    QPushButton *btnCfgHelp    = createNavButton("帮助",   QIcon());

    btnGroup = new QButtonGroup(this);
    btnGroup->setExclusive(true);
    btnGroup->addButton(btnHome, 0);
    btnGroup->addButton(btnAccount, 1);
    btnGroup->addButton(btnLog, 2);
    btnGroup->addButton(btnPlugin, 3);
    btnGroup->addButton(btnChat, 4);
    btnGroup->addButton(btnCfgBasic, 5);
    btnGroup->addButton(btnCfgButton, 6);
    btnGroup->addButton(btnCfgBuiltin, 7);
    btnGroup->addButton(btnCfgForbid, 8);
    btnGroup->addButton(btnCfgHelp, 9);

    connect(btnGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            [this](int id) {
                updateCurrentBotInfo();
                stackedWidget->setCurrentIndex(id);
                if (logPage)
                {
                    logPage->setActive(id == 2);
                }
                chatPage->存在 = (id==4);

                // 原来挂在「高级配置 → 基础设置」tab 上的图床停用提示，跟到这里
                if (id == 5 && !框架退出) {
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
    // 原来这里是顶部「机器人 [下拉]」的切号回调。
    // 该下拉已删除，切号统一走标题栏的机器人选择器 → switchToBot() → applyConfigAppId()。
    navLayout->addWidget(btnHome);
    navLayout->addWidget(btnAccount);
    navLayout->addWidget(btnLog);
    navLayout->addWidget(btnPlugin);
    navLayout->addWidget(btnChat);

    // 主功能 / 配置项之间的竖分隔
    QFrame *navSep = new QFrame;
    navSep->setObjectName("navSep");
    navSep->setFixedWidth(1);
    navSep->setFixedHeight(16);          // 导航条压到 32 之后，分隔线也别再顶满
    navLayout->addSpacing(6);
    navLayout->addWidget(navSep);
    navLayout->addSpacing(6);

    navLayout->addWidget(btnCfgBasic);
    navLayout->addWidget(btnCfgButton);
    navLayout->addWidget(btnCfgBuiltin);
    navLayout->addWidget(btnCfgForbid);
    navLayout->addWidget(btnCfgHelp);
    navLayout->addStretch();

    // 顶部这条「机器人 [下拉]」已删除：标题栏左侧那个机器人选择器就是唯一的切号入口

    createTitleBar();

    QWidget *contentWidget = new QWidget;
    contentWidget->setObjectName("contentWidget");
    QVBoxLayout *mainContentLayout = new QVBoxLayout(contentWidget);
    mainContentLayout->setContentsMargins(0, 0, 0, 0);
    mainContentLayout->setSpacing(0);
    mainContentLayout->addWidget(stackedWidget);

    QVBoxLayout *totalLayout = new QVBoxLayout;
    totalLayout->setContentsMargins(4, 4, 4, 4);
    totalLayout->setSpacing(0);
    totalLayout->addWidget(titleBar);
    totalLayout->addWidget(navBar);
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

    // 外面再套一层透明根：central 内缩 kShadowMargin，
    // 空出来的那一圈就是阴影带，同时也是拖边缩放的热区（热区在内容之外，和系统窗口一致）
    m_windowRoot = new WindowShadowRoot;
    m_windowRoot->setObjectName("windowRoot");
    QVBoxLayout *rootLayout = new QVBoxLayout(m_windowRoot);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(central);
    setCentralWidget(m_windowRoot);
    setWindowShadowMargin(kShadowMargin);

    // 标题栏那个选择器是唯一的切号入口，启动时先把配置目标对齐到默认账号
    syncConfigAppIdFromTitleBot();
}
constexpr unsigned int hash(const char* str, unsigned int h = 0) {
    return *str ? hash(str + 1, (h * 31) + static_cast<unsigned int>(*str)) : h;
}

constexpr unsigned int getRandomNumber() {
    return hash(__TIME__) % 100;
}

namespace {

// ── 标题栏图标全部自绘 ──
// 原来是 style()->standardIcon(SP_TitleBar*)：不同 Windows 主题下线条粗细、颜色、周围留白都不一样，
// 几个按钮并排放在 26×26 的小方块里显得参差（尤其和「快速重启」这种自定义图标放一起时）。
// 这里统一成「16×16 坐标系里的 1.5px 圆头细线 + 指定颜色」，hover 换色走 QIcon::Active。
enum TitleGlyph { GlyphMinimize, GlyphMaximize, GlyphRestore, GlyphClose, GlyphRestart };

QPixmap titleGlyphPixmap(TitleGlyph glyph, const QColor &color, int px)
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 16.0, px / 16.0);       // 统一在 16×16 里画，尺寸不同只改 px

    if (glyph == GlyphRestart) {
        // 圆形箭头：280° 圆弧（缺口留在右侧）+ 缺口处的实心三角。
        // 三角按弧起点（40°）的逆时针切向算，不是随手摆的。
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(3.4, 3.4, 9.2, 9.2), 40 * 16, 280 * 16);

        QPainterPath tri;
        tri.moveTo(10.75, 4.12);
        tri.lineTo(10.06, 6.26);
        tri.lineTo(12.98, 3.82);
        tri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(tri);
        return pm;
    }

    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    switch (glyph) {
    case GlyphMinimize:
        p.drawLine(QPointF(3.6, 8.0), QPointF(12.4, 8.0));
        break;
    case GlyphMaximize:
        p.drawRect(QRectF(4.0, 4.0, 8.0, 8.0));
        break;
    case GlyphRestore: {
        // 最大化之后：两个错位的方框（后框只画上/右两条边，不然线会重叠发黑）
        p.drawRect(QRectF(3.2, 5.8, 7.0, 7.0));
        QPainterPath back;
        back.moveTo(5.6, 5.8);
        back.lineTo(5.6, 3.2);
        back.lineTo(12.8, 3.2);
        back.lineTo(12.8, 10.4);
        back.lineTo(10.2, 10.4);
        p.drawPath(back);
        break;
    }
    case GlyphClose:
    default:
        p.drawLine(QPointF(4.6, 4.6), QPointF(11.4, 11.4));
        p.drawLine(QPointF(11.4, 4.6), QPointF(4.6, 11.4));
        break;
    }
    return pm;
}

// normal = 常态颜色，active = 鼠标悬停颜色（QIcon::Active 由样式在 hover 时自动取用）
QIcon titleGlyphIcon(TitleGlyph glyph, const QColor &normal, const QColor &active)
{
    QIcon icon;
    const int kLogical = 16;
    QPixmap on = titleGlyphPixmap(glyph, normal, kLogical * 2);
    on.setDevicePixelRatio(2.0);         // 2 倍图，高 DPI 下不糊
    icon.addPixmap(on, QIcon::Normal, QIcon::Off);

    QPixmap hov = titleGlyphPixmap(glyph, active, kLogical * 2);
    hov.setDevicePixelRatio(2.0);
    icon.addPixmap(hov, QIcon::Active, QIcon::Off);
    return icon;
}

} // namespace

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

    int randomIndex = QRandomGenerator::global()->bounded(1, 6);  // 生成 1~23
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

    // 快速重启：挨着最小化左边。语义就是「立即重启框架」，所以不弹确认（点了就重启）
    restartBtn = new QToolButton;
    restartBtn->setObjectName("restartBtn");
    restartBtn->setFixedSize(btnSize, btnSize);
    restartBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    restartBtn->setCursor(Qt::PointingHandCursor);
    restartBtn->setToolTip("快速重启框架（所有机器人会断开重连）");
    connect(restartBtn, &QToolButton::clicked, this, [](){ restartFramework(); });

    // 图标全部自绘，风格统一（见文件上方 titleGlyphIcon）
    const QColor glyphColor("#3D3D3D");
    const QColor glyphHover("#E2681F");
    minBtn->setIcon(titleGlyphIcon(GlyphMinimize, glyphColor, glyphHover));
    maxBtn->setIcon(titleGlyphIcon(GlyphMaximize, glyphColor, glyphHover));
    closeBtn->setIcon(titleGlyphIcon(GlyphClose, glyphColor, QColor("#D83B32")));
    restartBtn->setIcon(titleGlyphIcon(GlyphRestart, glyphColor, glyphHover));

    const int iconSize = 16;
    minBtn->setIconSize(QSize(iconSize, iconSize));
    maxBtn->setIconSize(QSize(iconSize, iconSize));
    closeBtn->setIconSize(QSize(iconSize, iconSize));
    restartBtn->setIconSize(QSize(iconSize, iconSize));

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
    restartBtn->setStyleSheet(btnStyle);
    closeBtn->setStyleSheet(btnStyle + "QToolButton:hover { background-color: #FFE2DF; color: #D83B32; }");

    // ========== 检查更新 / 关于（原在导航条右端，导航条让位给机器人下拉后挪到这里） ==========
    checkUpdateBtn = new QPushButton("检查更新");
    checkUpdateBtn->setFlat(true);
    checkUpdateBtn->setObjectName("linkBtn");
    checkUpdateBtn->setCursor(Qt::PointingHandCursor);
    connect(checkUpdateBtn, &QPushButton::clicked, this, [this](){
        checkUpdate();
    });

    QPushButton *btnLicense = new QPushButton("关于");
    btnLicense->setObjectName("linkBtn");
    btnLicense->setCursor(Qt::PointingHandCursor);
    connect(btnLicense, &QPushButton::clicked, this, [](){
        showClickableLicenseInfo();
    });

    // ========== 组装布局 ==========
    layout->addWidget(leftWidget);            // 新增的左侧区域
    layout->addStretch();                     // 弹性空间，将后续元素推到右侧
    layout->addWidget(checkUpdateBtn);
    layout->addWidget(btnLicense);
    layout->addSpacing(10);
    layout->addWidget(botStatusWidget);       // 机器人状态区域（保持原有位置）
    layout->addSpacing(10);
    layout->addWidget(restartBtn);            // 快速重启：窗口按钮组左边
    layout->addSpacing(6);
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

    // 顶部账号下拉删除后，g_appid 的首次初始化挪到这里：谁都没选过时默认第一个账号，
    // 否则各配置页大量 if (g_appid == 0) return 会导致界面一片空白。
    if (g_appid == 0 && !m_accounts.isEmpty() && m_accounts.first())
        applyConfigAppId(m_accounts.first()->appid_int);

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


// 切换「配置目标」账号。
// 原来由顶部那条「机器人 [下拉]」触发，下拉删除后并入标题栏的机器人选择器。
// 返回 false = 被拦下（批量推送中），调用方应中止本次切换。
bool MainWindow::applyConfigAppId(int appid)
{
    if (appid == 0) return true;                 // 无账号，什么都不用做

    if (ts_m_stopPush) {
        QMessageBox::warning(this, "批量推送中", "批量推送信息中展示不能切换 账号");
        return false;
    }

    g_appid = appid;
    RuleConfigWidget->列表行被单击();
    TextReplace->列表行被单击();
    keyword->列表行被单击();
    schedule->列表行被单击();
    ai_ui->list_c();

    ui_qunguan->列表行被单击();
    m_nickReviewWidget->setAppId(g_appid);
    material_ui->setAppId(g_appid);
    Black->setAppId(g_appid);          // 黑名单按账号隔离：切账号即换一份

    // 当前停在「菜单 / 指令 / 素材」三页（同一个 MenuPanelWidget）时重新拉取
    QListWidgetItem *curNavItem = configNav ? configNav->item(configNav->currentRow()) : nullptr;
    if (curNavItem && curNavItem->data(Qt::UserRole + 1).toInt() >= 0) {
        ui_MenuPanel->switchBot();
        _g_qieh = false;
        return true;
    }
    _g_qieh = true;
    return true;
}

// 启动时把配置目标对齐到标题栏当前显示的机器人（显示「全部」时取第一个账号）
void MainWindow::syncConfigAppIdFromTitleBot()
{
    if (m_accounts.isEmpty()) return;
    int idx = m_currentBotIndex;
    if (idx < 0 || idx >= m_accounts.size()) idx = 0;
    const auto &info = m_accounts.at(idx);
    if (info) applyConfigAppId(info->appid_int);
}

void MainWindow::switchToBot(int index)
{
    // 标题栏选择器现在兼任「配置目标」：先切 g_appid，被拦（批量推送中）就整次不生效
    if (index >= 0 && index < m_accounts.size() && m_accounts.at(index)) {
        if (!applyConfigAppId(m_accounts.at(index)->appid_int)) return;
    }

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
        // 最大化 / 全屏：收掉外圈阴影带（否则四边漏桌面），同时禁掉拖边缩放
        const bool maxed = isMaximized() || isFullScreen();
        setWindowShadowMargin(maxed ? 0 : kShadowMargin);
        if (maxed) applyEdgeCursor(Qt::Edges());

        // 图标同样是自绘的（见 titleGlyphIcon），不用系统 standardIcon
        const QColor glyphColor("#3D3D3D");
        const QColor glyphHover("#E2681F");
        if (isMaximized()) {
            maxBtn->setIcon(titleGlyphIcon(GlyphRestore, glyphColor, glyphHover));
            maxBtn->setToolTip("还原");
        } else {
            maxBtn->setIcon(titleGlyphIcon(GlyphMaximize, glyphColor, glyphHover));
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




// ═════════════════ 无边框窗口的边缘缩放 ═════════════════
// 窗口是 FramelessWindowHint，系统自带的那圈可拖拽边框一起没了，只能自己判。
// 判定与接管都放在 eventFilter 里（装在 qApp 上）而不是 mouseMoveEvent：
// 鼠标下面是 QListWidget / QLineEdit 这些子控件，它们会先吃掉鼠标事件，主窗口根本收不到。

// 热区宽度始终等于阴影带宽度：热区落在「内容之外、窗口之内」的那一圈，
// 与系统窗口的边框/阴影热区位置一致，也不会再抢内容区边缘的鼠标。
void MainWindow::setWindowShadowMargin(int m)
{
    edgeMargin = m;
    if (m_windowRoot)
        static_cast<WindowShadowRoot *>(m_windowRoot)->setShadowMargin(m);
    if (m <= 0)
        applyEdgeCursor(Qt::Edges());
}

Qt::Edges MainWindow::edgesAt(const QPoint &globalPos) const
{
    if (!isVisible() || isMaximized() || isFullScreen()) return Qt::Edges();

    const QRect r = geometry();               // 无边框窗口：frameGeometry() == geometry()
    const int m = edgeMargin;
    if (!r.adjusted(-2, -2, 2, 2).contains(globalPos)) return Qt::Edges();

    Qt::Edges edges;
    if (qAbs(globalPos.x() - r.left())   <= m) edges |= Qt::LeftEdge;
    if (qAbs(globalPos.x() - r.right())  <= m) edges |= Qt::RightEdge;
    if (qAbs(globalPos.y() - r.top())    <= m) edges |= Qt::TopEdge;
    if (qAbs(globalPos.y() - r.bottom()) <= m) edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::applyEdgeCursor(Qt::Edges edges)
{
    if (edges) {
        const bool left   = edges & Qt::LeftEdge;
        const bool right  = edges & Qt::RightEdge;
        const bool top    = edges & Qt::TopEdge;
        const bool bottom = edges & Qt::BottomEdge;

        Qt::CursorShape shape = Qt::ArrowCursor;
        if ((left && top) || (right && bottom))       shape = Qt::SizeFDiagCursor;   // ↖↘
        else if ((right && top) || (left && bottom))  shape = Qt::SizeBDiagCursor;   // ↗↙
        else if (left || right)                       shape = Qt::SizeHorCursor;
        else if (top || bottom)                       shape = Qt::SizeVerCursor;

        // 光标形状由「光标下方的子控件」决定，所以往主窗口上 setCursor 没用，
        // 必须走 override cursor（全局压栈，成对 restore，别漏）。
        if (!edgeCursorActive) {
            QApplication::setOverrideCursor(QCursor(shape));
            edgeCursorActive = true;
        } else if (QApplication::overrideCursor() && QApplication::overrideCursor()->shape() != shape) {
            QApplication::changeOverrideCursor(QCursor(shape));
        }
    } else if (edgeCursorActive) {
        QApplication::restoreOverrideCursor();
        edgeCursorActive = false;
    }
}

void MainWindow::beginResize(Qt::Edges edges, const QPoint &globalPos)
{
    resizing = true;
    resizeEdge = edges;
    resizeStartRect = geometry();
    startGlobalPos = globalPos;
}

void MainWindow::performResize(const QPoint &globalPos)
{
    if (!resizing || !resizeEdge) return;

    const QPoint d = globalPos - startGlobalPos;
    int left   = resizeStartRect.left();
    int top    = resizeStartRect.top();
    int right  = resizeStartRect.right();
    int bottom = resizeStartRect.bottom();

    if (resizeEdge & Qt::LeftEdge)   left   += d.x();
    if (resizeEdge & Qt::RightEdge)  right  += d.x();
    if (resizeEdge & Qt::TopEdge)    top    += d.y();
    if (resizeEdge & Qt::BottomEdge) bottom += d.y();

    // 最小尺寸：拖左/上边到极限时不能反向推着右边跑（否则窗口会整体横移）
    const int minW = minimumSize().width();
    const int minH = minimumSize().height();
    if (right - left + 1 < minW) {
        if (resizeEdge & Qt::LeftEdge) left  = right - minW + 1;
        else                           right = left  + minW - 1;
    }
    if (bottom - top + 1 < minH) {
        if (resizeEdge & Qt::TopEdge) top    = bottom - minH + 1;
        else                          bottom = top    + minH - 1;
    }

    // QRect(QPoint, QPoint) 是 topLeft/bottomRight，两端都含
    setGeometry(QRect(QPoint(left, top), QPoint(right, bottom)));
}

// 窗口缩放与拖拽（拖窗口统一走标题栏 eventFilter）
void MainWindow::mousePressEvent(QMouseEvent *event)
{
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
        if (resizing) applyEdgeCursor(edgesAt(event->globalPos()));
        resizing = false;
        resizeEdge = Qt::Edges();
        dragStartPos = QPoint();
    }
    QMainWindow::mouseReleaseEvent(event);
}
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // ── 边缘缩放压在最前面 ──
    // 标题栏顶边同时也是窗口顶边，不能让下面「拖窗口」的分支先吃掉。
    if (QWidget *w = qobject_cast<QWidget *>(obj)) {
        if (w->window() == this) {
            switch (event->type()) {
            case QEvent::MouseMove: {
                QMouseEvent *me = static_cast<QMouseEvent *>(event);
                if (resizing) {
                    if (!(me->buttons() & Qt::LeftButton)) {
                        // release 掉了（拖太快滑出窗口），强制收尾，否则会一直处于缩放态
                        resizing = false;
                        resizeEdge = Qt::Edges();
                        applyEdgeCursor(Qt::Edges());
                        break;
                    }
                    performResize(me->globalPos());
                    return true;
                }
                if (!me->buttons()) {
                    const Qt::Edges e = edgesAt(me->globalPos());
                    resizeEdge = e;              // 记一下，press 时省一次判断
                    applyEdgeCursor(e);
                }
                break;
            }
            case QEvent::MouseButtonPress: {
                QMouseEvent *me = static_cast<QMouseEvent *>(event);
                if (me->button() == Qt::LeftButton) {
                    const Qt::Edges e = edgesAt(me->globalPos());
                    if (e) {
                        beginResize(e, me->globalPos());
                        applyEdgeCursor(e);
                        dragStartPos = QPoint();   // 顺手清掉，避免标题栏拖拽逻辑接着跑
                        return true;
                    }
                }
                break;
            }
            case QEvent::MouseButtonRelease: {
                if (resizing) {
                    QMouseEvent *me = static_cast<QMouseEvent *>(event);
                    resizing = false;
                    resizeEdge = Qt::Edges();
                    applyEdgeCursor(edgesAt(me->globalPos()));
                    return true;
                }
                break;
            }
            case QEvent::Leave:
                // 只有当光标真的离开整个窗口才撤（子控件之间来回移动也会发 Leave，
                // 无条件撤会让缩放光标一闪一闪）
                if (!resizing && !geometry().contains(QCursor::pos()))
                    applyEdgeCursor(Qt::Edges());
                break;
            default:
                break;
            }
        }
    }

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
        return "#"+remoteTag+"\n"+releaseNotes+"\n\n---\n\n发送[#确认更新框架]() 来更新 注意更新需要重启 如果更新失败 可能需要手动更新";
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
    QString exePath = QDir(appDir).filePath("纯白铃32.exe");
    if (!QFile::exists(exePath))
        return "纯白铃32 不存在 或 运行失败 需要这个才能更新框架";

    std::wstring exe = exePath.toStdWString();
    std::wstring args = L" http";   // 注意参数前有空格
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
    // 下面这段是「基础样式表」（颜色都是默认配色）。
    // ThemeColors::apply 会把用户改过的那些颜色替换掉；一项都没改过时输出与原样完全一致。
    setStyleSheet(ThemeColors::apply(QString::fromUtf8(R"(
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
        QWidget#navBar {
            background: #FEFEFC;
            border-bottom: 1px solid #F4E8DA;
        }
        QFrame#navSep {
            background: #EADFD2;
            border: none;
        }
        QLabel#brandLogoLabel {
            background: transparent;
            border: none;
        }
        QPushButton#navBtn {
            background: transparent;
            border: none;
            border-bottom: 2px solid transparent;
            border-radius: 0px;
            color: #687589;
            font-size: 13px;
            font-weight: 600;
            padding: 0px 14px;
            text-align: center;
        }
        QPushButton#navBtn:hover {
            background: #FFF6EA;
            color: #FF914D;
            border-bottom: 2px solid #FFD7A8;
        }
        QPushButton#navBtn:checked {
            background: transparent;
            color: #FF7F32;
            border-bottom: 2px solid #FF7F32;
        }
        QPushButton#linkBtn {
            background: transparent;
            border: none;
            color: #9A9384;
            font-size: 12px;
            font-weight: 600;
            padding: 0px 8px;
        }
        QPushButton#linkBtn:hover {
            color: #FF7F32;
        }
        QLabel#mascotImage {
            background: transparent;
            border: none;
        }
        QStackedWidget#contentStack {
            background: #F7EFE5;
            border: none;
            border-bottom-left-radius: 5px;
            border-bottom-right-radius: 5px;
        }
        QFrame, QGroupBox {
            background: #FFFFFF;
            border: none; /* 移除残余硬边框 */
            border-radius: 5px;
        }
        /* ⚠ QLabel 继承自 QFrame，会被上面那条染成白底圆角块（米色面板上特别显眼，
           按钮生成页的表单标签就是这样）。这里显式改回透明；
           带 objectName（QLabel#xxx）或自身 setStyleSheet 的标签特异性更高，不受影响。 */
        QLabel {
            background: transparent;
            border: none;
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
            /* 浅橙实底 + 细描边 + 深色字：比原来的「无边框 + 橙字」更清楚，
               深色主题下三色都会被替换成暗橙底 / 深灰边 / 浅字，不会糊成一片 */
            border: 1px solid #E6D4C0;
            border-radius: 6px;
            padding: 6px 12px;
            background: #FFF0DE;
            color: #344054;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #FFE5C8;
            border: 1px solid #FFB066;
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
        /* ---------- 选择夹（QTabWidget / QTabBar） ---------- */
        QTabWidget, QTabBar {
            background: transparent;
            border: none;
        }
        QTabWidget::pane {
            border: 1px solid #E0E0E0;
            border-radius: 6px;
            background: #FFFFFF;
            top: -1px;
        }
        QTabBar::tab {
            background: #F3EADF;
            color: #687589;
            border: 1px solid #E0E0E0;
            border-bottom: none;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            padding: 6px 16px;
            margin-right: 2px;
            margin-bottom: -1px;
            font-weight: 600;
        }
        QTabBar::tab:hover {
            background: #FFF7EA;
            color: #FF914D;
        }
        QTabBar::tab:selected {
            background: #FFEDD9;
            color: #FF7F32;
            border: 1px solid #FFB066;
            border-bottom: none;
        }
#tagLabel {
    background: transparent !important;
    color: #1E90FF;
    font-size: 11px;
    padding: 0px;
    border: none;
}
        /* ---------- 高级配置页：二级竖排导航 ---------- */
        QListWidget#configNav {
            background: #FFFFFF;
            border: 1px solid #F0E3D3;
            border-radius: 8px;
            outline: none;
            padding: 6px;
        }
        QListWidget#configNav::item {
            color: #687589;
            background: transparent;
            border: none;
            border-radius: 6px;
            padding: 0px 10px;
            font-size: 13px;
            font-weight: 600;
        }
        QListWidget#configNav::item:hover {
            background: #FFF6EA;
            color: #FF914D;
        }
        QListWidget#configNav::item:selected {
            background: #FFF0DE;
            color: #FF7F32;
        }
        QStackedWidget#configStack {
            background: #FFFFFF;
            border: 1px solid #F0E3D3;
            border-radius: 8px;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 6px;
            margin: 0px 2px 0px 2px;
        }
        QScrollBar::handle:horizontal {
            background: #E7D9C8;
            border-radius: 3px;
            min-width: 40px;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
        QTableWidget { border: 1px solid #AE8AB1; gridline-color: #d0d0d0; }
        QHeaderView::section { background-color: #f5f5f5; border: 1px solid #d0d0d0; }
    )")));
}
