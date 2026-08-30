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

#include "global.h"
#include <QMessageBox>
#include <qtcpserver.h>
#include <QNetworkReply>
#include "chatpage.h"
#include "pluginmarket.h"
#include "netmanager.h"
#include "mainwindow.h"
#include <QHostInfo>
#include <QThreadPool>
bool 框架退出=false;
int miaomiao32=0;
int miaomiao=0;
QString g_neiw;
Global::Global() {}

int mapTypeToTabIndex(int type)
{
    switch (type) {
    case 0:
         return 1;
    case 1:
        return 2;
    case 2:
        return 3;
    case 3:
        return 4;
    default:
        return 0;
    }
}
QPair<int, QString> splitWrappedMsgId(const QString &wrapped);
QMap<QString, QTimer*> m_openidTimers;  // 只能由主线程访问
int plugin_n=0;
int plugin_n2=false;
void botnomsg(int appid,int type,const QString &openid,const QString &msgid)
{
    if (!m_botClients.contains(appid)) return;
    QQBotClient *c = m_botClients[appid];
    if (c->m_info->fallbackReply.isEmpty()) return;
    int tabIndex=type + 1;
    if(tabIndex<1 || tabIndex>4) return;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    if(index<0) return;
    int n = g_logdb [tabIndex]->incrementBufferStatus(index);
    if(n >= 250) return; //255代表被处理了
    //qDebug()<< "未回应计数：" <<entry.n;
    if(n>=plugin_n)
    {

        QMetaObject::invokeMethod(qApp, [=]() {

            if (m_openidTimers.contains(openid)) {
                QTimer *oldTimer = m_openidTimers[openid];
                oldTimer->start();  // 重新计时 5 秒
                //qDebug() << "重置定时" << openid;
                return;  // 无需创建新定时器
            }

            // 2. 创建新的单次定时器
            QTimer *timer = new QTimer();
            timer->setSingleShot(true);
            timer->setInterval(10000);

            // 3. 连接回调（注意 lambda 捕获所有需要的变量）
            QObject::connect(timer, &QTimer::timeout, qApp, [=]() {
                // 回调执行时，该定时器已触发，需要从 map 中移除
                m_openidTimers.remove(openid);  // 先移除自身

                // 执行业务逻辑
                if (!m_botClients.contains(appid)) {
                    timer->deleteLater();
                    return;
                }
                QQBotClient *c = m_botClients[appid];
                if (c->m_info->fallbackReply.isEmpty()) {
                    timer->deleteLater();
                    return;
                }
                QString text = "[未被处理回应]";
                c->send_msgAsync(type, openid, text, c->m_info->fallbackReply, msgid, false, false, 0, true);

                timer->deleteLater();  // 任务完成，清理定时器
            });

            // 4. 保存定时器到 map，并启动
            m_openidTimers.insert(openid, timer);
            timer->start();

            //qDebug() << "添加定时（或重置）" << openid;
        }, Qt::QueuedConnection);
    }

}



QString python_code4(const QString &py_code,int appid,const QList<QString> user_list,const QList<QString> user_name_list)
{
    py::gil_scoped_acquire gil;
    try {
        py::module_ qiancao = py::module_::import("qiancao_sdk");
        py::object api = qiancao.attr("QQApi")(g_keyuuid);

        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");
        exec_globals["UserList"] = py::cast(user_list);
        exec_globals["UserNameList"] = py::cast(user_name_list);
        exec_globals["g_appid"] = py::cast(appid);
        exec_globals["api"] = api;               // 注入 api 对象

        // 4. 执行用户代码
        py::exec(py_code.toStdString(), exec_globals);

        // 5. 读取返回值
        QString ret;
        if (exec_globals.contains("__result__"))
            ret = QString::fromStdString(py::str(exec_globals["__result__"]));
        return ret;
    } catch (const py::error_already_set &e) {
        AppendEventLog("[Python] Execute code error: " + QString::fromUtf8(e.what()) ,0xff);
    } catch (const std::exception &e) {
        AppendEventLog("[Python] Execute code error: " + QString::fromUtf8(e.what()) ,0xff);
    }
    return QString();
}

QString python_code(const QString &py_code)
{
    py::gil_scoped_acquire gil;
    py::object sys, stdout_old, stringio;
    try {
        // 重定向 stdout 以捕获 print 输出
        sys = py::module_::import("sys");
        stdout_old = sys.attr("stdout");
        stringio = py::module_::import("io").attr("StringIO")();
        sys.attr("stdout") = stringio;

        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");

        // 执行用户代码
        py::exec(py_code.toStdString(), exec_globals);

        // 恢复 stdout 并获取 print 输出
        sys.attr("stdout") = stdout_old;
        std::string output = py::str(stringio.attr("getvalue")());
        return QString::fromStdString(output);
    } catch (const std::exception &e) {

        if (!sys.is_none() && !stdout_old.is_none()) {
            try { sys.attr("stdout") = stdout_old; } catch (...) {}
        }
        return QString::fromUtf8(e.what());
    }
}
void cancelTimer(const QString &openid)
{
    QMetaObject::invokeMethod(qApp, [=]() {
        if (!m_openidTimers.contains(openid)) return;

        QTimer *timer = m_openidTimers.take(openid);  // 从 map 取出
        timer->stop();
        timer->deleteLater();  // 安全销毁
        //qDebug() << "取消定时" << openid;
    }, Qt::QueuedConnection);
}

void AppendEventLog(const QString &msg,int color)
{
    if(框架退出) return;
    Message m;
    m.msg= msg;
    m.Color_0=color;
    m.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    g_logdb[0]->appendLog("0","0",m);
    logPage->onNewLogAdded(0,0,0,"",m);

}
void showAutoCloseMessageBox(const QString &title, const QString &text, int timeoutMs)
{
    QMessageBox *msgBox = new QMessageBox(QMessageBox::Information, title, text,
                                          QMessageBox::NoButton, nullptr);
    msgBox->setAttribute(Qt::WA_DeleteOnClose);
    msgBox->show();
    QTimer::singleShot(timeoutMs, msgBox, &QMessageBox::close);
}



/**
 * @brief 将字符串中的所有换行序列统一替换为单个 '\r'
 * @param input 原始字符串（可能包含 \r\n, \n, \r 等）
 * @return 处理后的字符串，所有换行符被替换为 '\r'
 */
QString normalizeNewlinesToCR(const QString &input)
{
    QString result;
    result.reserve(input.size());
    int i = 0;
    const int len = input.size();
    while (i < len) {
        const QChar ch = input[i];
        if (ch == '\r') {
            if (i + 1 < len && input[i + 1] == '\n') {
                result.append('\n');
                ++i;
            } else {

                result.append('\r');
            }
        }
        else if (ch == '\n') {
            result.append('\r');
        }
        else {
            result.append(ch);
        }
        ++i;
    }
    result.replace("\\n","\r");
    return result;
}
QString python_code3(const QString &py_code,const MessageEvent &msg)
{
    py::gil_scoped_acquire gil;
    try {
        py::module_ qiancao = py::module_::import("qiancao_sdk");
        py::object api = qiancao.attr("QQApi")(g_keyuuid);

        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");
        exec_globals["msg"] = py::cast(msg);
        exec_globals["api"] = api;               // 注入 api 对象

        // 4. 执行用户代码
        py::exec(py_code.toStdString(), exec_globals);

        // 5. 读取返回值
        QString ret;
        if (exec_globals.contains("__result__"))
            ret = QString::fromStdString(py::str(exec_globals["__result__"]));

        return ret;
    } catch (const py::error_already_set &e) {
        QString text = "[Python] Execute code error: " + QString::fromUtf8(e.what());
        AppendEventLog(text ,0xff);
        return text;

    } catch (const std::exception &e) {
        QString text ="[Python] Execute code error: " + QString::fromUtf8(e.what());
        AppendEventLog(text ,0xff);
        return text;
    }
    return "代码执行完成 未返回数据";
}
QString handleMessage(const MessageEvent &ev, AccountInfo *info) {
    QString p1, p2, p3;  // 最多三个参数，p3 自动收尾剩余


    if (!info->bai_sr.isEmpty() && ev.msg.startsWith(info->bai_sr)) {
        int cnt = extractParams(ev.msg, info->bai_sr, 0, p1);
        if (cnt == -1) return "指令错误";
        if (cnt == 0) {
            //没提供参数
            if(ev.type==0)
            {

                if (ev.bitmap & BIT_AI_BAI) return "本群已在白名单";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 rec;
                db->getGroupInfo(ev.groupId,rec);
                rec.bitmap |= BIT_AI_BAI;
                db->addGroup(ev.groupId,rec);
                return "添加本群 ai白名单成功";
            }else if(ev.type==2)
            {
                auto *db = g_botdb[info->appid_int];
                UserRecord rec;
                db->getUserBySeqId(ev.user_int,rec);
                if (rec.bitmap & BIT_AI_BAI) return "你已经已在白名单";

                rec.bitmap |= BIT_AI_BAI;
                db->updateUserBySeqId(ev.user_int,rec);
                return "添加 ai白名单成功";
            }
            return "目前只支持 群 和 私聊设置白名单";
        }
        //提供了参数
        if(p1.size()==32)
        {
            auto *db = g_botdb[info->appid_int];
            GroupRecord2 rec;
            if(db->getGroupInfo(p1,rec))
            {
                if(p1!=ev.groupId) return "传递参数1 群id 在数据库 未记录 请检查是否是真实群id 或者将机器人移出 对应群再次邀请";
                rec.create_time= QDateTime::currentSecsSinceEpoch()/60;
                rec.inviter_seq_id=ev.user_int;
            }
            if (rec.bitmap & BIT_AI_BAI) return "本群已在白名单";
            rec.bitmap |= BIT_AI_BAI;
            db->addGroup(ev.groupId,rec);
            return "添加本群 ai白名单成功";
        }
        int user_id = p1.toInt();
        if(user_id <2147483636 && user_id>0)
        {
            auto *db = g_botdb[info->appid_int];
            UserRecord rec;
            db->getUserBySeqId(ev.user_int,rec);
            if (rec.bitmap & BIT_AI_BAI) return "该用户已经在白名单 或 已经开启";
            rec.bitmap |= BIT_AI_BAI;
            db->updateUserBySeqId(ev.user_int,rec);
            return "添加成功 你确保整个 id 是你需要添加的人";
        }

        return "参数错误 指令{群id|好友id} 好友id 是短id";
    }

    // ========== 白名单删除 ==========

    if (!info->bai_sc.isEmpty() && ev.msg.startsWith(info->bai_sc)) {
        int cnt = extractParams(ev.msg, info->bai_sc, 0, p1);
        if (cnt == -1) return "指令错误";
        if (cnt == 0) {
            // 无参数：操作当前会话（群或私聊）
            if (ev.type == 0) {

                if (!(ev.bitmap & BIT_AI_BAI)) return "本群不在白名单";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 rec;
                db->getGroupInfo(ev.groupId, rec);
                rec.bitmap &= ~BIT_AI_BAI;
                db->addGroup(ev.groupId, rec);
                return "删除本群 AI 白名单成功";

            } else if (ev.type == 2) {
                auto *db = g_botdb[info->appid_int];
                UserRecord rec;
                db->getUserBySeqId(ev.user_int, rec);
                if (!(rec.bitmap & BIT_AI_BAI)) return "该用户不在白名单";
                rec.bitmap &= ~BIT_AI_BAI;
                db->updateUserBySeqId(ev.user_int, rec);
                return "删除当前用户白名单成功";
            }
            return "目前只支持群和私聊设置白名单";
        }

        if (p1.size() == 32) {
            // 参数为群ID（长ID）
            auto *db = g_botdb[info->appid_int];
            GroupRecord2 rec;
            if (db->getGroupInfo(p1, rec)) {
                if (p1 != ev.groupId) return "只能操作当前群";
                if (!(rec.bitmap & BIT_AI_BAI)) return "该群不在白名单";
                rec.bitmap &= ~BIT_AI_BAI;
                db->addGroup(ev.groupId, rec);
                return "删除本群 AI 白名单成功";
            } else {
                return "数据库中无此群记录，可能未添加白名单 相当于删除成功";
            }
        }

        int user_id = p1.toInt();
        if (user_id > 0 && user_id < 2147483636) {
            // 参数为合法的用户ID（仅校验，操作当前用户）
            auto *db = g_botdb[info->appid_int];
            UserRecord rec;
            db->getUserBySeqId(ev.user_int, rec);
            if (!(rec.bitmap & BIT_AI_BAI)) return "该用户不在白名单";
            rec.bitmap &= ~BIT_AI_BAI;
            db->updateUserBySeqId(ev.user_int, rec);
            return "删除当前用户白名单成功";
        }

        return "参数错误，指令{群id|好友id}，好友id为短id";
    }


    if (!info->bai_qy.isEmpty() && ev.msg.startsWith(info->bai_qy)) {
        QString cmd = info->bai_qy;
        int cnt = extractParams(ev.msg, cmd, 0, p1);
        if (cnt < 1) return "请指定 1（启用）或 0（关闭）";
        QString status = p1.split(" ").first();
        if (status != "0" && status != "1") return "状态值必须为 0 或 1";
        bool enable = (status == "1");
        return QString("白名单已%1").arg(enable ? "启用" : "关闭");

    }

    return QString();
}


QString ruqunhy(AccountInfo *info,const MessageEvent &ev);
QString 内置指令(MessageEvent &ev)
{
    QString text;
    if(ev.msg=="我的id" || ev.msg=="我的ID")
    {
        if(g_botdb.contains(ev.appid))
        {
            QByteArray userKeyBytes = QByteArray::fromHex(ev.user.toUtf8());
            QByteArray groupKeyBytes = QByteArray::fromHex(ev.groupId.toUtf8());
            auto *db =  g_botdb[ev.appid];
            text = QString("**我的ID**\n>user_id:%1\n个人ID>:%2\n群ID>：%3\n>今日消息统计：%4\n>本群消息统计：%5")
                       .arg(ev.user_int).arg(ev.user, ev.groupId)
                       .arg(db->getUserTodayMsgCount(userKeyBytes)).arg(db->getGroupTodayMsgCount(groupKeyBytes));

        }else
            text = QString("**我的ID**\n>user_id:%1\n个人ID>:%2\n群ID>：%3\n>今日消息统计：%4\n>本群消息统计：%5").arg(ev.user_int).arg(ev.user, ev.groupId);
    }
    if(ev.msg=="代管列表")
    {
        text.reserve(200);
        text.append("**本群代管列表**");
        for (int i = 0; i < 20; ++i) {
            if(ev.qid[i]==0){
                text.append(QString("\n>%1:空位.. [添加](添加本群代管 <id>)").arg(i+1));
            }else
            {
                text.append(QString("\n>%1:%2 [删除](删除本群代管 %3)").arg(i+1).arg(ev.qid[i]).arg(ev.qid[i]));
            }
        }

        text.append("\n\n添加代管后可以使用机器人 禁言撤回 同意加群等指令 如果不知道id可以使用 [添加本群代管]() + 艾特 添加");
    }
    return text;
}
QString addbot(int appid,const QString &secret,const QString &wsAddress,int type,const QString  &markdown,int wsIntents)
{
    if (appid==0) return "appid 为0";
    if (secret.isEmpty()) return "secret 为空";
    int existingIndex = -1;
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i]->appid_int == appid) {
            existingIndex = i;
            break;
        }
    }
    if (existingIndex==-1) { //添加新
        auto oldInfoPtr = std::make_shared<AccountInfo>();
        oldInfoPtr->secret = secret;
        oldInfoPtr->wsAddress = wsAddress;
        oldInfoPtr->type = type;
        int md=0;
        if(markdown.isEmpty())
            md=-1;
        else
            md = markdown.toInt();
        if(md == -1)
            oldInfoPtr->markdown = true;
        else
            oldInfoPtr->markdown = (md==1);

        if(wsIntents == 0)
            oldInfoPtr->wsIntents = 1191186432;
        else
            oldInfoPtr->wsIntents = wsIntents;
        m_accounts.append(oldInfoPtr);
        QMetaObject::invokeMethod(qApp, [=]() {
            accountPage->refreshCards2(oldInfoPtr.get());

        });

    } else {
        auto oldInfoPtr = m_accounts[existingIndex];

        oldInfoPtr->secret = secret;
        oldInfoPtr->wsAddress = wsAddress;
        oldInfoPtr->type = type;
        int md=0;
        if(markdown.isEmpty())
            md=-1;
        else
            md = markdown.toInt();
        if(md == -1)
            oldInfoPtr->markdown = true;
        else
            oldInfoPtr->markdown = (md==1);
        if(wsIntents == 0)
            oldInfoPtr->wsIntents = 0;
        else
            oldInfoPtr->wsIntents = wsIntents;
        accountPage->saveAccounts(oldInfoPtr.get());

    }


    return "添加成功 注意 重复appid,是覆盖请确定appid 正确";
}



// ---------- 辅助函数（从之前的回答中已有） ----------
bool updateGlobalPluginList(QString& errorMsg);   // 从网络拉取并填充 m_allPlugins

static bool isPluginInstalled(const QString& id) {
    for (const PluginInfo &p : std::as_const(m_pluginList)) {
        if (p.id == id) return true;
    }
    return false;
}

// ---------- 处理 #插件市场 指令 ----------
QString handlePluginMarket(const QString& msg) {
    // 1. 解析参数
    QString cmd = msg.trimmed();
    if (!cmd.startsWith("#插件市场"))
        return "错误：指令格式不正确";

    QString args = cmd.mid(QString("#插件市场").length()).trimmed();
    int page = 1;
    QString keyword;
    QString tagFilter;

    if (!args.isEmpty()) {
        const QStringList parts = args.split(' ', Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            if (part.startsWith("标签:", Qt::CaseInsensitive)) {
                tagFilter = part.mid(3).trimmed();
            } else {
                bool ok;
                int num = part.toInt(&ok);
                if (ok && num > 0) {
                    page = num;
                } else {
                    if (!keyword.isEmpty()) keyword += " ";
                    keyword += part;
                }
            }
        }
    }

    // 2. 确保插件列表已加载
    if (m_allPlugins.isEmpty()) {
        QString err;
        if (!updateGlobalPluginList(err)) {
            return QString("获取插件列表失败：%1").arg(err);
        }
    }

    // 3. 过滤插件
    QList<PluginInfo2> filtered;
    for (const PluginInfo2& info : std::as_const(m_allPlugins)) {
        // 标签过滤
        if (!tagFilter.isEmpty()) {
            bool tagMatched = false;
            for (const QString& tag : info.tags) {
                if (tag.contains(tagFilter, Qt::CaseInsensitive)) {
                    tagMatched = true;
                    break;
                }
            }
            if (!tagMatched) continue;
        }

        // 关键词搜索（名称 + 备注）
        if (!keyword.isEmpty()) {
            if (!info.name.contains(keyword, Qt::CaseInsensitive) &&
                !info.remark.contains(keyword, Qt::CaseInsensitive)) {
                continue;
            }
        }

        filtered.append(info);
    }

    // 4. 分页
    const int pageSize = 20;
    int total = filtered.size();
    int totalPages = (total + pageSize - 1) / pageSize;

    if (total == 0)
        return "没有匹配的插件。";
    if (page > totalPages)
        return QString("页码超出，共 %1 页，当前请求第 %2 页").arg(totalPages).arg(page);

    int start = (page - 1) * pageSize;
    int end = qMin(start + pageSize, total);

    // 5. 构建 Markdown 回复
    QString result;
    result += QString("📦 **插件列表** (第 %1/%2 页，共 %3 个，筛选后 %4 个)\n\n")
                  .arg(page)
                  .arg(totalPages)
                  .arg(m_allPlugins.size())
                  .arg(total);

    for (int i = start; i < end; ++i) {
        const PluginInfo2& info = filtered[i];
        int displayIndex = i + 1;  // 当前页内的序号（从1开始）

        // 检查是否已安装
        bool installed = isPluginInstalled(info.id);

        // 插件名 + 安装链接（使用上下文参数，保证点击后能准确定位）
        // 链接格式： #安装插件 序号 页码 搜索词 标签
        // 注意：如果搜索词或标签包含空格，需用引号或URL编码，为简单，我们假设无空格
        QString installLink;
        if (installed) {
            installLink = "[已安装]";
        } else {
            // 构建参数：将当前过滤条件传递过去
            QString argsForLink;
            argsForLink += QString::number(displayIndex) + " " + QString::number(page);
            if (!keyword.isEmpty()) argsForLink += " " + keyword;
            if (!tagFilter.isEmpty()) argsForLink += " 标签:" + tagFilter;
            installLink = QString("[安装](#安装插件 %1)").arg(argsForLink);
        }

        result += QString("**%1.%2** %3\n")
                      .arg(displayIndex)
                      .arg(info.name,installLink);


        // 第二行：类型和说明
        result += QString("> [%1] %2\n\n")
                      .arg(info.type.isEmpty() ? "未知" : info.type, info.remark);
    }

    // 6. 翻页提示
    if (page < totalPages) {
        result += QString("输入 `#插件市场 %1` 查看下一页\n").arg(page + 1);
    }
    if (page > 1) {
        result += QString("输入 `#插件市场 %1` 查看上一页\n").arg(page - 1);
    }
    if (!keyword.isEmpty() || !tagFilter.isEmpty()) {
        result += "提示：使用 `#插件市场 页码 搜索词` 或 `#插件市场 页码 标签:标签名` 筛选\n";
    }

    return result;
}


QString onMessageReceived(const QString& msg) {
    if (msg.startsWith("#插件市场")) {
        return handlePluginMarket(msg);
    }
    else if (msg.startsWith("#安装插件")) {
        // ---------- 处理安装，不单独写函数，内联 ----------
        QString cmd = msg.trimmed();
        QString args = cmd.mid(QString("#安装插件").length()).trimmed();
        QStringList parts = args.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) {

            return "安装失败：缺少插件序号";
        }

        bool ok;
        int localIndex = parts[0].toInt(&ok);
        if (!ok || localIndex < 1) {

            return "安装失败：无效的序号";
        }

        QString keyword;
        QString tagFilter;

        if (parts.size() > 2) {
            keyword = parts[2];
        }
        if (parts.size() > 3) {
            tagFilter = parts[3];
        }

        // 确保插件列表已加载
        if (m_allPlugins.isEmpty()) {
            QString err;
            if (!updateGlobalPluginList(err)) {

                return "获取插件列表失败：" + err;
            }
        }

        // 重建过滤列表（与市场指令逻辑一致）
        QList<PluginInfo2> filtered;
        for (const PluginInfo2& info : std::as_const(m_allPlugins)) {
            if (!tagFilter.isEmpty()) {
                bool tagMatched = false;
                for (const QString& tag : info.tags) {
                    if (tag.contains(tagFilter, Qt::CaseInsensitive)) {
                        tagMatched = true;
                        break;
                    }
                }
                if (!tagMatched) continue;
            }
            if (!keyword.isEmpty()) {
                if (!info.name.contains(keyword, Qt::CaseInsensitive) &&
                    !info.remark.contains(keyword, Qt::CaseInsensitive)) {
                    continue;
                }
            }
            filtered.append(info);
        }

        // 检查序号
        if (localIndex < 1 || localIndex > filtered.size()) {

            return QString("安装失败：序号 %1 超出范围（当前筛选结果共 %2 个）")
                .arg(localIndex).arg(filtered.size());
        }

        const PluginInfo2& target = filtered[localIndex - 1];

        // 检查是否已安装
        if (isPluginInstalled(target.id)) {
            return "该插件已安装，无需重复安装。";
        }
        return "安装插件没写呢";
    }
    return "";
}

bool extractLoadParams(const QString &cmd, int &type, QString &path) {
    QStringList parts = cmd.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 3) return false;
    bool ok;
    type = parts[1].toInt(&ok);
    if (!ok) return false;
    path = parts[2];
    return true;
}
QString checkUpdate(const MessageEvent &ev);
QString getLatestDownloadUrl();
bool __cqkj=false;
QString upadmin(AccountInfo *info,MessageEvent &ev)
{

    if(!g_admin.contains(ev.user)) return QString();
    // ---------- 启用插件 ----------
    if (ev.msg.startsWith("#启用插件")) {
        QString index_ser;
        int cnt = extractParams(ev.msg, "#启用插件", 0, index_ser);
        if (cnt == -1) return "[启用插件] 缺少序号";
        int index = index_ser.toInt();
        if (index < 0 || index >= m_pluginList.size()) {
            return QString("错误：无效的插件序号，当前共 %1 个插件").arg(m_pluginList.size());
        }
        QMetaObject::invokeMethod(qApp, [index]() {
            pluginPage->Enabled_Plugin(m_pluginList[index]); // 假设返回 bool

        }, Qt::BlockingQueuedConnection); // 注意：使用 BlockingQueuedConnection 会阻塞当前线程直到 lambda 执行完毕
        return QString("已启用插件 %1").arg(m_pluginList[index].name);
    }
    // ---------- 禁用插件 ----------
    if (ev.msg.startsWith("#禁用插件")) {
        QString index_ser;
        int cnt = extractParams(ev.msg, "#禁用插件", 0, index_ser);
        if (cnt == -1) return "[禁用插件] 缺少序号";
        int index = index_ser.toInt();
        if (index < 0 || index >= m_pluginList.size()) {
            return QString("错误：无效的插件序号，当前共 %1 个插件").arg(m_pluginList.size());
        }
        if (QThread::currentThread() == qApp->thread()) {
            pluginPage->disable_Plugin(m_pluginList[index]);
        } else {
            QMetaObject::invokeMethod(qApp, [index]() {
                pluginPage->disable_Plugin(m_pluginList[index]);
            }, Qt::BlockingQueuedConnection);
        }
        return  QString("已禁用插件 %1").arg(m_pluginList[index].name);
    }
    // ---------- 重载插件 ----------
    if (ev.msg.startsWith("#重载插件")) {
        QString index_ser;
        int cnt = extractParams(ev.msg, "#重载插件", 0, index_ser);
        if (cnt == -1) return "[重载插件] 缺少序号";
        int index = index_ser.toInt();
        if (index < 0 || index >= m_pluginList.size()) {
            return QString("错误：无效的插件序号，当前共 %1 个插件").arg(m_pluginList.size());
        }
        if (QThread::currentThread() == qApp->thread()) {
            pluginPage->Reload_Plugin(index); // 注意参数可能是序号
        } else {
            QMetaObject::invokeMethod(qApp, [index]() {
                pluginPage->Reload_Plugin(index);
            }, Qt::BlockingQueuedConnection);
        }
        return QString("已重载插件 %1").arg(m_pluginList[index].name);
    }
    // ---------- 卸载插件 ----------
    if (ev.msg.startsWith("#卸载插件")) {
        QString index_ser;
        int cnt = extractParams(ev.msg, "#卸载插件", 0, index_ser);
        if (cnt == -1) return "[卸载插件] 缺少序号";
        int index = index_ser.toInt();
        if (index < 0 || index >= m_pluginList.size()) {
            return QString("错误：无效的插件序号，当前共 %1 个插件").arg(m_pluginList.size());
        }
        QString name = m_pluginList[index].name;
        if (QThread::currentThread() == qApp->thread()) {
            pluginPage->uninstall_Plugin2(index);
            pluginPage->savePlugins();
        } else {
            QMetaObject::invokeMethod(qApp, [index]() {
                pluginPage->uninstall_Plugin2(index);
                pluginPage->savePlugins();
            }, Qt::BlockingQueuedConnection);
        }

        return QString("已卸载插件 %1").arg(name) ;
    }
    QString rrrr = onMessageReceived(ev.msg);
    if(!rrrr.isEmpty()) return rrrr;
    // ---------- 加载插件 ----------
    if (ev.msg.startsWith("#加载插件")) {

        QString path = ev.msg.mid(QString("#加载插件").length()).trimmed();
        if (path.isEmpty()) {
            return "错误：用法 #加载插件 <路径> dll 类型需指定dll js python只需要指定文件夹";
        }

        // ----- 自动检测类型 -----
        QFileInfo info(path);
        if (!info.exists()) {
            return QString("错误：路径不存在 - %1").arg(path);
        }

        int type = -1;  // 最终确定的类型
        if (info.isFile() && info.suffix().compare("dll", Qt::CaseInsensitive) == 0) {
            // DLL 文件，默认以 64 位方式加载（内部自动降级 32 位）
            type = 1;
        } else if (info.isDir()) {
            // 检查入口文件，优先 Python
            QString pyPath = path + "/main.py";
            QString jsPath = path + "/main.js";
            if (QFile::exists(pyPath)) {
                type = 0;   // Python
            } else if (QFile::exists(jsPath)) {
                type = 3;   // JS
            } else {
                return QString("错误：文件夹 %1 中没有 main.py 或 main.js，无法加载").arg(path);
            }
        } else {
            return QString("错误：路径不是 DLL 文件或文件夹 - %1").arg(path);
        }

        // ----- 执行加载（在主线程）-----
        QString resultMsg;
        if (QThread::currentThread() == qApp->thread()) {
            QList<int> dummy;
            resultMsg = pluginPage->LoadPlugin(path, type, true, dummy);
        } else {
            QMetaObject::invokeMethod(qApp, [path, type, &resultMsg]() {
                QList<int> dummy;
                resultMsg = pluginPage->LoadPlugin(path, type, true, dummy);
            }, Qt::BlockingQueuedConnection);
        }

        if (!resultMsg.isEmpty())
            return "加载插件失败，错误内容："+resultMsg;
        else {
            pluginPage->savePlugins();
            return "加载成功 发送 [#插件列表]() 查看";

        }
    }
    if (ev.msg == "#扫描插件") {
        QString result;
        result.reserve(4096);
        result.append("**📂 可用插件文件（点击加载）**\n");

        QStringList dirs = {"plugin", "plugins"};
        for (const QString &dirName : dirs) {
            QDir dir(dirName);
            if (!dir.exists()) {
                result.append(QString("\n**%1** 目录不存在\n").arg(dirName));
                continue;
            }

            result.append(QString("\n**%1** 目录:\n>").arg(dirName));

            const QStringList dllFiles = dir.entryList(QStringList() << "*.dll", QDir::Files, QDir::Name);
            const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

            if (dllFiles.isEmpty() && subDirs.isEmpty()) {
                result.append("  (空)\n");
                continue;
            }

            // DLL 文件
            for (const QString &dll : dllFiles) {
                QString fullPath = dirName + "/" + dll;
                result.append(QString("  `%1` [加载](#加载插件 %2)\n").arg(dll, fullPath));
            }

            // 文件夹
            for (const QString &sub : subDirs) {
                QString fullPath = dirName + "/" + sub;
                QString pyPath = fullPath + "/main.py";
                QString jsPath = fullPath + "/main.js";
                bool hasPy = QFile::exists(pyPath);
                bool hasJs = QFile::exists(jsPath);


                if (hasPy) {
                    result.append(QString("  `%1`").arg(sub));
                    result.append(QString(" [加载](#加载插件 %1/)").arg(fullPath));
                } else if (hasJs) {
                    result.append(QString("  `%1`").arg(sub));
                    result.append(QString(" [加载](#加载插件 %1/)").arg(fullPath));
                } else {
                    continue;
                }
                result.append("\n");
            }
        }

        result.append("\n💡 点击加载按钮自动识别类型：DLL 或 Python/JS（优先 Python）");
        return result;
    }
    if(ev.msg=="webui")
    {
        if(ev.type == 2 || ev.type == 3)
        {
            int port=g_config["webhook_p"].toInt();
            return QString("%1://%2:%3/webui/index.html?token=%4").arg(g_config["SSL"].toBool() ? "https" : "http",g_ip).arg(port).arg(ws_token);
        }
        return "请在 私聊环境获取链接 否则其他人登录可恶意操作";
    }
    if(ev.msg=="重启框架")
    {
        return "发送[#确认重启框架]() 来重启";
    }
    if(ev.msg=="#确认重启框架")
    {
        QJsonObject obj;
        obj["msgid"] = ev.msgId;
        obj["type"]   = ev.type;
        obj["openid"] = ev.groupId;
        obj["time"]   = QDateTime::currentSecsSinceEpoch();
        obj["appid"]  = ev.appid;
        g_config["zdcq"] = obj;
        saveConfig();   // 必须同步写入磁盘
        QMetaObject::invokeMethod(qApp, []() {

            for (auto &c : m_botClients) {
                c->stop();   // 假设 stop 是同步的，会等待数据发送完毕
            }
            for (const auto &c : std::as_const(g_botdb)) {
                c->close();   // 假设 stop 是同步的，会等待数据发送完毕
            }
            #ifdef _WIN32
            if (bridge) {
                bridge->writeResponseToBlock(1, "{\"type\":6}");
                bridge->stopServer();   // 同步停止
            }
            #endif
            for(const auto &db : std::as_const(g_logdb))
            {
                db->close();
            }
            pluginPage->foruninstall_Plugin();
            QThreadPool::globalInstance()->waitForDone();
            QString program = QCoreApplication::applicationFilePath();
            QStringList args = QCoreApplication::arguments();
            QProcess::startDetached(program, args);

            qApp->quit();
        }, Qt::QueuedConnection);



        return "正在重启";
    }
    if(ev.msg=="#更新框架")
    {

        return checkUpdate(ev);
    }
    if(ev.msg=="#确认更新框架")
    {
        #ifdef _WIN32
        if(!__cqkj) return "请发送 [#更新框架]() 来检查是否需要更新";

            QString appDir = QCoreApplication::applicationDirPath();
            QString exePath = QDir(appDir).filePath("纯白铃铛-下崽器.exe");
            if (!QFile::exists(exePath))
                return "纯白铃铛-下崽器 不存在 或 运行失败 需要这个才能更新框架";

            std::wstring exe = exePath.toStdWString();
            std::wstring args = L" 啥也没";   // 注意参数前有空格
            std::wstring cmdLine = exe + args;

            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            if (CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
                               CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
                QJsonObject obj;
                obj["msgid"] = ev.msgId;
                obj["type"]   = ev.type;
                obj["openid"] = ev.groupId;
                obj["time"]   = QDateTime::currentSecsSinceEpoch();
                obj["appid"]  = ev.appid;
                g_config["zdcq"] = obj;
                saveConfig();   // 必须同步写入磁盘
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

        // 获取最新 release 中带 linux 的压缩包下载链接
        QString downloadUrl = getLatestDownloadUrl();
        if (downloadUrl.isEmpty()) {
            return "未找到 Linux 版本的更新包，请检查仓库 release";
        }

        // 生成临时 Shell 脚本
        QString scriptPath = QDir::tempPath() + "/update.sh";
        QFile script(scriptPath);
        if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return "无法创建更新脚本，请检查临时目录权限";
        }

        QString appPath = QCoreApplication::applicationFilePath();
        QString appDir = QCoreApplication::applicationDirPath();

        // 根据检测到的解压工具生成对应命令
        QString unzipCmd;
        if (unzipTool == "7z") {
            unzipCmd = QString("7z x /tmp/update.zip -y -o'%1'").arg(appDir);
        } else {
            unzipCmd = QString("unzip -o /tmp/update.zip -d '%1'").arg(appDir);
        }

        // 保存上下文到 g_config（和 Windows 一样）
        QJsonObject obj;
        obj["msgid"]  = ev.msgId;
        obj["type"]   = ev.type;
        obj["openid"] = ev.groupId;
        obj["time"]   = QDateTime::currentSecsSinceEpoch();
        obj["appid"]  = ev.appid;
        g_config["zdcq"] = obj;
        saveConfig();   // 必须同步写入磁盘

        // 构建脚本内容
        QString scriptContent = QString(
                                    "#!/bin/bash\n"
                                    "sleep 2   # 等待主程序完全退出\n"
                                    "echo '下载更新包...'\n"
                                    "wget -O /tmp/update.zip '%1' || curl -L -o /tmp/update.zip '%1'\n"
                                    "if [ $? -ne 0 ]; then echo '下载失败'; exit 1; fi\n"
                                    "echo '解压中...'\n"
                                    "%2\n"
                                    "if [ $? -ne 0 ]; then echo '解压失败'; exit 1; fi\n"
                                    "rm -f /tmp/update.zip\n"
                                    "echo '更新完成，重启程序...'\n"
                                    "exec '%3' &\n"
                                    "exit 0\n"
                                    ).arg(downloadUrl, unzipCmd, appPath);

        script.write(scriptContent.toUtf8());
        script.close();

        // 添加执行权限
        QFile::setPermissions(scriptPath,
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                  QFile::ReadGroup | QFile::ExeGroup |
                                  QFile::ReadOther | QFile::ExeOther);

        // 启动脚本（detach）
        if (!QProcess::startDetached("/bin/bash", QStringList() << scriptPath)) {
            QFile::remove(scriptPath);
            return "无法启动更新脚本";
        }

        // 主程序退出
        QCoreApplication::quit();
        return QString(); // 成功
        #endif
    }
    if(ev.msg=="关闭webui")
    {
        setA->set_webui(false);
        return "已关闭webui";
    }
    if(ev.msg=="开启webui")
    {
        setA->set_webui(true);
        return "已开启webui";
    }
    if(ev.msg=="botlist")
    {
        QString res;
        res.append("#机器人列表🌴:\n");
        for (const auto& bot : std::as_const(m_accounts)) {
            QString status = bot->online ? "[在线](logout %1)" : "[离线](login %1)";
            QString log;
            if(bot->unid.isEmpty())
            {
                log = QString(">![#24px #24px](%1) %2(%3) %4\n").arg(bot->avatarPath,bot->nickname,bot->appid,status.arg(bot->appid));
            }else
                log = QString(">![#24px #24px](https://thirdqq.qlogo.cn/qqapp/%1/%2/100) %3(%4) %5\n").arg(bot->appid,bot->unid,bot->nickname,bot->appid,status.arg(bot->appid));
            res.append(log);
        }
        res.append("#可用指令：\n[login](login {appid}) 登录指定机器人\n[logout](logout {appid}) 下线指定机器人\n"
                   "[delbot](delbot {appid}) 删除一个机器人\n[addbot](addbot {appid,secret,type,markdown,wsIntents}) 添加一个机器人\n[boterr](boterr {appid}) 查看最后错误日志");
        return  res;
    }else if(ev.msg.startsWith("boterr"))
    {
        QString appid_str;
        int cnt = extractParams(ev.msg, "boterr", 0, appid_str);
        if (cnt == -1) return "boterr 缺少appid参数";
        int appid = appid_str.toInt();
        if (g_CW.contains(appid))
        {
            auto *cw = g_CW[appid];
            return "以下是机器人最后错误:\n> "+ cw->m_info->err+"\n\n如果是空代表无错误";
        }else return "查看最后错误的appid 未添加在 账号列表";
    }else if(ev.msg.startsWith("addadmin")){
        QString appid_str,user;
        int cnt = extractParams(ev.msg, "boterr", 0, appid_str,user);
        if (cnt <= 1) return "addadmin 缺少appid参数 或 ID 参数";
        int appid = appid_str.toInt();
        int index = accinfo(appid);
        if(index==-1) return "appid对应机器人不存在";
        QString &admin = m_accounts[index]->admin;
        if(admin.contains(user)) return "对应id已经添加到 管理列表";
        admin.append(" ");
        admin.append(user);
        admin.replace("  "," ");
        accountPage->saveAccounts( m_accounts[index].get());
        return QString("添加 %1 为 %2 管理员").arg(user,m_accounts[index]->nickname);


    }else if(ev.msg.startsWith("deladmin")){
        QString appid_str,user;
        int cnt = extractParams(ev.msg, "boterr", 0, appid_str,user);
        if (cnt <= 1) return "addadmin 缺少appid参数 或 ID 参数";
        int appid = appid_str.toInt();
        int index = accinfo(appid);
        if(index==-1) return "appid对应机器人不存在";
        QString &admin = m_accounts[index]->admin;
        if(!admin.contains(user)) return "对应id不存在 管理列表";

        admin.remove(user);
        admin.replace("  "," ");
        admin.replace("  "," ");
        accountPage->saveAccounts( m_accounts[index].get());
        return QString("从 %1 删除 %2 管理员").arg(m_accounts[index]->nickname,user);

    }
    return QString();
}

QString admin_zl(AccountInfo *info,MessageEvent &ev)
{

    bool upad = g_admin.contains(ev.user);
    if(!upad){

        if(!info->admin.contains(ev.user)){
            if(!info->cbl) return QString();
            if(ev.msg=="#纯白铃铛" || ev.msg.startsWith("纯白铃") )
            {
                return             "**普通权限**\n>[我的ID]() 获取ID\n>[代管列表]() 查看本群代管\n\n"
                        "**群管理**\n>"
                        "[撤回]() <艾特> <条数> 可能有接口频率限制(不指定用户时撤回机器人)\n>"
                        "[禁言]() <艾特> <秒> \n>"
                        "[解禁]() <艾特> 解除禁言某个人\n>"
                        "[免验证]() <艾特> 删除某人的验证\n>"
                        "[一键解禁]() 批量解除\n>"
                        "[禁言列表]() 获取禁言列表\n>"
                        "[本群状态]() 查看开启列表\n>"
                        "[开入群验证]() | [关入群验证]()\n>"
                        "[获取加群列表]() 获取申请加群列表\n"
                        "[添加本群代管]() | [删除本群代管]()\n>"
                        "[开申请加群提示]() | [关申请加群提示]()\n>"
                        "[开自动同意加群]() | [关自动同意加群]()\n>"
                        "[设置自动同意加群答案]()\n>"
                        "[开刷屏检测](设置刷屏检测) | [关刷屏检测](取消刷屏检测)\n>"
                        "[开入群提示](设置入群提示) | [关入群提示](取消入群提示)\n>[开退群提示](设置退群提示) | [关退群提示](取消退群提示)\n\n---\n\n需要群主或者管理才能触发指令";
            }
            return QString();
        }
    }
    if(ev.msg=="#纯白铃铛" || ev.msg.startsWith("纯白铃"))
    {
        int js=0,dll=0,dll32=0,python=0;
        for(const auto & p :std::as_const(m_pluginList))
        {
            if(p.type==0) python++;
            else if(p.type ==1) dll++;
            else if(p.type ==2) dll32++;
            else if(p.type ==3) js++;
        }
        return
            QString(
            "**普通权限**\n>[我的ID]() 获取ID\n>[代管列表]() 查看本群代管\n\n"
            "**群管理**\n>"
            "[撤回]() <艾特> <条数> 可能有接口频率限制(不指定用户时撤回机器人)\n>"
            "[禁言]() <艾特> <秒> \n>"
            "[解禁]() <艾特> 解除禁言某个人\n>"
           "[免验证]() <艾特> 删除某人的验证\n>"
           "[一键解禁]() 批量解除\n>"
           "[禁言列表]() 获取禁言列表\n>"
           "[本群状态]() 查看开启列表\n>"
           "[开入群验证]() | [关入群验证]()\n>"
            "[获取加群列表]() 获取申请加群列表\n"
            "[添加本群代管]() | [删除本群代管]()\n>"
            "[开申请加群提示]() | [关申请加群提示]()\n>"
            "[开自动同意加群]() | [关自动同意加群]()\n>"
            "[设置自动同意加群答案]()\n>"
            "[开刷屏检测](设置刷屏检测) | [关刷屏检测](取消刷屏检测)\n>"
            "[开入群提示](设置入群提示) | [关入群提示](取消入群提示)\n>[开退群提示](设置退群提示) | [关退群提示](取消退群提示)\n\n"

            "**管理**\n"
            ">[取]() 获取某条信息原始数据\n"
            ">[md]() 复读指令\n"
            ">[重启框架]() 字面意思\n"
            ">[接口测试]() 字面意思\n\n"
            ">[数据统计]() 查看机器人收发统计\n\n"
            "**插件相关**\n"
            ">JS:%1 | Python:%4\nDLL:%2 | DLL32:%3\n>"
            "[#插件列表]()\n>[#插件启用]() <序号>\n>[#插件禁用]() <序号>\n\n"
            "**其他**\n"
            ">[开启拟人]() | [关闭拟人]()\n"
            ">[%5]() 启用或取消白名单系统\n"
            ">[%6]() {群id|好友id} 设置白名单\n"
            ">[%7]() {群id|好友id} 取消白名单\n\n"
            "**机器人设置**\n"
            ">[login]() <appid> 登录某个机器人\n"
            ">[logout]() <appid> 下线某个bot\n"
            ">[delbot]() <appid> 删除某个bot\n"
            "\n\n---\n\n"
            "**下面为超级管理员可用**\n"
            ">[webui]() 网页ui\n"
            "[关闭webui]() | [开启webui]()\n\n"
            "**插件管理**\n>"
            "[#启用插件]() <序号>\n>"
            "[#禁用插件]() <序号>\n>"
            "[#重载插件]() <序号>\n>"
            "[#卸载插件]() <序号>\n>"
            "[#加载插件]() <路径>\n>"
            "[#扫描插件]()\n\n"

            "**机器人管理**\n"

            ">[boterr]() <appid> 查看最后登录错误\n"
            ">[botlist]() 查看框架机器人列表\n"
            ">[addbot]() <appid> <secret> {登录类型0 ws|1 webhook} {启用md} {事件订阅}\n\n"
            ">[addadmin]() <appid> <ID> 为某个bot添加一个管理员\n"
            ">[deladmin]() <appid> <ID> 删除某个bot一个管理员\n\n"
            "**框架相关**\n"
            ">[#重启框架]()\n>[#更新框架]()\n"

                       ">'<>'为必填 '{}'可选\n>以上指令需要艾特 机器人才能触发")
                        .arg(js).arg(dll).arg(dll32).arg(python)

                        .arg(info->bai_qy.isEmpty() ? "未设置" : info->bai_qy,
                 info->bai_sr.isEmpty() ? "未设置" : info->bai_sr,
                 info->bai_sc.isEmpty() ? "未设置" : info->bai_sc);
    }
    if (ev.msg.contains("取")) {
        QRegularExpression nonChinese("[^\\x{4E00}-\\x{9FA5}]");
        QString onlyChinese = ev.msg;
        onlyChinese.remove(nonChinese);  // 移除所有非基本汉字
        if (onlyChinese == "取") {
            return "```python\n"+ev.raw+"\n```";
        }
    }
    if(!ev.at_you) return QString();
    if (ev.msg == "#插件列表") {
        // 你已有的代码，保持不变
        QString res;
        res.reserve(1024);
        res.append("**插件列表**\n");
        for (int i = 0; i < m_pluginList.size(); ++i) {
            auto &p = m_pluginList[i];
            res.append(">");
            res.append(QString::number(i));
            res.append(".");
            if (p.type == 0)  res.append("[Py] ");
            else if (p.type == 1) res.append("[x64] ");
            else if (p.type == 2) res.append("[x32] ");
            else if (p.type == 3) res.append("[JS] ");
            res.append(p.name);

            if (!p.appid.contains(ev.appid))
                res.append(" [禁用](#插件禁用");
            else
                res.append(" [启用](#插件启用");
            res.append(QString::number(i));
            res.append(") ");
            res.append("[卸载](#卸载插件");
            res.append(QString::number(i));
            res.append(")\n");
        }
        res.append("可用指令：\n[#插件列表]()\n>[#插件启用]() <序号>\n>[#插件禁用]()<序号>\n\n**需超管权限**\n>[#重载插件]() <序号>\n>[#启用插件]() <序号>\n>[#禁用插件]() <序号>\n>[#卸载插件]() <序号>\n>[#加载插件]() <路径>\n[#插件市场]()\n[#扫描插件]() 查看现有插件");
        return res;
    }

    if (ev.msg.startsWith("#插件启用")) {

        QString index_ser;
        int cnt = extractParams(ev.msg, "#插件启用", 0, index_ser);
        if (cnt == -1) return "[插件启用] 缺少序号";
        int index = index_ser.toInt();
        if (index < 0 || index >= m_pluginList.size()) {
            return QString("错误：无效的插件序号，当前共 %1 个插件").arg(m_pluginList.size());
        }
        if(!m_pluginList[index].appid.contains(ev.appid)) return "当前插件已经对当前机器人是 启用状态";
        m_pluginList[index].appid.removeAll(ev.appid);
        pluginPage->savePlugins();
        /*
        QMetaObject::invokeMethod(qApp, [index]() {
            pluginPage->Enabled_Plugin(m_pluginList[index]); // 假设返回 bool

        }, Qt::BlockingQueuedConnection); // 注意：使用 BlockingQueuedConnection 会阻塞当前线程直到 lambda 执行完毕
        */
        return QString("已启用插件 %1").arg(m_pluginList[index].name);
    }
    if (ev.msg.startsWith("#插件禁用")) {
        QString index_ser;
        int cnt = extractParams(ev.msg, "#插件禁用", 0, index_ser);
        if (cnt == -1) return "[插件禁用] 缺少序号";
        int index = index_ser.toInt();
        if (index < 0 || index >= m_pluginList.size()) {
            return QString("错误：无效的插件序号，当前共 %1 个插件").arg(m_pluginList.size());
        }
        if(m_pluginList[index].appid.contains(ev.appid)) return "当前插件已经对当前机器人是 禁用状态";
        m_pluginList[index].appid.append(ev.appid);
        pluginPage->savePlugins();
        return  QString("已禁用插件 %1").arg(m_pluginList[index].name);
    }
    if (ev.msg== "数据统计") {
        return  info->StatT;
    }
    if(ev.msg=="接口测试")
    {
        QString resu;

        if(m_botClients.contains(info->appid_int))
        {
            resu.reserve(1024);
            QQBotClient *client = m_botClients[info->appid_int];

            resu.append("\n\n**获取用户信息**\n>");
            resu.append( client->get_groups_members(ev.groupId,ev.user));


            resu.append("\n\n**获取群列表**\n>");
            resu.append(client->get_groups_list(5,0));

            resu.append("\n\n**获取好友列表**\n>");
            resu.append(client->get_groups_list(5,0));

            resu.append("\n\n**获取群成员列表**\n>");
            resu.append(client->get_members_list(ev.groupId,5,0));

            resu.append("\n\n**移除群成员**\n>");
            resu.append(client->del_members(ev.type,ev.groupId,info->unid));
            resu.append("\n\n**禁言列表**\n>");
            resu.append(client->getGroupRestrictChatSetting(ev.groupId));

        }
        return resu;
    }
    if(ev.msg=="取")
    {
        QJsonParseError err;
        QJsonDocument dom = QJsonDocument::fromJson(ev.raw.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && !dom.isNull()) {
            return dom.toJson(QJsonDocument::Indented);
        }
        return "```json\n"+ ev.raw+"\n```\n";  // 若 ev.raw 是 QString，需要转为 QByteArray
    }
    if(ev.msg.startsWith("md"))
    {
        QString text = ev.msg.mid(2).trimmed(); // 删除"md"并去除前导空白
        if (text.startsWith("#python")) {
            text = python_code3(text, ev);
        }
        return text;
    }
    if (ev.msg == "开启拟人" || ev.msg == "关闭拟人") {
        if (ev.type != 0 && ev.type != 1) {
            return "除群和频道外，其他类型无需手动操作";
        }



        bool isEnabled = (ev.bitmap & BIT_Ainiren) == 1;  // 当前拟人状态

        if (ev.msg == "开启拟人") {
            if (isEnabled) {
                return "本群已开启拟人，无需重复开启";
            }

            auto *db = g_botdb[ev.appid];
            ev.bitmap |= BIT_Ainiren;   // 置位

            GroupRecord2 gr;
            db->getGroupInfo(ev.groupId, gr);
            gr.bitmap = ev.bitmap;
            db->addGroup(ev.groupId, gr);
            return "拟人已开启";
        } else { // 关闭拟人
            if (!isEnabled) {
                return "本群未开启拟人，无需关闭";
            }
            ev.bitmap &= ~BIT_Ainiren;  // 清除位
            GroupRecord2 gr;
            auto *db = g_botdb[ev.appid];
            db->getGroupInfo(ev.groupId, gr);
            gr.bitmap = ev.bitmap;
            db->addGroup(ev.groupId, gr);
            return "拟人已关闭";
        }
    }
    if(ev.msg.startsWith("login"))
    {
        QString appid_str;
        int cnt = extractParams(ev.msg, "login", 0, appid_str);
        if (cnt == -1) return "login 缺少appid参数";
        int appid = appid_str.toInt();

        if (g_CW.contains(appid))
        {
            auto *cw = g_CW[appid];
            if(cw->m_info->online) return "机器人已经在线 无须重复登录";
            if(!cw->m_info->admin.contains(ev.user) ){
                if(!upad) return "你非该机器人管理员 或者超管 不能执行上线";
            }
            QMetaObject::invokeMethod(qApp, [=]() {
                cw->onLoginButton();
            });
            doWork(1000);
            if(cw->m_info->err.isEmpty())
                return QString("#提交登录\n请发送 [botlist]() 获取登录状态\n[boterr](boterr %1) 查看最后错误").arg(appid);
            return QString("#提交登录\n请发送 [botlist]() 获取登录状态\n[boterr](boterr %1) 查看最后错误\n疑似登录错误：\n>%2").arg(appid).arg(cw->m_info->err);
        }else
        {
            return "要登录的 appid 在框架账号列表不存在";
        }
    }else if(ev.msg.startsWith("logout"))
    {
        QString appid_str;
        int cnt = extractParams(ev.msg,"logout", 0, appid_str);
        if (cnt == -1) return "login 缺少appid参数";
        int appid = appid_str.toInt();
        if (g_CW.contains(appid))
        {
            auto *cw = g_CW[appid];
            if(!cw->m_info->online) return "机器人未在线 无须下线";
            if(!cw->m_info->admin.contains(ev.user) ){
                if(!upad) return "你非该机器人管理员 或者超管 不能执行下线";
            }
            QMetaObject::invokeMethod(qApp, [=]() {
                cw->onLoginButton();
            });
            return "下线成功 请发送 [botlist]() 获取登录状态";
        }else return "要下线的 appid 在框架账号列表不存在";

    }else if(ev.msg.startsWith("delbot"))
    {
        QString appid_str;
        int cnt = extractParams(ev.msg, "delbot", 0, appid_str);
        if (cnt == -1) return "delbot 缺少appid参数";
        int appid = appid_str.toInt();

        if (g_CW.contains(appid))
        {
            auto *cw = g_CW[appid];
            if(!cw->m_info->admin.contains(ev.user) ){
                if(!upad) return "你非该机器人管理员 或者超管 不能执行删除";
            }

            QMetaObject::invokeMethod(qApp, [=]() {
                accountPage->onDeleteAccount(appid);
            });
            return "删除成功";
        }
        return "删除失败 appid 不存在";
    }
    QString res = handleMessage(ev,info);
    if(!res.isEmpty()){
        return res;
    }

    return upadmin(info,ev);
}

//===========================================================================================================================================我猜你在找这个
void Messages(AccountInfo *info,MessageEvent &ev) {

    if(ev.type ==4 && ev.subType==4 || ev.type==5 && ev.subType==6)
    {
        if(!info->welcomeMsg.isEmpty())
        {
            QString ret = info->welcomeMsg;
            if(ret.startsWith("#python")) ret = python_code(ret,ev);
            if(m_botClients.contains(info->appid_int))
            {
                QQBotClient *client = m_botClients[info->appid_int];
                QString text = "[欢迎语]";
                client->send_msgAsync(ev.type,ev.groupId,text,ret,ev.msgId);
                return;
            }

        }
    }
    if(ev.type ==18)
    {

        if(ev.bitmap & BIT_AUTO_JOJN)
        {
            auto *db = g_botdb[info->appid_int];
            GroupRecord2 gid;
            db->getGroupInfo(ev.groupId, gid);
            QString text = gid.autoref;
            if(text.isEmpty()){
                QQBotClient *client = m_botClients[info->appid_int];

                QString t2 = client->approveGroupJoinRequest(ev.groupId,ev.user,true,ev.callbackId);
                if(t2=="{}") return;

            }else if(text.contains(","))
            {
                QStringList list = text.split(",");
                for(const auto & str : std::as_const(list))
                {
                    if(ev.extra.contains(str))
                    {
                        QQBotClient *client = m_botClients[info->appid_int];
                        QString t2 = client->approveGroupJoinRequest(ev.groupId,ev.user,true,ev.callbackId);
                        if(t2=="{}") return;
                        break;
                    }
                }
            }else if(ev.extra.contains(text)){
                QQBotClient *client = m_botClients[info->appid_int];
                QString t2 = client->approveGroupJoinRequest(ev.groupId,ev.user,true,ev.callbackId);
                if(t2=="{}") return;
            }

        }
        if(ev.bitmap & BIT_AUTO_JOJN_KG){
            if(!info->apply.isEmpty())
            {
                QString ret = info->apply;
                if(ret.startsWith("#python")) ret = python_code(ret,ev);
                if(m_botClients.contains(info->appid_int))
                {
                    QQBotClient *client = m_botClients[info->appid_int];
                    ret.replace("{ID}",ev.user);
                    ret.replace("{ReqId}",ev.callbackId);

                    ret.replace("{申请理由}",ev.msg);
                    ret.replace("{昵称}",ev.nickname);
                    ret.replace("{群昵称}",ev.groupname);
                    if(ret.contains("{头像}"))
                        ret.replace("{头像}",QString("![#48px #48px](https://thirdqq.qlogo.cn/qqapp/%1/%2/100)").arg(ev.appid).arg(ev.user));
                    client->send_msgAsync(ev.type,ev.groupId,"[入群申请]",ret,QString());
                    return;
                }

            }else{
                QQBotClient *client = m_botClients[info->appid_int];
                QString ret = R"(有人申请加群但是未配置文案 点击下面按钮同意加群\n申请人：{ID}#b:#{"keyboard":{"content":{"rows":[{"buttons":[{"action":{"data":"同意加群 {ID} {ReqId}","permission":{"type":2},"type":2,"unsupport_tips":"不支持"},"id":"1","render_data":{"label":"同意","style":1,"visited_label":"按钮1"}}]}]}}}#b:#)";
                ret.replace("{ID}",ev.user);
                ret.replace("{ReqId}",ev.callbackId);
                client->send_msgAsync(ev.type,ev.groupId,"[入群申请]",ret,QString());
            }
        }
    }

    QString ret= 内置指令(ev);
    if(ret.isEmpty())
        ret = admin_zl(info,ev); //机器人管理
    QString text;
    if(!ret.isEmpty())
    {
        QQBotClient *client = m_botClients[info->appid_int];
        if(text.isEmpty()) text = "[私有指令|%1ms]";
        client->send_msgAsync(ev.type,ev.groupId,text,ret,ev.msgId);
        return;
    }


    if(m_blacklist.contains(ev.groupId) || m_blacklist.contains(ev.user)) { //黑名单
        return;
    }
    ret =ruqunhy(info,ev);

    if(ret=="!!!!") return ; //命中违禁词处罚了

    if(!ret.isEmpty() && ret!="*")
    {
        QQBotClient *client = m_botClients[info->appid_int];
        if(text.isEmpty()) text = "[入群提示|%1ms]";
        client->send_msgAsync(ev.type,ev.groupId,text,ret,ev.msgId);
        return;
    }

        ret = keyword->match(info->appid_int,ev.msg);
    if(ret.isEmpty())
        ret = schedule->ppzl(ev,text,info);
    if(ret == "*") return;

    if(!ret.isEmpty())
    {

        if(m_botClients.contains(info->appid_int))
        {
            if(ret.startsWith("#python")) ret = python_code(ret,ev);
            if(!ret.isEmpty())
            {
                QQBotClient *client = m_botClients[info->appid_int];
                if(text.isEmpty()) text = "[关键词匹配|%1ms]";
                client->send_msgAsync(ev.type,ev.groupId,text,ret,ev.msgId);
                return;
            }
        }
    }

    pluginPage->dispatch_message(ev.raw,ev);

    ret = ai_ui->Ai_qx(info,ev);
    if(ret.isEmpty() )
        ret = ai_ui->Ai_post(info,ev);
    if(ret=="*") return;

    if(!ret.isEmpty())
    {
        QQBotClient *client = m_botClients[info->appid_int];
        if(text.isEmpty()) text = "[Ai|%1ms]";
        client->send_msgAsync(ev.type,ev.groupId,text,ret,ev.msgId);
        return;
    }
}


quint32 getTimestampMs() {
    using namespace std::chrono;
    // 取 steady_clock（单调时钟，不受系统时间调整影响，适合做差值计算）
    auto now = steady_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
    return static_cast<quint32>(ms);
}
void addMessage(UserStat &stat,quint32 now) {
    int capacity = stat.buffer.size();
    if (capacity == 0) return; // 防御性检查

    int writePos = (stat.head + stat.count) % capacity;
    stat.buffer[writePos] = now;

    if (stat.count < capacity) {
        stat.count++;
    } else {
        stat.head = (stat.head + 1) % capacity;
    }
}

bool isSpam(const UserStat &stat, quint32 now, int threshold,int times) {
    if (stat.count < threshold) return false;

    int capacity = stat.buffer.size();
    int validCount = 0;
    for (int i = 0; i < stat.count; ++i) {
        int idx = (stat.head + i) % capacity;
        if ((now - stat.buffer[idx]) <= times) {
            if (++validCount >= threshold) return true;
        }
    }
    return false;
}
bool shuaping(AccountInfo *info, const MessageEvent &ev) {

    auto it = info->stat.find(ev.user_int);
    if (it == info->stat.end()) {
        UserStat newStat;
        newStat.buffer.resize(info->tiaoshu + 1);
        it = info->stat.insert(ev.user_int, newStat);
    }
    UserStat &stat = it.value();
    quint32 now = getTimestampMs();
    addMessage(stat,now);
    if (isSpam(stat, now, info->tiaoshu,info->times)) {

        return true;
    }
    return false;
}
QString sendapp(int appid,const QString& text,const PendingGroupEvent &evt)
{
    QString sentText = text;
    sentText.replace("{群名}",evt.gname);


    if(sentText.contains("#python"))
        sentText = python_code4(sentText, appid,evt.user,evt.username);
    else {

        if(sentText.contains("{艾特}"))
        {
            QString nat;
            int estimatedLen = evt.user.size() * 36; // 视实际 ID 长度调整，也可遍历一次精确计算
            nat.reserve(estimatedLen);

            bool first = true;
            for(const auto &id : std::as_const(evt.user))
            {
                if(!first)
                    nat.append(",");
                first = false;
                nat.append("<@").append(id).append(">");
            }

            sentText.replace("{艾特}",nat);
        }

        if(sentText.contains("{ID}"))
        {
            QString result = evt.user.join(",");
            sentText.replace("{ID}",result);
        }
        if(sentText.contains("{数量}")) sentText.replace("{数量}",QString::number(evt.user.size()));

        if(sentText.contains("{头像}"))
        {
            QString nat;
            int estimatedLen = evt.user.size() * 32*4;
            nat.reserve(estimatedLen);
            for(const auto &id : std::as_const(evt.user))
            {
                QString hh = QString("![#24px #24px](https://thirdqq.qlogo.cn/qqapp/%1/%2/100)\n").arg(appid).arg(id);
                nat.append(hh);
            }
            sentText.replace("{头像}",nat);
        }
        if(sentText.contains("{混合}"))
        {
            QString nat;
            int estimatedLen = evt.user.size() * 32*4; // 视实际 ID 长度调整，也可遍历一次精确计算
            nat.reserve(estimatedLen);
            int i=0;
            for(const auto &id : std::as_const(evt.user))
            {
                QString hh = QString("![#24px #24px](https://thirdqq.qlogo.cn/qqapp/%1/%2/100) <@%3>\n").arg(appid).arg(id,id);
                nat.append(hh);
                i++;
            }
            sentText.replace("{混合}",nat);
        }
        if(sentText.contains("{混合x}"))
        {
            QString nat;
            int estimatedLen = evt.user.size() * 32*4; // 视实际 ID 长度调整，也可遍历一次精确计算
            nat.reserve(estimatedLen);
            int i=0;
            for(const auto &id : std::as_const(evt.user))
            {
                QString hh = QString("![#24px #24px](https://thirdqq.qlogo.cn/qqapp/%1/%2/100) %3\n").arg(appid).arg(id,evt.username[i]);
                nat.append(hh);
                i++;
            }
            sentText.replace("{混合}",nat);
        }
        if(sentText.contains("{昵称}"))
        {
            QString nat;
            int estimatedLen = evt.username.size() * 32*4; // 视实际 ID 长度调整，也可遍历一次精确计算
            nat.reserve(estimatedLen);
            for(const auto &username : std::as_const(evt.username))
            {
                nat.append(username).append(" ");
            }

            sentText.replace("{昵称}",nat);
        }

    }
    return sentText;

}
static int parseTimeUnit(const QString &unit) {
    static const QMap<QString, int> unitMap = {
        {"秒", 1}, {"s", 1},
        {"分钟", 60}, {"分", 60}, {"min", 60}, {"m", 60},
        {"月", 30 * 24 * 3600}, {"month", 30 * 24 * 3600}
    };
    return unitMap.value(unit.toLower(), -1);
}

// 解析禁言命令，返回 {ID, 禁言秒数}，失败时秒数返回负数
static std::pair<QString, int> parseBanCommand(const QString &msg) {
    // 1. 预处理：去掉常见的 @ 提及符号，只保留核心内容
    QString clean = msg;
    clean.remove("<@");
    clean.remove(">");
    clean = clean.trimmed();

    // 2. 定义正则（ID必须是32位十六进制）
    QString idPattern = "[0-9a-fA-F]{32}";


    static const QRegularExpression re1(
        "^\\s*禁言\\s*(" + idPattern + ")"           // 组1: ID
                                       "(?:\\s+(\\d+)\\s*(秒|分钟|分|月|s|m|min|month)?)?\\s*$"
        );


    static const QRegularExpression re2(
        "^\\s*禁言\\s*(\\d+)\\s*(秒|分钟|分|月|s|m|min|month)?"  // 组1:数字, 组2:单位
        "\\s+(" + idPattern + ")\\s*$"                           // 组3: ID
        );

    QRegularExpressionMatch match = re1.match(clean);
    bool isTimeFirst = false;

    if (!match.hasMatch()) {
        match = re2.match(clean);
        isTimeFirst = true;
        if (!match.hasMatch()) {
            return {"", -1}; // 解析失败
        }
    }

    QString id;
    int seconds = 30; // 默认30秒

    if (!isTimeFirst) {
        // 情况A：ID在组1，数字在组2，单位在组3
        id = match.captured(1);
        QString numStr = match.captured(2);
        QString unit = match.captured(3);

        if (!numStr.isEmpty()) {
            int multiplier = 1;
            if (!unit.isEmpty()) {
                multiplier = parseTimeUnit(unit);
                if (multiplier < 0) return {"", -2}; // 未知单位
            }
            seconds = numStr.toInt() * multiplier;
            if (seconds <= 0) seconds = 30;
        }
    } else {
        // 情况B：数字在组1，单位在组2，ID在组3
        QString numStr = match.captured(1);
        QString unit = match.captured(2);
        id = match.captured(3);

        int multiplier = 1;
        if (!unit.isEmpty()) {
            multiplier = parseTimeUnit(unit);
            if (multiplier < 0) return {"", -2};
        }
        seconds = numStr.toInt() * multiplier;
        if (seconds <= 0) seconds = 30;
    }

    return {id, seconds};
}
static QPair<QString, int> parseRecallCommand(const QString &msg) {
    // 1. 预处理：去掉 "撤回" 前缀，去除尖括号，去除“条”字
    QString clean = msg;
    clean.remove("撤回");
    clean.remove("<@");
    clean.remove(">");
    clean.remove("条");      // 忽略单位后缀
    clean = clean.trimmed();

    // 2. 定义 ID 正则（32位十六进制）
    const QString idPattern = "[0-9a-fA-F]{32}";

    // 3. 匹配各种组合（顺序任意，数字可选）
    // 模式1: ID 数字 (ID在前)
    static const QRegularExpression re1("^\\s*(" + idPattern + ")\\s+(\\d+)\\s*$");
    // 模式2: 数字 ID (数字在前)
    static const QRegularExpression re2("^\\s*(\\d+)\\s+(" + idPattern + ")\\s*$");
    // 模式3: 只有 ID (无数字 → 默认1条)
    static const QRegularExpression re3("^\\s*(" + idPattern + ")\\s*$");
    // 模式4: 只有数字 (无 ID → 撤回自己的)
    static const QRegularExpression re4("^\\s*(\\d+)\\s*$");

    QRegularExpressionMatch match;

    if ((match = re1.match(clean)).hasMatch()) {
        return qMakePair(match.captured(1), match.captured(2).toInt());
    }
    if ((match = re2.match(clean)).hasMatch()) {
        return qMakePair(match.captured(2), match.captured(1).toInt());
    }
    if ((match = re3.match(clean)).hasMatch()) {
        return qMakePair(match.captured(1), 1);   // 默认1条
    }
    if ((match = re4.match(clean)).hasMatch()) {
        return qMakePair(QString(), match.captured(1).toInt()); // 无ID
    }

    // 完全无法解析
    return qMakePair(QString(), -1);
}


QString ruqunhy(AccountInfo *info, const MessageEvent &ev)
{
    if (ev.type != 0) return QString(); // 仅群聊
    if(ev.user.isEmpty()) return QString();
    if(info->cbl || g_admin.contains(ev.user) || info->admin.contains(ev.user) ) {
        bool admin=false,admin2=false;
        for (int i = 0; i < 20; ++i) { //循环20次 很快 不影响
            if (ev.qid[i] == ev.user_int) {
                admin = true;
                break;
            }
        }
        admin2 = ((ev.member_role ==0) || g_admin.contains(ev.user) || info->admin.contains(ev.user) );
        if(admin2)
        {
            if (ev.msg.startsWith("添加本群代管"))
            {
                QString QID;
                int cnt = extractParams(ev.msg, "添加本群代管", 0, QID);
                if (cnt == -1) return "[添加本群代管] 缺少ID|openid";
                int id = QID.toInt();
                if(QID.size()==32)
                {
                    auto *db = g_botdb[info->appid_int];

                    QString text ;
                    id =  db->getOrUpdateUser(QID,text);
                }

                if(id<=0) return QString("添加的ID：#1 异常 可能从来没记录该用户 请让该用户发言一次").arg(id);
                for (int i = 0; i < 20; ++i) {
                    if (ev.qid[i] == id) {

                        return QString("该ID:%1 已经添加在代管列表").arg(id);
                    }

                }
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                for (int i = 0; i < 20; ++i) { //懒得记录空位置了
                    if (ev.qid[i] == 0) {
                        gid.qid[i]=id;
                        db->addGroup(ev.groupId, gid);
                        return "添加代管成功 该用户目前可以使用机器人 管理员指令";
                    }

                }
                return "添加代管失败 代管上线20位 已经达到上限制";
            }
            if (ev.msg.startsWith("删除本群代管"))
            {
                QString QID;
                int cnt = extractParams(ev.msg, "删除本群代管", 0, QID);
                if (cnt == -1) return "[删除本群代管] 缺少id";
                int id = QID.toInt();
                if(QID.size()==32)
                {
                    auto *db = g_botdb[info->appid_int];
                    QString text ;
                    id =  db->getOrUpdateUser(QID,text);
                }

                if(id<=0) return QString("添加的ID：#1 异常 可能从来没记录该用户 请让该用户发言一次").arg(id);
                for (int i = 0; i < 20; ++i) {
                    if (ev.qid[i] == id) {
                        auto *db = g_botdb[info->appid_int];
                        GroupRecord2 gid;
                        db->getGroupInfo(ev.groupId, gid);
                        gid.qid[i]=0;
                        db->addGroup(ev.groupId, gid);
                        return "删除代管成功";

                    }

                }
                return QString("该ID:%1 不存在在代管列表").arg(id);
            }

        }else if(ev.member_role==1 )
        {

            if (ev.msg.startsWith("添加本群代管"))  return "添加本群代管 这个指令只有群主才能使用";
            if (ev.msg.startsWith("删除本群代管")) return "删除本群代管 这个指令只有群主才能使用";
            admin2 =true;
        }
        if (admin2 || admin ) {
            if (ev.msg == "本群状态")
            {
                QString text;
                text.reserve(256);

                text.append("已开启状态\n>");

                text.append(ev.bitmap & BIT_ruqun ? "❌" : "✅"); //这两个 0是开
                text.append("入群提示\n>");
                text.append(ev.bitmap & BIT_tuiqun ? "❌" : "✅");
                text.append("退群提示\n>");
                text.append(ev.bitmap & BIT_SHUA_P ? "✅" : "❌");
                text.append("刷屏检测\n>");
                text.append(ev.bitmap & BIT_Ainiren ? "✅" : "❌");
                text.append("AI拟人\n>");
                text.append(ev.bitmap & BIT_AI_BAI ? "✅" : "❌");
                text.append("AI白名单\n>");
                text.append(ev.bitmap & BIT_RUQUN_YZ ? "✅" : "❌");
                text.append("入群验证\n>");
                text.append(ev.bitmap & BIT_AUTO_JOJN_KG ? "✅" : "❌");
                text.append("入群申请提示\n>");



                text.append(ev.bitmap & BIT_AUTO_JOJN ? "✅" : "❌");
                text.append("自动同意入群..");
                if(g_botdb.contains(ev.appid)){
                    auto *db = g_botdb[ev.appid];
                    GroupRecord2 gid;
                    db->getGroupInfo(ev.groupId,gid);
                    text.append(QString::number(gid.autoref.size()));
                }



                text.append("\n\n关键词撤回 不在计划内");
                return text;
            }

            if (ev.msg == "获取加群列表")
            {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                auto *c = m_botClients[ev.appid];
                QString js = c->getjoin_request_list(ev.groupId,30);

                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(js.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError) {
                    return "❌ 获取加群列表失败，可能接口超过调用频率";
                }

                QJsonObject root = doc.object();
                QJsonArray list = root["list"].toArray();
                int count = list.size();

                // 构建回复文本（预分配空间）
                QString text;

                text.reserve(100 + count * 100);
                if(count == 0 && js.contains("admin"))
                {
                    text = "📋 **加群申请列表**\n非管理员无法获取 或 频率超限 .."+js;
                }
                else if (count == 0) {
                    text = "📋 **加群申请列表**\n当前暂无加群申请。";
                } else {
                    text += "📋 **加群申请列表**\n";
                    for (int i = 0; i < count; ++i) {
                        QJsonObject item = list[i].toObject();
                        QString username = item["member_openid"].toString();
                        QString applyAt = item["apply_at"].toString();
                        //QString id = item["join_request_id"].toString();
                        text += QString("> **%1** 申请时间：`%2`\n")
                                    .arg(username, applyAt);
                    }
                    // 可选：添加下一页游标（如果需要）
                    text.append("\n[一键同意](一键同意加群) 每次处理30个");
                    QString nextCursor = root["next_cursor"].toString();
                    if (!nextCursor.isEmpty()) {
                        text += QString("\n📌 下一页游标：`%1`").arg(nextCursor);
                    }
                }

                return text;
            }
            if (ev.msg == "一键同意加群")
            {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试(如果已经是管理员 请艾特一次机器人后在试试)";
                auto *c = m_botClients[ev.appid];
                // 获取最多30条加群申请
                QString js = c->getjoin_request_list(ev.groupId, 40);

                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(js.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError) {
                    return "❌ 获取加群列表失败，可能接口超过调用频率";
                }

                QJsonObject root = doc.object();
                QJsonArray list = root["list"].toArray();
                int count = list.size();
                if(count == 0 && js.contains("admin"))
                {
                    return "📋 **加群申请列表**\n非管理员无法获取 或 频率超限";
                }
                if (count == 0) {
                    return "📋 当前没有待处理的加群申请。";
                }

                // 遍历所有申请，逐个同意
                for (const QJsonValue &val : std::as_const(list)) {
                    QJsonObject item = val.toObject();
                    QString joinRequestId = item["join_request_id"].toString();
                    QString memberOpenid = item["member_openid"].toString();
                    QString username = item["username"].toString();
                    c->approveGroupJoinRequest(ev.groupId,memberOpenid,true,joinRequestId,QString(),false,[](auto, auto){ return; });
                }

                // 立即回复用户，表示开始处理
                return QString("✅ 共 %1 条加群申请，已开始逐一同处理，如果没处理代表 接口频率限制 每分钟只处理60个").arg(count);
            }
            if (ev.msg.startsWith("同意加群"))
            {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试(如果已经是管理员 请艾特一次机器人后在试试)";
                QString QID,joinRequestId;
                QString text = ev.msg;
                text.remove("同意加群");
                text = text.trimmed();
                QStringList list = text.split(" ");

                if(list.size()<2) return "参数不满足 指令 同意加群 userid RequestId";
                auto *c = m_botClients[ev.appid];
                 c->approveGroupJoinRequest(ev.groupId,list[0],true,list[1],QString(),false,[c,ev](const QString &resp, auto){
                    if(resp!="{}"){
                        QString text = resp;
                        c->send_messages(ev.type,ev.groupId,"[同意加群]",text,ev.msgId);
                        return ;
                    }
                    QString text = "同意加群成功";
                    c->send_messages(ev.type,ev.groupId,"[同意加群]",text,ev.msgId);
                    return ;
                });
            }
            if (ev.msg == "禁言列表")
            {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试(如果已经是管理员 请艾特一次机器人后在试试)";
                auto *c = m_botClients[ev.appid];
                QString js = c->getGroupRestrictChatSetting(ev.groupId);
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(js.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError) {

                    return "❌ 获取禁言列表失败，可能接口超过调用频率";
                }

                QJsonObject root = doc.object();
                QJsonObject globalRule = root["global_rule"].toObject();
                QString mode = globalRule["mode"].toString();
                const QJsonArray members = root["members"].toArray();


                int memberCount = members.size();

                int estimatedSize = 100 + memberCount * 80;
                QString reply;
                reply.reserve(estimatedSize);

                // 3. 构建回复文本（不用数组容器）
                reply += "📋 **禁言列表**\n";

                if (memberCount == 0) {
                    reply += "当前群组没有禁言成员，也未开启全局禁言。";
                } else if(memberCount == 0 ){
                    if (mode != "none") {
                        reply += "🔒 **全局禁言模式**：`" + mode + "`\n";
                    }
                    if (memberCount > 0) {
                        reply += "📌 **被禁言成员**：";
                        for (const QJsonValue &val : members) {
                            QJsonObject member = val.toObject();
                            QString member_openid = member["member_openid"].toString();
                            QString expireAt = member["mute_expire_at"].toString();
                            reply += "\n>**"+expireAt+"**\n><@"+member_openid+"> | [解禁](解禁 "+member_openid+")";
                        }
                    }
                }

                return reply;
            }
            if (ev.msg.contains("禁言")) {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试(如果已经是管理员 请艾特一次机器人后在试试)";
                auto [QID, sj] = parseBanCommand(ev.msg);

                if (QID.isEmpty()) {
                    return "[禁言] 解析失败，请检查格式（需要艾特一个人）";
                }
                if (sj == -2) {
                    return "[禁言] 未知的时间单位，支持：秒、分钟、月";
                }

                auto *c = m_botClients[ev.appid];
                QString res = c->setGroupRestrictChatSetting(ev.groupId, QID, sj,[c,ev](const QString resp,QNetworkReply::NetworkError err){
                    QString text = (resp == "{}" ? "禁言成功" : "禁言失败，可能无权限.." + resp);
                    c->send_msgAsync(ev.type,ev.groupId,"[禁言某人]",text,ev.msgId);

                });
                return "*"; //异步 这里不返回消息
            }
            if (ev.msg.startsWith("免验证")) {
                QString strid = ev.msg;
                strid.remove("免验证");
                QString qid  = strid.trimmed();
                if (qid.size()!=32) {
                    return "错误的 id 不是 32字节hex";
                }
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                QString text;

                int id= db->getOrUpdateUser(qid,text);
                db->getGroupInfo(ev.groupId, gid);
                for(int i=0;i<gid.jojnyz.size();++i){
                    if(gid.jojnyz[i]==id)
                    {
                        gid.jojnyz.removeAt(i);
                        gid.jojntime.removeAt(i);
                        db->savejojnyzData(ev.groupId,gid.jojnyz,gid.jojntime);
                        auto *c = m_botClients[ev.appid];
                        c->setGroupRestrictChatSetting(ev.groupId,qid,0,[](auto,auto){});

                        return "免验证成功";
                    }
                }
                return "免验证失败 该用户不再验证列表 如果需要解除禁言 发送 [解禁]() + 艾特 指令";
            }
            if (ev.msg.contains("解禁"))
            {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试(如果已经是管理员 请艾特一次机器人后在试试)";
                QString text = ev.msg;
                text.remove("解禁");
                QString QID;
                int cnt = extractParams(text, "", 0, QID);
                if (cnt == -1) return "[解除禁言] 缺少被禁言人";
                auto *c = m_botClients[ev.appid];
                QString res = c->setGroupRestrictChatSetting(ev.groupId,QID,0);
                if(res=="{}")
                {
                    return "解除成功";
                }
                return "解除失败可能无权限.."+res;
            }
            if (ev.msg == "一键解禁")
            {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试(如果已经是管理员 请艾特一次机器人后在试试)";
                auto *c = m_botClients[ev.appid];
                QString immediateReply = "⏳ 正在执行一键解禁，请稍候...";
                c->getGroupRestrictChatSetting(ev.groupId, [=](const QString& jsonStr, QNetworkReply::NetworkError err) {
                    if (err != QNetworkReply::NoError) {
                        QString text = "❌ 获取禁言列表失败（网络错误）";
                        c->send_msgAsync(ev.type, ev.groupId, "[私有指令]", text, ev.msgId);
                        return;
                    }
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
                    if (parseError.error != QJsonParseError::NoError) {
                        QString text = "❌ 解析禁言列表失败";
                        c->send_msgAsync(ev.type, ev.groupId, "[私有指令]", text, ev.msgId);
                        return;
                    }
                    QJsonObject root = doc.object();
                    QJsonArray members = root["members"].toArray();
                    if (members.isEmpty()) {
                        QString text = "✅ 当前没有需要解禁的成员";
                        c->send_msgAsync(ev.type, ev.groupId, "[私有指令]", text, ev.msgId);
                        return;
                    }
                    // 构造批量解禁的 JSON 数组
                    QJsonArray delMembers;
                    for (const QJsonValue &val : std::as_const(members)) {
                        QJsonObject member = val.toObject();
                        QString openid = member["member_openid"].toString();
                        QJsonObject delItem;
                        delItem["member_openid"] = openid;
                        delItem["op"] = "del";
                        delMembers.append(delItem);
                    }
                    // 异步调用批量解禁接口
                    c->setGroupRestrictChatSetting(ev.groupId, delMembers, [=](const QString& resJson, QNetworkReply::NetworkError err2) {
                        QString text;
                        if (err2 != QNetworkReply::NoError) {
                            text = "❌ 一键解禁失败（网络错误）";
                        } else if (resJson == "{}") {
                            text = "✅ 一键解禁成功，已解除 " + QString::number(delMembers.size()) + " 名成员的禁言";

                        } else {
                            text = "❌ 一键解禁失败，可能无权限或接口错误: " + resJson;
                        }
                        c->send_msgAsync(ev.type, ev.groupId, "[私有指令]", text, ev.msgId);
                    });
                });

                // 由于是异步处理，这里返回一个占位消息（或空字符串）
                return immediateReply;   // 或 return "";
            }

            if (ev.msg.startsWith("撤回")) {

                auto [targetId, count] = parseRecallCommand(ev.msg);
                if (count < 0) {
                    return "[撤回] 解析失败，请使用格式：撤回 [<@ID>] [条数]，如“撤回 <@a1b2...> 5”";
                }

                // 限制最大条数
                if (count > 30) count = 30;
                if (count <= 0) count = 1;

                auto *c = m_botClients[ev.appid];
                // 获取最近200条消息（与原逻辑一致）
                QList<Message> msgList = g_logdb[1]->getRecentLogs(
                    QString::number(ev.appid), ev.groupId, 2147483636, 200, true
                    );

                int deleted = 0;      // 已撤回数量
                int failed = 0;       // 失败次数（仅针对指定用户）

                if (targetId.isEmpty()) {
                    // ========== 撤回机器人自己的消息 ==========
                    for (const auto &m : std::as_const(msgList)) {
                        if (m.plugin_ch.isEmpty()) continue;  // 不是机器人发送的消息
                        // 撤回（不检查返回，原逻辑如此）
                        c->delete_messages(0, ev.groupId, m.plugin_ch, [](auto, auto) { return; });
                        deleted++;
                        if (deleted >= count) break;
                    }
                    return QString("[撤回] 已撤回机器人自己的 %1 条消息").arg(deleted);
                } else {
                    if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                    // ========== 撤回指定用户的消息 ==========
                    for (const auto &m : std::as_const(msgList)) {
                        if (m.user != targetId) continue;

                        // 前3次尝试检查权限，后续直接尝试（原逻辑）
                        if (deleted < 3) {
                            QString res = c->delete_messages(0, ev.groupId, m.ch);
                            if (res.contains("权限")) {
                                return "[撤回] 无撤回权限，请确认是管理员或机器人有管理员权限";
                            }
                            if (res != "{}") {
                                failed++;
                                if (failed >= 3) {
                                    return "[撤回] 撤回完成，但失败次数超过3次，自动终止";
                                }
                            }
                        } else {
                            // 后续消息忽略结果
                            c->delete_messages(0, ev.groupId, m.ch, [](auto, auto) { return; });
                        }

                        deleted++;
                        if (deleted >= count) {
                            return QString("[撤回] 已撤回 %1 条 %2 的消息").arg(deleted).arg(targetId);
                        }
                    }
                    return QString("[撤回] 撤回完成，但可能未达到指定数量（已撤回 %1 条）").arg(deleted);
                }
            }
            if (ev.msg == "设置刷屏检测") {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                if (ev.bitmap & BIT_SHUA_P)  return "当前已经开启 刷屏检测";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap |= BIT_SHUA_P;
                db->addGroup(ev.groupId, gid);
                return "设置刷屏检测成功";
            }
            if (ev.msg == "取消刷屏检测") {

                if (!(ev.bitmap & BIT_SHUA_P))  return "当前已经关闭 刷屏检测";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap &= ~BIT_SHUA_P;
                db->addGroup(ev.groupId, gid);
                return "关闭刷屏检测成功";
            }
            if (ev.msg == "开申请加群提示") {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                if (ev.bitmap & BIT_AUTO_JOJN_KG)  return "当前已经开启 申请加群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap |= BIT_AUTO_JOJN_KG;
                db->addGroup(ev.groupId, gid);
                return "设置申请加群提示成功";
            }
            if (ev.msg == "关申请加群提示") {
                if (!(ev.bitmap & BIT_AUTO_JOJN_KG))  return "当前已经关闭 申请加群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap &= ~BIT_AUTO_JOJN_KG;
                db->addGroup(ev.groupId, gid);
                return "关闭申请加群提示成功";
            }
            if (ev.msg == "开违禁词检测") {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                if (ev.bitmap & BIT_PUNISH)  return "当前已经开启 申请加群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap |= BIT_PUNISH;
                db->addGroup(ev.groupId, gid);
                return "设置违禁词检测成功";
            }
            if (ev.msg == "关违禁词检测") {
                if (!(ev.bitmap & BIT_PUNISH))  return "当前已经关闭 申请加群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap &= ~BIT_PUNISH;
                db->addGroup(ev.groupId, gid);
                return "关闭违禁词检测成功";
            }
            if (ev.msg == "开自动同意加群") {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                if (ev.bitmap & BIT_AUTO_JOJN)  return "当前已经开启 自动同意加群";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap |= BIT_AUTO_JOJN;
                db->addGroup(ev.groupId, gid);
                return "设置自动同意加群成功 请使用 [设置自动同意加群答案]() <关键词> 来设置自动同意关键词\n"
                "如果没设置就全部同意..当前设置的关键词长度(由于并不能查看关键词 请重新设置覆盖).."+QString::number(gid.autoref.size());
            }
            if (ev.msg == "关自动同意加群") {
                if (!(ev.bitmap & BIT_AUTO_JOJN))  return "当前已经关闭 自动同意加群";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap &= ~BIT_AUTO_JOJN;
                db->addGroup(ev.groupId, gid);
                return "关闭自动同意加群成功";
            }
            if (ev.msg == "开入群验证") {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                if (ev.bitmap & BIT_RUQUN_YZ)  return "当前已经开启 入群验证";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap |= BIT_RUQUN_YZ;
                db->addGroup(ev.groupId, gid);
                return "开入群验证成功，开启后 有人入群将会禁言一个月 直到 单击验证按钮 \n";
            }
            if (ev.msg == "关入群验证") {
                if (!(ev.bitmap & BIT_RUQUN_YZ))  return "当前已经关闭 入群验证";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap &= ~BIT_RUQUN_YZ;
                db->addGroup(ev.groupId, gid);
                return "关入群验证成功成功";
            }


            if (ev.msg.startsWith("设置自动同意加群答案")) {
                QString text;
                int cnt = extractParams(ev.msg, "设置自动同意加群答案", 0, text);
                if (cnt == -1) return "[设置自动同意加群答案] 缺少关键词";

                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                QByteArray utf8 = text.toUtf8();
                if(utf8.size()>=128) return QString("设置的关键词长度不能超128字节 当前%1字节").arg(utf8.size());
                gid.autoref = text;
                gid.autoref[utf8.size()] = '\0';              // 手动添加终止符
                db->addGroup(ev.groupId, gid);
                return "设置自动同意加群答案 完成 这里是不显示答案的 最长128直接 相当于42个中文 使用（,） 英文逗号分割多个关键词 可以不设置关键词 会全部通过 然后 没处理可能是 接口跳用超频率";
            }
            if (ev.msg == "设置入群提示") {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                if (!(ev.bitmap & BIT_ruqun))  return "当前已经设置 入群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap &= ~BIT_ruqun;
                db->addGroup(ev.groupId, gid);
                return "设置入群提示成功";
            } else if (ev.msg == "取消入群提示") {
                if (ev.bitmap & BIT_ruqun)  return "当前没有设置 入群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap |= BIT_ruqun;
                db->addGroup(ev.groupId, gid);
                return "取消入群提示成功 如需打开 请发送 [设置入群提示]()";
            }

            if (ev.msg == "设置退群提示") {
                if(!ev.bot_admin) return "机器人需要是管理员才能执行当前命令呢 请将机器人添加到管理员列表后再试试";
                if (!(ev.bitmap & BIT_tuiqun))  return "当前已经设置 退群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap &= ~BIT_tuiqun;
                db->addGroup(ev.groupId, gid);
                return "设置退群提示成功";
            } else if (ev.msg == "取消退群提示") {
                if (ev.bitmap & BIT_tuiqun)  return "当前没有设置 退群提示";
                auto *db = g_botdb[info->appid_int];
                GroupRecord2 gid;
                db->getGroupInfo(ev.groupId, gid);
                gid.bitmap |= BIT_tuiqun;
                db->addGroup(ev.groupId, gid);
                return "取消退群提示成功 如需打开 请发送 [设置退群提示]()";
            }
            // 其他管理命令不处理，继续往下
        }else{
            if(keyword_Punish->match(ev)) return "!!!!";
        }
    }
    // ---------- 入群事件（缓存起来，等待主线程合并发送） ----------
    if (ev.subType == 2) {
        if (info->rqhy.isEmpty()) return QString(); // 无回复内容则忽略
        auto *db = g_botdb[info->appid_int];
        if (ev.bitmap & BIT_ruqun) return QString(); // 已关闭入群提示
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (info->fasjg > 0) {
            GroupRecord2 gid;
            db->getGroupInfo(ev.groupId, gid);
            qint64 lastSend = gid.xychy_time;
            if (now - lastSend < info->fasjg) {
                return QString();
            }
            gid.xychy_time = now;
            db->addGroup(ev.groupId, gid);
        }
        if(info->rq_ychf==0)
        {
            QString sentText;
            if(info->rqhy.contains("#python"))
                return   python_code4(info->rqhy,ev.appid,QStringList() << ev.user,QStringList() << ev.nickname);

            PendingGroupEvent ext;
            ext.gname = ev.groupname;
            ext.user.append(ev.user);
            ext.username.append( ev.nickname);
            return  sendapp(info->appid_int,info->tqhy,ext);

        }
        QMutexLocker locker(&info->pendingMutex);
        auto &entry = info->pendingJoin[ev.groupId];
        if (entry.user.isEmpty()) {

            entry.startTime = QDateTime::currentSecsSinceEpoch();
            entry.gname = ev.groupname;
        }

        entry.user.append(ev.user);
        entry.username.append(ev.nickname);
        entry.msgid = ev.msgId;
        return QString(); // 不立即回复
    }
    if (ev.subType == 3) {
        if (!chatPage->全量群.contains(ev.groupId)) return QString();
        if (info->tqhy.isEmpty()) return QString();

        if (ev.bitmap & BIT_tuiqun) return QString(); // 已关闭退群提示
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (info->tq_lq > 0) {
            auto *db = g_botdb[info->appid_int];
            GroupRecord2 gid;
            db->getGroupInfo(ev.groupId, gid);
            qint64 lastSend = gid.tq_CD;
            if (now - lastSend < info->tq_lq) {
                return QString();
            }
            gid.tq_CD = now;
            db->addGroup(ev.groupId, gid);
        }
        if(info->tq_ychf==0)
        {
            PendingGroupEvent ext;
            ext.gname = ev.groupname;
            ext.user.append(ev.user);
            ext.username.append( ev.nickname);
            return  sendapp(info->appid_int,info->tqhy,ext);
        }
        QMutexLocker locker(&info->pendingMutex2);
        auto &entry = info->pendingLeave[ev.groupId];
        if (entry.user.isEmpty()) {

            entry.startTime = QDateTime::currentSecsSinceEpoch();
            entry.gname = ev.groupname;

        }
        entry.msgid=ev.msgId;
        entry.user.append(ev.user);
        entry.username.append(ev.nickname);
        return QString();
    }
    return QString();
}



void processPendingEvents()
{
    qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto &acc : m_accounts){
        {
            QMutexLocker locker(&acc->pendingMutex);
            for (auto it = acc->pendingJoin.begin(); it != acc->pendingJoin.end(); ) {
                QString groupId = it.key();
                PendingGroupEvent &evt = it.value();
                if (now - evt.startTime < acc->rq_ychf) {
                    ++it;
                    continue;
                }
                if(!m_botClients.contains(acc->appid_int)) return ;
                QString sentText= sendapp(acc->appid_int,acc->rqhy,evt);
                QQBotClient* client = m_botClients[acc->appid_int];
                client->send_msgAsync(0,groupId,"[入群提示]", sentText,QString());
                it = acc->pendingJoin.erase(it);
            }
        }
        QMutexLocker locker(&acc->pendingMutex2);
        for (auto it =acc->pendingLeave.begin(); it != acc->pendingLeave.end(); ) {
            QString groupId = it.key();
            PendingGroupEvent &evt = it.value();
            if (now - evt.startTime < acc->tq_ychf) {
                ++it;
                continue;
            }

            if(!m_botClients.contains(acc->appid_int)) return ;
            QString sentText= sendapp(acc->appid_int,acc->tqhy,evt);

            QQBotClient* client = m_botClients[acc->appid_int];
            client->send_msgAsync(0,groupId,"[退群提示]", sentText,QString());
            it = acc->pendingLeave.erase(it);
        }
    }
}

QString extractBetween(const QString &source, const QString &left, const QString &right) {
    int start = source.indexOf(left);
    if (start == -1) return QString();
    start += left.length();
    int end = source.indexOf(right, start);
    if (end == -1) return QString();
    return source.mid(start, end - start);
}


QString replaceBetweenAll(const QString &original,const QString &left,const QString &right,
                          const QString &replacement,int maxReplacements)
{
    if (left.isEmpty() || right.isEmpty()) return original;
    QString result = original;
    int count = 0;
    int startPos = 0;
    while (true) {
        int posLeft = result.indexOf(left, startPos);
        if (posLeft == -1) break;
        int posRight = result.indexOf(right, posLeft + left.length());
        if (posRight == -1) break;

        result = result.left(posLeft) + replacement + result.mid(posRight + right.length());
        count++;
        if (maxReplacements != -1 && count >= maxReplacements) break;

        startPos = posLeft + replacement.length();
    }
    return result;
}

#include <QRegularExpression>
#include <QString>

// 将字节数转换为可读字符串，如 1024 -> "1KB", 1536000 -> "1.5MB"
QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString("%1B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QString("%1KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024)
        return QString("%1MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

/**
 * @brief 将文本中所有 [file,name=xxx,size=xxx,url=xxx] 格式的标签替换为 "[文件]文件名(大小)"
 * @param content 原始文本
 * @param format  替换格式，默认为 "[文件]%1(%2)"，其中 %1=文件名, %2=格式化后的大小
 * @return 替换后的文本
 */
QString replaceFileTag(const QString &content, const QString &format)
{
    // 正则匹配: [file, name=值, size=数值, url=值]  属性顺序可能变化，这里兼容 name 和 size 出现任意顺序
    // 使用两个捕获组分别捕获 name 和 size，不依赖顺序
    static QRegularExpression re("\\[file,\\s*(?:name=([^,\\]]+)[^\\]]*?,\\s*size=(\\d+)|size=(\\d+)[^\\]]*?,\\s*name=([^,\\]]+))[^\\]]*\\]");
    QString result = content;
    int offset = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(content);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString nameValue;
        QString sizeValueStr;
        // 尝试两种顺序捕获
        if (!match.captured(1).isEmpty()) {
            nameValue = match.captured(1).trimmed();
            sizeValueStr = match.captured(2);
        } else {
            nameValue = match.captured(4).trimmed();
            sizeValueStr = match.captured(3);
        }
        qint64 sizeBytes = sizeValueStr.toLongLong();
        QString sizeReadable = formatFileSize(sizeBytes);
        QString replacement = format.arg(nameValue, sizeReadable);

        return replacement;
    }
    return "[文件]";
}

QString uploadToMhimg(const QByteArray &imageData, const QString &originalFileName, QString *errorMsg)
{
    if (imageData.isEmpty()) {
        if (errorMsg) *errorMsg = "Image data is empty";
        return QString();
    }

    QByteArray hash = QCryptographicHash::hash(imageData, QCryptographicHash::Md5);
    QString hashHex = hash.toHex();

    // 2. 确定文件扩展名
    QString extension = ".jpg";
    if (!originalFileName.isEmpty()) {
        int dot = originalFileName.lastIndexOf('.');
        if (dot != -1)
            extension = originalFileName.mid(dot);
    }
    QString fileName = hashHex + extension;
    QString boundary = "----WebKitFormBoundary" +
                       QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);

    QByteArray body;
    body.append("--" + boundary.toUtf8() + "\r\n");
    body.append("Content-Disposition: form-data; name=\"Filedata\"; filename=\"" + fileName.toUtf8() + "\"\r\n");
    body.append("Content-Type: image/jpeg\r\n\r\n");
    body.append(imageData);
    body.append("\r\n");
    body.append("--" + boundary.toUtf8() + "--\r\n");


    QString contentType = "multipart/form-data; boundary=" + boundary;

    QHash<QString,QString> h;
    h["User-Agent"]= "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";
    h["Accept"]= "*/*";
    h["Origin"]= "https://cli.im";
    h["Referer"]= "https://cli.im/deqr/";
    h["Sec-Fetch-Site"]= "same-site";
    h["Sec-Fetch-Mode"]= "cors";
    h["Content-Type"]= contentType;

    auto future = NetManager::instance()->post("https://upload.api.cli.im/upload.php?kid=cliim",body,h,30000);

    QString responseBody = future.get();

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(responseBody.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMsg) *errorMsg = "Invalid JSON response: " + parseErr.errorString();
        return QString();
    }
    QJsonObject obj = doc.object();
    QString status = obj.value("status").toString();
    if (status != "1") {
        QString msg = obj.value("msg").toString();
        if (errorMsg) *errorMsg = QString("Upload failed, status=%1, msg=%2").arg(status, msg);
        return QString();
    }
    QJsonObject dataObj = obj.value("data").toObject();
    QString url = dataObj.value("path").toString();
    if (url.isEmpty()) {
        if (errorMsg) *errorMsg = "Response missing data.path";
        return QString();
    }
    return url;
}

QString uploadToMhimg(const QString &filePath, QString *errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QString("Cannot open file: %1").arg(file.errorString());
        return QString();
    }
    QByteArray data = file.readAll();
    file.close();
    return uploadToMhimg(data, filePath, errorMsg);
}
qint64 mergeToId(int appid, int type) {
    return (static_cast<qint64>(appid) << 32) | (static_cast<quint32>(type));
}
void parseFromId(qint64 id, int &appid, int &type) {
    appid = static_cast<int>(id >> 32);
    type = static_cast<int>(id & 0xFFFFFFFF);
}

QString upload(const QString &path);
QString uploadImageSync(const QString& serverUrl, const QString& token, const QString& filePath,int timeoutMs = 30000, QString* errorMsg = nullptr);
QString uploadImageByPath(const QString &serverUrl,const QString &localPath, int timeoutMs,QString *errorMsg);


QString joinIntListFast(const QList<int>& list, const QString& sep) {
    if (list.isEmpty()) return {};

    // 1. 计算总长度
    int totalLen = 0;
    int sepLen = sep.length();
    for (int v : list) {
        totalLen += QString::number(v).length();  // 当前数字的字符长度
        totalLen += sepLen;                       // 分隔符长度（每个数字后都加，最后再减）
    }
    totalLen -= sepLen;  // 最后一个数字后面不加分隔符

    // 2. 预分配内存
    QString result;
    result.reserve(totalLen);

    // 3. 拼接
    for (int i = 0; i < list.size(); ++i) {
        if (i != 0) result += sep;
        result += QString::number(list.at(i));
    }
    return result;
}


void doWork(int totalDelay) {
    if(totalDelay==0) return;
    const int step = 100;        // 每 100ms 检查一次

    QElapsedTimer timer;
    timer.start(); // 开始计时

    while (timer.elapsed() < totalDelay) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            return;
        }
        int remaining = totalDelay - timer.elapsed();
        if (remaining > step) {
            QThread::msleep(step);
        } else {
            QThread::msleep(remaining);
        }
    }
}


QStringList takeAllTextMiddle(const QString &original, const QString &leftMarker,const QString &rightMarker,bool includeSides)
{
    QStringList results;
    int searchStart = 0;
    int leftLen = leftMarker.length();
    int rightLen = rightMarker.length();

    while (true) {
        int leftPos = original.indexOf(leftMarker, searchStart);
        if (leftPos == -1)
            break;   // 没有更多左标记

        int rightPos = original.indexOf(rightMarker, leftPos + leftLen);
        if (rightPos == -1)
            break;   // 左标记之后没有右标记，后续也不会有，因为右标记必须出现在左标记之后

        // 提取匹配部分
        if (includeSides) {
            // 包含左右标记
            results << original.mid(leftPos, rightPos - leftPos + rightLen);
        } else {
            // 只取中间
            results << original.mid(leftPos + leftLen,
                                    rightPos - leftPos - leftLen);
        }

        // 继续从右标记之后查找下一个
        searchStart = rightPos + rightLen;
    }

    return results;
}
QString subTextReplace(const QString &source,const QString &find,const QString &replace,
                       int replaceCount,int startPos)
{
    if (find.isEmpty())
        return source;          // 空子串无法替换，直接返回


    int idx = startPos - 1;
    if (idx < 0)
        idx = 0;
    if (idx > source.length())
        return source;          // 起始位置超出长度，无替换

    // 如果替换次数为0，不替换
    if (replaceCount == 0)
        return source;

    // 结果字符串（可变拷贝）
    QString result = source;
    int replaced = 0;
    int offset = 0;             // 因为每次替换会改变字符串长度，用于修正查找位置

    while (true) {
        // 从当前 offset 开始查找 find 在 result 中的位置
        int pos = result.indexOf(find, offset);

        if (pos == -1)
            break;              // 找不到更多
        result.replace(pos, find.length(), replace);
        replaced++;

        if (replaceCount != -1 && replaced >= replaceCount)
            break;
        offset = pos + replace.length();
    }

    return result;
}
bool downloadFile(const QString &url, const QString &savePath, QString &errorMsg) {

    auto f = NetManager::instance()->get(url,QHash<QString,QString>(),30000);
    QByteArray data = f.get();
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorMsg = "无法创建文件: " + savePath;
        return false;
    }
    file.write(data);
    file.close();
    return true;
}


QByteArray R_file(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {}; // 或记录 errorString()
    }
    return file.readAll();
}

bool W_file(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    qint64 written = file.write(data);
    file.close();
    return written == data.size();
}
