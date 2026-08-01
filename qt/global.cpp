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
bool 框架退出=false;
int miaomiao32=0;
int miaomiao=0;
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
            // 以下代码在主线程运行

            if (m_openidTimers.contains(openid)) {
                QTimer *oldTimer = m_openidTimers[openid];
                oldTimer->start();  // 重新计时 5 秒
                qDebug() << "重置定时" << openid;
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
                c->send_messagesAsync(type, openid, text, c->m_info->fallbackReply, msgid, false, false, 0, true);

                timer->deleteLater();  // 任务完成，清理定时器
            });

            // 4. 保存定时器到 map，并启动
            m_openidTimers.insert(openid, timer);
            timer->start();

            qDebug() << "添加定时（或重置）" << openid;
        }, Qt::QueuedConnection);
    }

}
void cancelTimer(const QString &openid)
{
    QMetaObject::invokeMethod(qApp, [=]() {
        if (!m_openidTimers.contains(openid)) return;

        QTimer *timer = m_openidTimers.take(openid);  // 从 map 取出
        timer->stop();
        timer->deleteLater();  // 安全销毁
        qDebug() << "取消定时" << openid;
    }, Qt::QueuedConnection);
}

void AppendEventLog(const QString &msg,int color)
{

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
                auto *db = g_botdb[info->appid_int];
                GroupRecord rec;
                db->getGroupInfo(ev.groupId,rec);
                if (rec.bitmap & 4) return "本群已在白名单";
                rec.bitmap |= 4;
                db->addGroup(ev.groupId,rec);
                return "添加本群 ai白名单成功";
            }else if(ev.type==2)
            {
                auto *db = g_botdb[info->appid_int];
                UserRecord rec;
                db->getUserBySeqId(ev.user_int,rec);
                if (rec.bitmap & 4) return "你已经已在白名单";
                rec.bitmap |= 4;
                db->updateUserBySeqId(ev.user_int,rec);
                return "添加 ai白名单成功";
            }
            return "目前只支持 群 和 私聊设置白名单";
        }
        //提供了参数
        if(p1.size()==32)
        {
            auto *db = g_botdb[info->appid_int];
            GroupRecord rec;
            if(db->getGroupInfo(p1,rec))
            {
                if(p1!=ev.groupId) return "传递参数1 群id 在数据库 未记录 请检查是否是真实群id 或者将机器人移出 对应群再次邀请";
                rec.create_time= QDateTime::currentSecsSinceEpoch()/60;
                rec.inviter_seq_id=ev.user_int;
            }
            if (rec.bitmap & 4) return "本群已在白名单";
            rec.bitmap |= 4;
            db->addGroup(ev.groupId,rec);
            return "添加本群 ai白名单成功";
        }
        int user_id = p1.toInt();
        if(user_id <2147483636 && user_id>0)
        {
            auto *db = g_botdb[info->appid_int];
            UserRecord rec;
            db->getUserBySeqId(ev.user_int,rec);
            if (rec.bitmap & 4) return "该用户已经在白名单 或 已经开启";
            rec.bitmap |= 4;
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
                auto *db = g_botdb[info->appid_int];
                GroupRecord rec;
                db->getGroupInfo(ev.groupId, rec);
                if (!(rec.bitmap & 4)) return "本群不在白名单";
                rec.bitmap &= ~4;
                db->addGroup(ev.groupId, rec);
                return "删除本群 AI 白名单成功";
            } else if (ev.type == 2) {
                auto *db = g_botdb[info->appid_int];
                UserRecord rec;
                db->getUserBySeqId(ev.user_int, rec);
                if (!(rec.bitmap & 4)) return "该用户不在白名单";
                rec.bitmap &= ~4;
                db->updateUserBySeqId(ev.user_int, rec);
                return "删除当前用户白名单成功";
            }
            return "目前只支持群和私聊设置白名单";
        }

        if (p1.size() == 32) {
            // 参数为群ID（长ID）
            auto *db = g_botdb[info->appid_int];
            GroupRecord rec;
            if (db->getGroupInfo(p1, rec)) {
                if (p1 != ev.groupId) return "只能操作当前群";
                if (!(rec.bitmap & 4)) return "该群不在白名单";
                rec.bitmap &= ~4;
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
            if (!(rec.bitmap & 4)) return "该用户不在白名单";
            rec.bitmap &= ~4;
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

QString python_code(const QString &py_code,const MessageEvent &msg);
QString ruqunhy(AccountInfo *info,const MessageEvent &ev);
QString 内置指令(MessageEvent &ev)
{
    QString text;
    if(ev.msg=="我的id" || ev.msg=="我的ID")
    {
        text = QString("**我的ID**\n>user_id:%1\n个人ID:%2\n群ID：%3").arg(ev.user_int).arg(ev.user, ev.groupId);
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
            homePage->refreshRuntimeStats();
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
        QMetaObject::invokeMethod(qApp, [=]() {
            homePage->refreshRuntimeStats();
        });
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
            if (bridge) {
                bridge->writeResponseToBlock(1, "{\"type\":6}");
                bridge->stopServer();   // 同步停止
            }
            for(const auto &db : std::as_const(g_logdb))
            {
                db->close();
            }
            pluginPage->foruninstall_Plugin();
            QThreadPool::globalInstance()->waitForDone();
            QString program = QCoreApplication::applicationFilePath();
            QStringList args = QCoreApplication::arguments();
            QProcess::startDetached(program, args);
            ::TerminateProcess(GetCurrentProcess(), 0);
        }, Qt::QueuedConnection);



        return "正在重启";
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
                log = QString(">![#24px #24px](https://q.qlogo.cn/qqapp/%1/%2/0) %3(%4) %5\n").arg(bot->appid,bot->unid,bot->nickname,bot->appid,status.arg(bot->appid));
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
    bool upad = false;
    if(!g_admin.isEmpty()  ){
        upad = g_admin.contains(ev.user);
    }
    if(!upad){
        if(info->admin.isEmpty()) return QString();
        if(!info->admin.contains(ev.user)) return QString();
    }
    if(ev.msg=="#纯白铃铛")
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
            QString("**基础**\n"
            ">[取]() 获取某条信息原始数据\n"
            ">[md]() 复读指令\n"
            ">[重启框架]() 字面意思\n"
            ">[接口测试]() 字面意思\n\n"

            "**插件相关**\n"
            ">JS:%1 | Python:%4\nDLL:%2 | DLL32:%3\n>"
            "[#插件列表]()\n>[#插件启用]() | [#插件禁用]()\n\n"
            "**其他**\n"
            ">[开启拟人]() | [关闭拟人]()\n"
            ">[%5]() 启用或取消白名单系统\n"
            ">[%6]() {群id|好友id} 设置白名单\n"
            ">[%7]() {群id|好友id} 取消白名单\n\n"
            "**机器人设置**\n"
            ">[login]() <appid> 登录某个机器人\n"
            ">[logout]() <appid> 下线某个bot\n"
            ">[delbot]() <appid> 删除某个bit\n"
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
            ">[#重启框架]()\n"

                       ">'<>'为必填 '{}'可选\n>以上指令需要艾特 机器人才能触发")
                        .arg(js).arg(dll).arg(dll32).arg(python)

                        .arg(info->bai_qy.isEmpty() ? "未设置" : info->bai_qy,
                 info->bai_sr.isEmpty() ? "未设置" : info->bai_sr,
                 info->bai_sc.isEmpty() ? "未设置" : info->bai_sc);
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
        res.append("可用指令：\n[#插件列表]()\n>[#插件启用]() <序号>\n>[#插件禁用]()\n\n**需超管权限**\n>[#重载插件]() <序号>\n>[#启用插件]() <序号>\n>[#禁用插件]() <序号>\n>[#卸载插件]() <序号>\n>[#加载插件]() <路径>\n[#插件市场]()\n[#扫描插件]() 查看现有插件");
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
    if(ev.msg=="接口测试")
    {
        QString resu;

        if(m_botClients.contains(info->appid_int))
        {
            resu.reserve(1024);
            QQBotClient *client = m_botClients[info->appid_int];
            resu.append("**/get_groups_info**\n>");
            resu.append( client->get_groups_info(ev.groupId));

            resu.append("\n\n**/get_groups_bot_state**\n>");
            resu.append( client->get_groups_bot_state(ev.groupId));

            resu.append("\n\n**/set_mute**\n>");
            resu.append(client->set_mute(0,ev.groupId,ev.user,60));

            resu.append("\n\n**/get_members_list**\n>");
            resu.append(client->get_members_list(ev.groupId,5));

            resu.append("\n\n**/del_members**\n>");
            resu.append(client->del_members(ev.type,ev.groupId,info->unid));
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

        auto *db = g_botdb[ev.appid];
        GroupRecord gr;
        db->getGroupInfo(ev.groupId, gr);
        bool isEnabled = (gr.bitmap & 1) == 1;  // 当前拟人状态

        if (ev.msg == "开启拟人") {
            if (isEnabled) {
                return "本群已开启拟人，无需重复开启";
            }
            gr.bitmap |= 1;   // 置位
            db->addGroup(ev.groupId, gr.inviter_seq_id, gr.inviter_seq_id, gr.bitmap);
            return "拟人已开启";
        } else { // 关闭拟人
            if (!isEnabled) {
                return "本群未开启拟人，无需关闭";
            }
            gr.bitmap &= ~1;  // 清除位
            db->addGroup(ev.groupId, gr.inviter_seq_id, gr.inviter_seq_id, gr.bitmap);
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
            if(info->welcomeMsg.startsWith("#python")) ret = python_code(ret,ev);
            if(m_botClients.contains(info->appid_int))
            {
                QQBotClient *client = m_botClients[info->appid_int];
                QString text = "[欢迎语]";
                client->send_messages(ev.type,ev.groupId,text,ret,ev.msgId);
            }
        }


    }
    QString text;
    QString ret= 内置指令(ev);
    if(ret.isEmpty()) ret = admin_zl(info,ev);
    if(!ret.isEmpty())
    {
        QQBotClient *client = m_botClients[info->appid_int];
        if(text.isEmpty()) text = "[私有指令|%1ms]";
        client->send_messages(ev.type,ev.groupId,text,ret,ev.msgId);
        return;
    }


    if(m_blacklist.contains(ev.groupId) || m_blacklist.contains(ev.user)) { //黑名单
        return;
    }
    ret =ruqunhy(info,ev);
    if(!ret.isEmpty())
    {
        QQBotClient *client = m_botClients[info->appid_int];
        if(text.isEmpty()) text = "[入群提示|%1ms]";
        client->send_messages(ev.type,ev.groupId,text,ret,ev.msgId);
        return;
    }
    ret = ai_ui->Ai_qx(info,ev);
    if(ret.isEmpty() )
        ret = ai_ui->Ai_post(info,ev);
    else if(ret=="*") ret =QString();

    if(!ret.isEmpty())
    {
        QQBotClient *client = m_botClients[info->appid_int];
        if(text.isEmpty()) text = "[Ai|%1ms]";
        client->send_messages(ev.type,ev.groupId,text,ret,ev.msgId);
        return;
    }
    ret = keyword->match(info->appid_int,ev.msg);
    if(ret.isEmpty()) ret = schedule->ppzl(ev,text,info);

    if(!ret.isEmpty())
    {
        if(ret == "*") return;
        if(m_botClients.contains(info->appid_int))
        {
            if(ret.startsWith("#python")) ret = python_code(ret,ev);
            if(!ret.isEmpty())
            {
                QQBotClient *client = m_botClients[info->appid_int];
                if(text.isEmpty()) text = "[关键词匹配|%1ms]";
                client->send_messages(ev.type,ev.groupId,text,ret,ev.msgId);
                return;
            }
        }
    }

    pluginPage->dispatch_message(ev.raw,ev);

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

QString ruqunhy(AccountInfo *info, const MessageEvent &ev)
{
    if (ev.type != 0) return QString(); // 仅群聊

    // ---------- 管理员命令（立即处理，与原逻辑相同） ----------
    if (ev.member_role < 2) {
        auto *db = g_botdb[info->appid_int];
        GroupRecord gid;
        db->getGroupInfo(ev.groupId, gid);

        if (ev.msg == "设置入群提示") {
            if (!(gid.bitmap & 2))  return "当前已经设置 入群提示";
            gid.bitmap &= ~2;
            db->addGroup(ev.groupId, gid);
            return "设置入群提示成功";
        } else if (ev.msg == "取消入群提示") {
            if (gid.bitmap & 2)  return "当前没有设置 入群提示";
            gid.bitmap |= 2;
            db->addGroup(ev.groupId, gid);
            return "取消入群提示成功 如需打开 请发送 [设置入群提示]()";
        }

        if (ev.msg == "设置退群提示") {
            if (!(gid.bitmap & 4))  return "当前已经设置 退群提示";
            gid.bitmap &= ~4;
            db->addGroup(ev.groupId, gid);
            return "设置退群提示成功";
        } else if (ev.msg == "取消退群提示") {
            if (gid.bitmap & 4)  return "当前没有设置 退群提示";
            gid.bitmap |= 4;
            db->addGroup(ev.groupId, gid);
            return "取消退群提示成功 如需打开 请发送 [设置退群提示]()";
        }
        // 其他管理命令不处理，继续往下
    }

    // ---------- 入群事件（缓存起来，等待主线程合并发送） ----------
    if (ev.subType == 2) {
        if (info->rqhy.isEmpty()) return QString(); // 无回复内容则忽略

        auto *db = g_botdb[info->appid_int];
        GroupRecord gid;
        db->getGroupInfo(ev.groupId, gid);
        if (gid.bitmap & 2) return QString(); // 已关闭入群提示
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (info->fasjg > 0) {
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
                sentText = python_code4(info->rqhy,QStringList() << ev.user);
            else {
                sentText =info->rqhy;
                if(sentText.contains("{艾特}"))
                {
                    sentText.replace("{艾特}","<@"+ev.user+">");
                }
                sentText.replace("{ID}",ev.user);
                sentText.replace("{数量}","1");
                if(sentText.contains("{混合}"))
                {
                    QString hh=QString("![#24px #24px](https://q.qlogo.cn/qqapp/%1/%2/0) <@%3>\n").arg(ev.appid).arg(ev.user, ev.user);
                    sentText.replace("{混合}",hh);
                }

            }

            return sentText;
        }
        QMutexLocker locker(&info->pendingMutex);
        auto &entry = info->pendingJoin[ev.groupId];
        if (entry.user.isEmpty()) {

            entry.startTime = QDateTime::currentSecsSinceEpoch();
        }
        entry.user.append(ev.user);
        entry.msgid=ev.msgId;
        return QString(); // 不立即回复
    }
    if (ev.subType == 3) {
        if (!chatPage->customGroupNames.contains(ev.groupId)) return QString();
        if (info->tqhy.isEmpty()) return QString();
        auto *db = g_botdb[info->appid_int];
        GroupRecord gid;
        db->getGroupInfo(ev.groupId, gid);
        if (gid.bitmap & 4) return QString(); // 已关闭退群提示
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (info->tq_lq > 0) {
            qint64 lastSend = gid.tq_CD;
            if (now - lastSend < info->tq_lq) {
                return QString();
            }
            gid.tq_CD = now;
            db->addGroup(ev.groupId, gid);
        }
        if(info->tq_ychf==0)
        {
            auto *task = new SendMessageTask2(info->appid_int, 0, ev.groupId,info->tqhy,"","","[退群提示]", QStringList() <<ev.user);
            QThreadPool::globalInstance()->start(task);
            return QString();
        }
        QMutexLocker locker(&info->pendingMutex2);
        auto &entry = info->pendingLeave[ev.groupId];
        if (entry.user.isEmpty()) {

            entry.startTime = QDateTime::currentSecsSinceEpoch();
        }
        entry.msgid=ev.msgId;
        entry.user.append(ev.user);
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
                auto *task = new SendMessageTask2(acc->appid_int, 0, groupId,acc->rqhy,evt.msgid,"","[进群提示]", evt.user);
                QThreadPool::globalInstance()->start(task);
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
            auto *task = new SendMessageTask2(acc->appid_int, 0, groupId, acc->tqhy,"","","[退群提示]", evt.user);
            QThreadPool::globalInstance()->start(task);
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
    QRegularExpression re("\\[file,\\s*(?:name=([^,\\]]+)[^\\]]*?,\\s*size=(\\d+)|size=(\\d+)[^\\]]*?,\\s*name=([^,\\]]+))[^\\]]*\\]");
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

    // 1. 计算文件内容的 MD5 作为文件名
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

    // 3. 生成随机边界字符串
    QString boundary = "----WebKitFormBoundary" +
                       QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);

    // 4. 构造 multipart/form-data 请求体
    QByteArray body;
    body.append("--" + boundary.toUtf8() + "\r\n");
    body.append("Content-Disposition: form-data; name=\"Filedata\"; filename=\"" + fileName.toUtf8() + "\"\r\n");
    body.append("Content-Type: image/jpeg\r\n\r\n");
    body.append(imageData);
    body.append("\r\n");
    body.append("--" + boundary.toUtf8() + "--\r\n");

    // 5. 设置请求头
    QString contentType = "multipart/form-data; boundary=" + boundary;
    QNetworkRequest request;
    request.setUrl(QUrl("https://upload.api.cli.im/upload.php?kid=cliim"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Origin", "https://cli.im");
    request.setRawHeader("Referer", "https://cli.im/deqr/");
    request.setRawHeader("Sec-Fetch-Site", "same-site");
    request.setRawHeader("Sec-Fetch-Mode", "cors");

    // 6. 发送请求（同步阻塞）
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, body);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(30000);  // 30秒超时
    loop.exec();

    // 7. 处理响应
    bool ok = false;
    int statusCode = 0;
    QByteArray responseBody;
    QString reqErrorString;

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        ok = true;
        statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        responseBody = reply->readAll();
    } else {
        if (!timer.isActive()) {
            reqErrorString = "Request timeout";
        } else {
            reqErrorString = reply->errorString();
        }
        statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        responseBody = reply->readAll();  // 可能包含部分响应
    }
    reply->deleteLater();

    if (!ok || statusCode != 200) {
        if (errorMsg) {
            if (!responseBody.isEmpty())
                *errorMsg = QString("HTTP %1: %2").arg(statusCode).arg(QString::fromUtf8(responseBody));
            else
                *errorMsg = reqErrorString;
        }
        return QString();
    }

    // 8. 解析 JSON 响应
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseErr);
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
// 便捷重载：直接根据本地文件路径上传
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

/**
 * @brief 提取两个标记之间的内容，并可选择是否包含标记本身。
 * @param original    原始文本
 * @param leftMarker  左侧标记
 * @param rightMarker 右侧标记
 * @param includeSides 若为 true，返回 "左标记 + 中间内容 + 右标记" 的完整子串；
 *                      若为 false，只返回中间的文本。
 * @return 提取到的字符串；若未找到标记，返回空字符串。
 */
/**
 * @brief 从原文本中批量提取所有由左右标记包围的内容。
 * @param original     原始文本
 * @param leftMarker   左侧标记
 * @param rightMarker  右侧标记
 * @param includeSides 若为 true，每个结果包含左右标记；若为 false，只取中间内容。
 * @return QStringList 包含所有匹配结果的列表（按出现顺序排列）。
 *         如果未找到任何匹配，返回空列表。
 */
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
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(url)));

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        errorMsg = reply->errorString();
        reply->deleteLater();
        return false;
    }

    // 读取数据
    QByteArray data = reply->readAll();
    reply->deleteLater();

    // 写入文件
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
QString python_code4(const QString &py_code,QList<QString> user_list)
{
    py::gil_scoped_acquire gil;
    try {
        py::module_ qiancao = py::module_::import("qiancao_sdk");
        py::object api = qiancao.attr("QQApi")(g_keyuuid);

        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");
        exec_globals["UserList"] = py::cast(user_list);
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

        // 准备执行环境（保留 qq_api 的全局命名空间，但不再注入 api/msg）
        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");

        // 执行用户代码
        py::exec(py_code.toStdString(), exec_globals);

        // 恢复 stdout 并获取 print 输出
        sys.attr("stdout") = stdout_old;
        std::string output = py::str(stringio.attr("getvalue")());
        return QString::fromStdString(output);
    } catch (const std::exception &e) {
        // 尝试恢复 stdout（避免影响后续调用），忽略恢复中的异常
        if (!sys.is_none() && !stdout_old.is_none()) {
            try { sys.attr("stdout") = stdout_old; } catch (...) {}
        }
        return QString::fromUtf8(e.what());
    }
}