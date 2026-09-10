#include <QJsonObject>
#include "aiwidget.h"
#include <QJsonDocument>
#include <QJsonArray>
#include "bqbgl.h"
#include "global.h"
#include <QTextDocumentFragment>

#include "netmanager.h"
#include <memory>
#include <QPointer>

extern bqbgl *ai_bqbgl;

// 异步状态机的「下一步」句柄。
// lambda 想递归调用自己，正常做法是先捕获持有它的 shared_ptr；但嵌套 lambda 不能跳过
// 中间那层去捕获更外层的局部变量（就是刚才那个编译错误）。所以改成把自身当参数传进去。
// 附带好处：fn 不持有指向自己的引用 → 不会循环引用泄漏。
struct AsyncStep;
using AsyncStepPtr = std::shared_ptr<AsyncStep>;
struct AsyncStep {
    std::function<void(const AsyncStepPtr &self, int a, int b)> fn;
};
QString _tools(const QString &code,const QString &args,const MessageEvent &ev,const QString &mode)
{
    Ai_Fun aifun;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(args.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {

        return "调用函数时 参数错误\n";
    }
    QJsonObject obj=doc.object();

    //{"p2":"苹果","p1":"512x512"}
    aifun.p1=obj["p1"].toString();
    aifun.p2=obj["p2"].toString();
    aifun.p3=obj["p3"].toString();
    aifun.p4=obj["p4"].toString();
    aifun.p5=obj["p5"].toString();
    aifun.p6=obj["p6"].toString();
    aifun.p7=obj["p7"].toString();
    aifun.p8=obj["p8"].toString();


    py::gil_scoped_acquire gil;
    try {
        py::module_ qiancao = py::module_::import("qiancao_sdk");
        py::object api = qiancao.attr("QQApi")(g_keyuuid);
        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");
        exec_globals["msg"] = py::cast(ev);
        exec_globals["args"] = py::cast(aifun);
        exec_globals["__model__"] = mode.toStdString();
        exec_globals["api"] = api;
        py::exec(code.toStdString(), exec_globals);
        QString ret;
        if (exec_globals.contains("__result__"))
            ret = QString::fromStdString(py::str(exec_globals["__result__"]));

        return ret;
    } catch (const py::error_already_set &e) {
        return "[Python] Execute code error: " + QString::fromUtf8(e.what());
    } catch (const std::exception &e) {
        return "[Python] Execute code error: " + QString::fromUtf8(e.what());
    }
    return QString();

}


QString browseWeb(const QString &urlString) {


    auto f = NetManager::instance()->get(urlString);


    QString html = f.get();

    QTextDocumentFragment fragment = QTextDocumentFragment::fromHtml(html);
    QString plainText = fragment.toPlainText();

    plainText.replace(QRegularExpression("\\n{3,}"), "\n\n");
    plainText = plainText.trimmed();
    const int MAX_LENGTH = 700000;
    if (plainText.size() > MAX_LENGTH) {
        plainText = plainText.left(MAX_LENGTH) + "\n\n...(内容过长，已截断)";
    }

    return plainText;
}


QString 内置函数处理(const MessageEvent &ev,const QString &tool_name,const QString &args,const QString &model)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(args.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        return "调用函数时 参数错误\n";
    }
    QJsonObject obj=doc.object();
    QString res;
    QString p1 = obj["p1"].toString();
    if(tool_name== "dimg")
    {
        QString p2 =  obj["p2"].toString();
        QString targetPath = "image/" +QString::number(ev.appid)+"/"+p2;
        if (QFile::exists(targetPath)) return  "目标文件已存在，跳过：" + targetPath;
        if(p1.startsWith("http"))
        {
            QString err;
            if(downloadFile(p1,targetPath,err)){
                ai_bqbgl->meiju(QString::number(ev.appid));
                res = "添加表情包成功";
            }else res = "添加表情包失败 错误:"+err;
        }else{
            if (!QFile::exists(p1))  return "本地源文件不存在：" + p1;
            if (QFile::copy(p1, targetPath)) {
                ai_bqbgl->meiju(QString::number(ev.appid));
                res = "复制成功：" + targetPath;
            } else res = "复制失败，可能权限不足或磁盘已满";
        }
    }else if(tool_name == "rimg"){
        QFile file("image/" +QString::number(ev.appid)+"/"+p1);
        ai_bqbgl->meiju(QString::number(ev.appid));
        res = file.remove() ? "删除成功 上下文可能存在 下回合消失" : "删除失败可能不存在";
    }else if(tool_name == "llwye") res = browseWeb(p1);
    else if(tool_name=="dingshy")
    {
        QString pycode=QString("code_ai|||%1|||%2|||%3|||%4").arg(ev.user,ev.groupId,p1).arg(ev.type);
        res = schedule->add_byAi(p1,ev.appid,obj["p2"].toString(),1,pycode);
    }else if(tool_name=="getdings")
    {
        QString pycode;
        res = schedule->get_aids_list(ev.appid,ev.user);
    }else if(tool_name=="redings")
    {
        res = schedule->remov_ds_byai(ev.appid,p1.toInt());
    }else if(tool_name == "byss")
    {
        int y = obj["p2"].toInt();
        if(y<=0) y=1;
        res = browseWeb("https://cn.bing.com/search?q="+ QUrl::toPercentEncoding(p1) +"&first="+QString::number(y*10));
    }else if(tool_name == "run_python")
    {
        QString 设定 =R"(你的主要任务是审核下面python代码，有没有危害系统，恶意删除文件,覆盖某些系统文件,如果执行了 cmd命令 cmd指令有没有危害系统，
或者尝试下载网络文件 并且执行 等，注意有可能会下载东西 但是不执行就可以
代码通过 返回 '[通过]'，需要用户确认 返回 '[待确认]+说明可能的危害,因为用户可能也不懂',返回 '[拒绝]+理由\n\n下面是审核的python代码

)";
        res = ai_ui->Ai_post(model,设定+p1,ev.type);
        if(res.contains("[通过]"))
        {
            res =python_code(p1,ev);
            if(res.isEmpty())
                res = "python执行完成 无返回值";
        }else if(res.contains("[待确认]") || res.contains("[拒绝]"))
        {}else{
            res = "[审核异常]" + res;
        }

    }else if(tool_name =="html_to_img")
    {
        QByteArray out;
        if(p1.startsWith("http"))
        {
            out = ScreenA->captureUrlSync(p1);
        }else{
            out = ScreenA->captureHtmlSync(p1);

        }
        if(!out.isEmpty())
        {
            QUuid uuid = QUuid::createUuid();
            res = "tmp/"+uuid.toString(QUuid::WithoutBraces)+".png";
            if(W_file(res,out))
            {
                res = "![]("+res+")";
            }   else{
                res.clear();
            }
        }

        if(res.isEmpty())
        {
            res = "未启动截图接口 请通知用户配置截图程序";
        }
    }
    return res;
}



QString AiWidget::Ai_posts(const MessageEvent &ev,int model_index,QJsonObject &sxw,int timeoutMs) //内部使用请勿公开
{
    QString err;
    int kswz = modelList[model_index].enabledInterfaceIndices.size();
    for(int n1=0 ;n1<kswz;++n1){
        int index2 =modelList[model_index].enabledInterfaceIndices[n1];
        auto &key = globalInterfaces[index2].keys;
        int len = key.size();
        for(int i2=0;i2< len;++i2)
        {
            int index = globalInterfaces[index2].key_index++;
            index = index % len;
            QString text =  Ai_post(ev,globalInterfaces[index2].url,key[index].key,sxw,err,timeoutMs);
            if(text.isEmpty()) continue;
            return text;
        }
    }


    return err;
}

QByteArray AiWidget::Ai_post3(const QString &url,const QString &key, QJsonObject &sxw,int timeoutMs)
{
    /*
    QJsonArray msgs = sxw["messages"].toArray();
    QJsonObject sxw2 = sxw;
    if (!msgs.isEmpty()) {
        QJsonObject lastMsg = msgs.last().toObject();
        QString role = lastMsg["role"].toString();
        if (role == "user") {
            QString content = lastMsg["content"].toString();
            content += 附加提示词;
            lastMsg["content"] = content;
            msgs[msgs.size() - 1] = lastMsg;
            sxw2["messages"] = msgs;
        }
    }
    */

    QByteArray jsonData = QJsonDocument(sxw).toJson(QJsonDocument::Compact);
    QHash<QString, QString> headers;
    headers.insert("Content-Type", "application/json");
    headers.insert("Authorization", "Bearer " + key);
    std::future<QByteArray> future = NetManager::instance()->post(url, jsonData, headers, timeoutMs);


    return future.get();
}

// 异步 POST：走 NetManager 的回调版接口，不占用发起线程。
// 回调在线程池线程执行（NetManager::CallbackOnPoolThread）。
void AiWidget::Ai_post3Async(const QString &url, const QString &key, const QJsonObject &sxw,
                             int timeoutMs, AiRawCb cb)
{
    QByteArray jsonData = QJsonDocument(sxw).toJson(QJsonDocument::Compact);
    QHash<QString, QString> headers;
    headers.insert("Content-Type", "application/json");
    headers.insert("Authorization", "Bearer " + key);

    NetManager::instance()->postAsync(url, jsonData, headers, timeoutMs,
        [cb](const QString &response, QNetworkReply::NetworkError err) {
            if (cb) cb(response.toUtf8());
        });
}

// 解析一次接口响应（同步 / 异步链路共用）
AiWidget::AiParseResult AiWidget::parseAiResponse(const QByteArray &response, const QString &key,
                                                  QJsonObject &sxw, QString &err, QJsonObject &obj)
{
    if (response.isEmpty()) {
        err += "接口返回空\n";
        return AiParseResult::Failed;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError) {
        err += "接口返回错误json:" + error.errorString()+"\n";
        if (!key.isEmpty() && err.contains(key)) err = subTextReplace(err, key, "...");
        return AiParseResult::Failed;
    }
    obj = doc.object();

    QJsonObject obj2 = obj["error"].toObject();
    QString error_mes = obj2["message"].toString();
    if (error_mes.contains("token")) {
        if (sxw.contains("messages") && sxw["messages"].isArray()) {
            QJsonArray msgs = sxw["messages"].toArray();
            if (msgs.size() > 1) {
                msgs.removeAt(1);
                sxw["messages"] = msgs;
            }
        }
        return AiParseResult::TokenOverflow;
    }
    if (!error_mes.isEmpty()) {
        err += error_mes+"\n";
        return AiParseResult::Failed;
    }
    return AiParseResult::Ok;
}

// 处理一次 AI 响应：追加上下文 + 执行工具调用（同步 / 异步链路共用）
AiWidget::AiStepResult AiWidget::handleAiResponse(const QJsonObject &obj, const MessageEvent &ev, QJsonObject &sxw)
{
    AiStepResult ret;

    QJsonArray arr = obj["choices"].toArray();
    if (arr.isEmpty()) {
        ret.finished = true;
        ret.failed   = true;
        ret.err      = "返回的 choices 为空 请确认接口是否正确\n";
        return ret;
    }
    QJsonObject obj2 = arr.at(0).toObject();
    QJsonObject obj3 = obj2["message"].toObject();
    QString text = obj3["content"].toString().trimmed();
    const QJsonArray arr2 = obj3["tool_calls"].toArray();
    obj3.remove("reasoning_content");
    //qDebug() << "ai回复：" << text << "tool:" << arr2;
    // 将 AI 响应加入上下文
    if (sxw.contains("messages") && sxw["messages"].isArray()) {
        QJsonArray msgs = sxw["messages"].toArray();
        if(arr2.size()==0)
            obj3.remove("tool_calls");
        msgs.append(obj3);
        sxw["messages"] = msgs;
    }

    bool ok = false;
    if (!arr2.isEmpty()) {
        //不传递appid 也不会传递 函数所以这里是调不到的
        if (!text.isEmpty() && m_botClients.contains(ev.appid)) {
            QQBotClient *bot = m_botClients.value(ev.appid);

            if (bot) {
                if (bot->m_info->niren) {
                    QStringList list = text.split("|#|#|");
                    for (auto & s : list)
                    {
                        if(s.isEmpty()) continue;
                        bot->send_messages(ev.type, ev.groupId, "[AI系统]", s, ev.msgId, false, false);
                        doWork(1000);
                    }
                }else{
                    bot->send_messages(ev.type, ev.groupId, "[AI系统]", text, ev.msgId, false, false);
                }
                text = QString();
            }
        }

        for (const QJsonValue &value : arr2) {
            QJsonObject a = value.toObject();
            QJsonObject function = a["function"].toObject();
            QString tool_name = function["name"].toString();
            QString args = function["arguments"].toString();
            QString callID = a["id"].toString();

            QString data = 内置函数处理(ev,tool_name,args,sxw["model"].toString());
            if(!data.isEmpty())
            {
                AppendEventLog(QString("[%1]执行函数:%2\n参数：%3\n\n结果：%4").arg(ev.appid).arg(tool_name,args,data));
                QJsonArray msgs = sxw["messages"].toArray();
                QJsonObject toolMsg;
                toolMsg["role"] = "tool";
                toolMsg["content"] = data;
                toolMsg["tool_call_id"] = callID;
                toolMsg["name"] = tool_name;
                if (tool_name == "run_python" && data.contains("[待确认]"))
                {
                    toolMsg["pycode"]=function;
                }
                msgs.append(toolMsg);
                sxw["messages"] = msgs;
                if (tool_name == "run_python" && data.contains("[待确认]"))
                {
                    int index = accinfo(ev.appid);
                    QString keyboard;
                    if(index>=0)
                    {
                        if (m_accounts[index]->admin.isEmpty()) {
                            keyboard = "\n未设置管理员，请在框架设置管理员后再试。本次审核无效，Python代码不会执行。";
                        } else {
                            keyboard = R"(#b:#{"keyboard":{"content":{"rows":[{"buttons":[{"action":{"data":"同意%1","enter":true,"permission":{"type":2},"type":2,"unsupport_tips":"不支持"},"id":"1","render_data":{"label":"同意","style":1,"visited_label":"同意"}},{"action":{"data":"拒绝%2","permission":{"type":2},"type":2,"unsupport_tips":"不支持"},"id":"2","render_data":{"label":"拒绝","style":1,"visited_label":"拒绝"}}]}]}}}#b:#)";
                            keyboard = keyboard.arg(ev.user, ev.user);
                        }
                    }else{
                        keyboard = "\n未设置管理员，请在框架设置管理员后再试。本次审核无效，Python代码不会执行。";
                    }

                    ret.finished = true;
                    ret.text = data + keyboard;  // 审核信息 + 键盘
                    return ret;
                }
                ok = true;
            }else{
                for (const auto &fun : std::as_const(functionList)) {
                    if (fun.funcName != tool_name) continue;
                    data = _tools(fun.code, args, ev,sxw["model"].toString());

                    if (data.isEmpty()) {
                        data = "函数返回空";
                    }
                    AppendEventLog(QString("[%1]Ai执行函数:%2:结果：%3").arg(ev.appid).arg(tool_name,data));
                    if (sxw.contains("messages") && sxw["messages"].isArray()) {
                        QJsonArray msgs = sxw["messages"].toArray();
                        QJsonObject toolMsg;
                        toolMsg["role"] = "tool";
                        toolMsg["content"] = data;
                        toolMsg["tool_call_id"] = callID;
                        toolMsg["name"] = tool_name;
                        msgs.append(toolMsg);
                        sxw["messages"] = msgs;
                    }
                    if(fun.interrupt) {
                        ret.finished = true;
                        ret.text = data;
                        return ret;
                    }
                    ok = true;
                    break;
                }
            }
        }
        if (ok) return ret;   // 调完工具，带着新上下文再发一轮
    }

    ret.finished = true;
    ret.text = text;
    return ret;
}

QString AiWidget::Ai_post(const MessageEvent &ev, const QString &url, const QString &key, QJsonObject &sxw, QString &err, int timeoutMs)
{

    if (!ev.msg.isEmpty()) {
        if (sxw.contains("messages") && sxw["messages"].isArray()) {
            QJsonArray msgs = sxw["messages"].toArray();
            QJsonObject userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = ev.msg;
            msgs.append(userMsg);
            sxw["messages"] = msgs;
        }
    }

    for(int i=0; i<10; ++i) {
        QJsonObject obj;
        for(int i2=0; i2<3; ++i2) {

            QByteArray response = Ai_post3(url, key, sxw, timeoutMs);
            AiParseResult r = parseAiResponse(response, key, sxw, err, obj);
            if (r == AiParseResult::TokenOverflow) continue;   // 删了一条历史，重试
            if (r == AiParseResult::Failed) return QString();
            break;
        }

        AiStepResult s = handleAiResponse(obj, ev, sxw);
        if (!s.finished) continue;
        if (s.failed) {
            err += s.err;
            return QString();
        }
        return s.text;
    }
    return QString();
}

// 异步核心：把上面那个「3 次重试 × 最多 10 轮工具调用」的循环拆成状态机。
// sxw / err 用 shared_ptr 持有 —— 整条链会跨线程接着往下走，不能放栈上。
//
// AsyncStep 把「自己」以参数形式传给 lambda：
//   - 嵌套 lambda 里捕获 self 是合法的（self 是外层 lambda 的形参，不是更外层的局部变量）；
//   - fn 本身不持有指向自己的引用，所以不会循环引用；
//   - 在途请求的回调强引用 self → 请求期间一定活着；跑完自动释放。
void AiWidget::Ai_postAsyncCore(const MessageEvent &ev, const QString &url, const QString &key,
                                std::shared_ptr<QJsonObject> sxw, std::shared_ptr<QString> err,
                                int timeoutMs, AiReplyCb cb)
{
    auto step = std::make_shared<AsyncStep>();

    step->fn = [this, ev, url, key, sxw, err, timeoutMs, cb]
               (const std::shared_ptr<AsyncStep> &self, int round, int retry) {
        if (round >= 10) {                 // 工具调用轮次用尽
            if (cb) cb(QString());
            return;
        }

        Ai_post3Async(url, key, *sxw, timeoutMs,
            [this, ev, url, key, sxw, err, timeoutMs, cb, self, round, retry](const QByteArray &response) {
                QJsonObject obj;
                AiParseResult r = parseAiResponse(response, key, *sxw, *err, obj);
                if (r == AiParseResult::Failed) {
                    if (cb) cb(QString());
                    return;
                }
                if (r == AiParseResult::TokenOverflow && retry + 1 < 3) {
                    self->fn(self, round, retry + 1);
                    return;
                }
                // 三次都超 token：和同步版一样，带着最后一次的 obj 往下走
                // （choices 为空 → 记错并返回空，让 Ai_postsAsyncCore 换 key）
                AiStepResult s = handleAiResponse(obj, ev, *sxw);
                if (!s.finished) {
                    self->fn(self, round + 1, 0);
                    return;
                }
                if (s.failed) {
                    *err += s.err;
                    if (cb) cb(QString());
                    return;
                }
                if (cb) cb(s.text);
            });
    };

    step->fn(step, 0, 0);   // (第几轮, 第几次重试)
}

void AiWidget::Ai_postAsync(const MessageEvent &ev, const QString &url, const QString &key,
                            const QJsonObject &sxw, int timeoutMs, AiReplyCb cb)
{
    auto sxwPtr = std::make_shared<QJsonObject>(sxw);

    if (!ev.msg.isEmpty()) {
        if (sxwPtr->contains("messages") && (*sxwPtr)["messages"].isArray()) {
            QJsonArray msgs = (*sxwPtr)["messages"].toArray();
            QJsonObject userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = ev.msg;
            msgs.append(userMsg);
            (*sxwPtr)["messages"] = msgs;
        }
    }

    Ai_postAsyncCore(ev, url, key, sxwPtr, std::make_shared<QString>(), timeoutMs, std::move(cb));
}

// 异步版 Ai_posts：逐个接口、逐个 key 试；全部失败时把累积的错误串回调出去（同步版 return err）
void AiWidget::Ai_postsAsync(const MessageEvent &ev, int model_index, const QJsonObject &sxw,
                             int timeoutMs, AiReplyCb cb)
{
    // 注意：内部会复制一份上下文，调用方拿不到请求过程中新增的消息。
    // 需要拿到更新后上下文的地方（flushPendingMessagesTail）请用 Ai_postsAsyncCore。
    Ai_postsAsyncCore(ev, model_index, std::make_shared<QJsonObject>(sxw), timeoutMs, std::move(cb));
}

void AiWidget::Ai_postsAsyncCore(const MessageEvent &ev, int model_index,
                                 std::shared_ptr<QJsonObject> sxwPtr, int timeoutMs, AiReplyCb cb)
{
    if (model_index < 0 || model_index >= modelList.size()) {
        if (cb) cb(QString());
        return;
    }

    auto err = std::make_shared<QString>();

    auto step = std::make_shared<AsyncStep>();   // (第几个接口, 第几个 key)

    step->fn = [this, ev, model_index, sxwPtr, err, timeoutMs, cb]
               (const std::shared_ptr<AsyncStep> &self, int n1, int i2) {
        auto next = [self](int a, int b) { self->fn(self, a, b); };

        if (model_index >= modelList.size()) { if (cb) cb(*err); return; }
        const QList<int> indices = modelList.at(model_index).enabledInterfaceIndices;
        if (n1 >= indices.size()) {
            if (cb) cb(*err);
            return;
        }

        int index2 = indices.at(n1);
        if (index2 < 0 || index2 >= globalInterfaces.size()) { next(n1 + 1, 0); return; }

        InterfaceData &iface = globalInterfaces[index2];
        const int len = iface.keys.size();
        if (len == 0 || i2 >= len) { next(n1 + 1, 0); return; }

        int index = iface.key_index++;
        index = index % len;
        const QString url = iface.url;
        const QString k   = iface.keys.at(index).key;

        Ai_postAsyncCore(ev, url, k, sxwPtr, err, timeoutMs,
            [cb, self, n1, i2](const QString &text) {
                if (text.isEmpty()) {          // 这个 key 没出内容，换下一个
                    self->fn(self, n1, i2 + 1);
                    return;
                }
                if (cb) cb(text);
            });
    };

    step->fn(step, 0, 0);
}
void AiWidget::flushPendingMessages(const QString &openid,bool send)
{
    if (m_shuttingDown) return;   // 正在析构，别再碰任何成员

    // 本函数跑在线程池线程。绝不能持有 m_sessions 内部元素的引用跨异步边界：
    // 主线程（清理定时器 / 析构）随时可能 erase 掉这个会话，那个引用立刻变悬空，
    // 后面再写就是写已释放内存 —— 表现为别处 delete/stop 时莫名其妙崩。
    // 所以这里拷一份值，后面只用副本；要写回的字段走带锁的 setSessionProcessing。
    SessionContext session;
    {
        QMutexLocker lk(&m_sessionsMutex);
        session = m_sessions.value(openid);
        if (!send) {
            if (session.pendingMessages.isEmpty())
                return;
            if (m_sessions.contains(openid))
                m_sessions[openid].pendingMessages.clear();
        }
    }

    AccountInfo* info = session.accountInfo;
    if (!info) {
        setSessionProcessing(openid, false);
        return;
    }

    // 查找模型索引
    int model_index = -1;
    for (int i = 0; i < modelList.size(); ++i) {
        if (modelList[i].name == info->model) {
            model_index = i;
            break;
        }
    }
    if (model_index == -1) {
        setSessionProcessing(openid, false);
        return;
    }

    // 待处理消息已在上面锁内取走并清空，这里直接用副本（send=true 时不带历史消息）。
    // 后面走异步，回调期间主线程还会往 session.pendingMessages 里追加，
    // 所以只能用这份快照，不再碰会话里的 list。
    QList<PendingMessage> pendings;
    if (!send) pendings = session.pendingMessages;

    QJsonObject baseContext = buildBaseContext(session.accountInfo,session.groupId, openid,session.type);
    int oldMsgCount = 0;
    if (baseContext.contains("messages") && baseContext["messages"].isArray()) {
        oldMsgCount = baseContext["messages"].toArray().size();
    }

    // 构造空 MessageEvent
    MessageEvent ev;
    ev.appid = session.appid;
    ev.type = session.type;
    ev.groupId = session.groupId;
    ev.msgId = session.msgId;
    ev.user = session.openid;
    ev.msg = "";

    if(!send && session.type==0 && info->juece && session.cflx!=1 && !pendings.isEmpty()){
        QJsonObject juece_mode=baseContext;

        QString setting;
        for (const auto &sd : std::as_const(m_globalSettings)) {
            if (sd.name == info->setting) {
                setting = sd.content;
                break;
            }
        }
        trimContextByMessageCount(juece_mode, 24); //限制上下文
        QJsonArray msgs = juece_mode["messages"].toArray();
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = "你是群聊对话中决策AI\n你的任务是查看下面对话 是否有对话提到了你，以及和你的设定相符合的内容，\n"
                               "1.如你主要扮演原神中的纳西妲，当聊天内容包含原神你就可以返回'【提到】'\n"
                               "2.如果是拟人扮演 设定是爱多管闲事也可以返回 提到"
                               "3.或者你的上下文正在聊天"
                               "4.注意你只是决策 并不需要回复用户只需要回复 【提到】 或 其他"
                               "5.不能长期不回复，所以偶尔 决策 【提到】"
                               "如果提到了 请回复'【提到】'文本 我会判定 你返回的内容有没有这个字符"
                               "下面是用户给你的角色设定：\n"+setting;
        msgs[0] = systemMsg;
        QJsonObject systemMsg2;
        systemMsg2["role"] = "user";
        systemMsg2["content"] = "下面是新的对话内容";
        msgs.append(systemMsg2);
        juece_mode["messages"] = msgs;

        for (const PendingMessage &pm : std::as_const(pendings)) {
            appendPendingMessageToContext(juece_mode, pm);
        }
        trimContextImages(juece_mode, 0);//处理图片
        juece_mode.remove("tools");

        // 决策请求异步发出，本函数立即返回（不再占着线程池的 worker 干等）
        // 先占住 isProcessing，避免回调回来之前定时器又触发一轮
        setSessionProcessing(openid, true);
        Ai_postsAsync(MessageEvent(), model_index, juece_mode, 60000,
            [this, openid, info, model_index, baseContext, oldMsgCount, pendings, ev](const QString &fh) {
                flushPendingMessagesTail(openid, info, model_index, baseContext, oldMsgCount,
                                         fh.contains("【提到】"), fh, pendings, ev);
            });
        return;
    }

    flushPendingMessagesTail(openid, info, model_index, baseContext, oldMsgCount,
                             true, QString(), pendings, ev);
}

// flushPendingMessages 的后半段：可能是紧跟在上面同步执行，也可能在决策请求的回调里执行
void AiWidget::flushPendingMessagesTail(const QString &openid, AccountInfo *info, int model_index,
                                       QJsonObject baseContext, int oldMsgCount,
                                       bool juecejg, const QString &fh,
                                       const QList<PendingMessage> &pendings, const MessageEvent &ev)
{
    if (m_shuttingDown) return;   // 正在析构（本函数可能在回调线程执行）
    if (!info) return;

    for (const PendingMessage &pm : pendings) {
        appendPendingMessageToContext(baseContext, pm);
    }

    if(!juecejg) //决策没提到 直接返回
    {

        Message msg;
        QDateTime currentDateTime = QDateTime::currentDateTime();
        msg.timestamp = currentDateTime.toString("yyyy-MM-dd HH:mm:ss");
        msg.msg = QString("%1(%2) ai决策 不返回内容：%3").arg(info->Ai_nickname,openid,fh);
        logPage->onNewLogAdded(0,0,0,"",msg);

        baseContext.remove("tools");
        aidb->put(info->appid+":"+openid, QJsonDocument(baseContext).toJson(QJsonDocument::Compact));
        setSessionProcessing(openid, false);
        return;
    }
    trimContextImages(baseContext, 6);//处理图片
    trimToolResponses(baseContext, 5, 64);
    if(info->context_len<5)
        info->context_len=5;
    trimContextByMessageCount(baseContext, info->context_len); //限制上下文
    aidb->put(info->appid+":"+openid, QJsonDocument(baseContext).toJson(QJsonDocument::Compact));
    convertContextImagesToBase64(baseContext);//图片转b64


    setSessionProcessing(openid, true);

    if (ev.type==2 && info->xiangliang && !info->Embed_model.isEmpty()) {

        QString lastUserMsg;
        QJsonArray msgs = baseContext["messages"].toArray();
        for (int i = msgs.size() - 1; i >= 0; --i) {
            if (msgs[i].toObject()["role"].toString() == "user") {

                QJsonObject obj =msgs[i].toObject();
                QJsonArray arr=obj["content"].toArray();
                QJsonObject obj2 = arr.at(0).toObject();
                lastUserMsg = obj2["text"].toString();
                break;
            }
        }

        if (!lastUserMsg.isEmpty()) {
            int index=-1;
            QVector<double> queryVec;
            for (int i = 0; i < modelList.size(); ++i) {
                if (modelList[i].name == info->Embed_model) {
                    index = i;
                    break;
                }
            }
            if (index != -1) {
                for (int i : std::as_const(modelList[index].enabledInterfaceIndices)) {
                    if (globalInterfaces[i].keys.size() == 0) {
                        queryVec = getEmbedding(lastUserMsg,
                                                globalInterfaces[i].url,
                                                info->Embed_model,
                                                QString());
                    } else {
                        for (const auto &key : std::as_const(globalInterfaces[i].keys)) {
                            //if (!key.enabled) continue;
                            queryVec = getEmbedding(lastUserMsg,
                                                    globalInterfaces[i].url,
                                                    info->Embed_model,
                                                    key.key);
                            if (!queryVec.isEmpty()) break;
                        }
                    }
                    if (!queryVec.isEmpty()) break;
                }
            }

            // 取一份 memory 的共享副本（本函数在回调线程）：主线程就算清理了会话，
            // 这里还持有一份引用，对象不会中途被 delete。
            QSharedPointer<VectorMemory> mem;
            {
                QMutexLocker lk(&m_sessionsMutex);
                if (m_sessions.contains(openid))
                    mem = m_sessions[openid].memory;
            }

            if (mem && !queryVec.isEmpty()) {

                std::vector<float> queryFloatVec(queryVec.begin(), queryVec.end());
                auto results = mem->search(queryFloatVec, 3); // 取3条最相似的

                if (!results.empty()) {

                    QString memoryText = "【用户历史信息】\n";
                    for (const auto &[id, score] : results) {

                        std::string meta = mem->getMetadata(id);
                        if (!meta.empty()) {
                            memoryText += "- " + QString::fromStdString(meta) + "\n";
                        }
                    }
                    qDebug() <<"向量读取 "<< memoryText;

                    QJsonArray newMsgs = baseContext["messages"].toArray();
                    QJsonObject sysMsg;
                    sysMsg["role"] = "system";
                    sysMsg["content"] = memoryText;

                    if (!newMsgs.isEmpty() && newMsgs[0].toObject()["role"].toString() == "system") {
                        QJsonObject existing = newMsgs[0].toObject();
                        existing["content"] = existing["content"].toString() + "\n\n" + memoryText;
                        newMsgs[0] = existing;
                    } else {
                        newMsgs.prepend(sysMsg);
                    }
                    baseContext["messages"] = newMsgs;
                }
            }
        }
    }

    int timeoutMs = 30000;
    // 异步请求：发出后立即返回，worker 线程被放回去干别的活。
    // ctxPtr 全程共享，请求过程中追加的 AI 回复/工具结果会写进它，
    // 回调里原样交给 onAsyncReply 存上下文（和同步版传引用等价）。
    auto ctxPtr = std::make_shared<QJsonObject>(baseContext);
    Ai_postsAsyncCore(ev, model_index, ctxPtr, timeoutMs,
        [this, openid, ev, ctxPtr, oldMsgCount, info](const QString &reply2) {
            QString reply = reply2;
            emit asyncReplyReceived(openid, reply, *ctxPtr, oldMsgCount);

            QQBotClient *bot = m_botClients.value(ev.appid);
            if (!bot) return;
            BotDB *db = g_botdb.value(ev.appid);

            if(reply.contains("![img]("))
                reply.replace("![img](","![img](image/"+info->appid+"/");
            if(reply.contains("![ima]("))
                reply.replace("![ima](","![img](image/"+info->appid+"/");
            QStringList atlist = takeAllTextMiddle(reply,"<@",">",false);//将短id转 unid
            if(atlist.size()!=0 && db)
            {
                for( auto &uid : atlist)
                {
                    QString user;
                    db->getOpenIdBySeqId(uid.toInt(),user);
                    if(user.isEmpty())
                    {
                        reply.remove("<@"+uid+">");
                    }else
                        reply.replace("<@"+uid+">","<@"+user+">") ;
                }
            }
            QStringList text = reply.split("|#|#|");

            // 串行发送：上一条的回调回来了再发下一条（原来是一次性全发出去，长回复会乱序）。
            // 失败降级顺序和同步版一致：
            //   attempt0 带引用(ev.msgId) → attempt1 去掉引用 → attempt2 主动消息(仅 type==2 且原本无 msgId)
            // botSafe 用 QPointer：回调要等一个网络往返，中间机器人可能被删/切换，裸指针会变野指针。
            const bool isw = (ev.type == 2 && ev.msgId.isEmpty());
            QPointer<QQBotClient> botSafe(bot);
            auto texts = std::make_shared<QStringList>(text);
            auto sendStep = std::make_shared<AsyncStep>();

            sendStep->fn = [botSafe, ev, texts, isw]
                (const AsyncStepPtr &self, int i, int attempt) {

                if (!botSafe) return;                       // 机器人已失效，整链结束

                while (i < texts->size() && texts->at(i).trimmed().isEmpty()) ++i;
                if (i >= texts->size()) return;             // 全部发完

                QString t     = texts->at(i).trimmed();
                QString msgid = (attempt == 0) ? ev.msgId : QString();
                bool    wake  = (attempt == 2);
                QString pname = "[Ai系统]";

                botSafe->send_msgAsync(ev.type, ev.groupId, pname, t, msgid, wake, false, 0, false,
                    [self, i, attempt, isw](const QString &rsp, QNetworkReply::NetworkError) {

                        if (rsp.contains("\"id\"")) {       // 这条发成功了，发下一条
                            self->fn(self, i + 1, 0);
                            return;
                        }
                        if (attempt == 0)                   // 带引用失败 → 去掉引用重试
                            self->fn(self, i, 1);
                        else if (attempt == 1 && isw)       // 还失败 → 主动消息
                            self->fn(self, i, 2);
                        else                                // 放弃这条，继续下一条
                            self->fn(self, i + 1, 0);
                    });
            };
            sendStep->fn(sendStep, 0, 0);
        });
}

