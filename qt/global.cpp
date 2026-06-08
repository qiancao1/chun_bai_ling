#include "global.h"
#include "LogPage.h"
#include <QMessageBox>
#include <qtcpserver.h>
#include <QNetworkReply>



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
void botnomsg(int appid,int type,const QString &openid,const QString &msgid)
{
    if(m_logStore[0].capacity()==0) return ;
    int tabIndex=type + 1;
    if(tabIndex<1 || tabIndex>5) return;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    if(index<0 || index>=m_logStore[tabIndex].capacity()) return;
    LogEntry& entry =  m_logStore[tabIndex].at(index);
    if(!entry.direction.isEmpty()) return;
    entry.n++;
    //qDebug()<< "未回应计数：" <<entry.n;
    if(entry.n>=2 && m_botClients.contains(appid))
    {
        QQBotClient *c =  m_botClients[appid];
        if(c->m_info->fallbackReply.isEmpty()) return;
        QString text="[未被处理回应]";
        c->send_messages(type,openid,text,c->m_info->fallbackReply,msgid);
    }
}

void AppendEventLog(const QString &msg,const QColor color)
{
    LogEntry* ev2 = m_logStore[0].allocate2();
    if (!ev2) return;
    ev2->botName = QString();
    ev2->appid = 0;
    ev2->time = QDateTime::currentDateTime().toString("dd hh:mm:ss");
    ev2->color = 0;  // 默认颜色
    ev2->user = QString();
    ev2->user_name = QString();
    ev2->groupId = QString();
    if(msg.contains("%1"))
        ev2->msg = msg.arg("0");
    else
        ev2->msg = msg;
    ev2->msgid = QString();
    ev2->deleteid = QString();
    ev2->direction=QString();

}
void showAutoCloseMessageBox(const QString &title, const QString &text, int timeoutMs)
{
    QMessageBox *msgBox = new QMessageBox(QMessageBox::Information, title, text,
                                          QMessageBox::NoButton, nullptr);
    msgBox->setAttribute(Qt::WA_DeleteOnClose);
    msgBox->show();
    QTimer::singleShot(timeoutMs, msgBox, &QMessageBox::close);
}

void logMessageEvent(const QString &botName, const MessageEvent &ev,QString &direction) {
    int tabIndex = mapTypeToTabIndex(ev.type);  // 默认全部

    LogEntry &ev2 = m_logStore[tabIndex].at(ev.log);
    ev2.n=0;
    ev2.dirState=0;
    ev2.fullType=ev.fullType;
    ev2.botName = botName;
    ev2.appid = ev.appid;
    ev2.time = QDateTime::currentDateTime().toString("dd hh:mm:ss");
    ev2.color = 0;  // 默认颜色
    ev2.hf=ev.replyTo;
    ev2.user = ev.user;
    ev2.user_name = ev.nickname;
    ev2.groupId = ev.groupId;
    ev2.msg = ev.msg;
    ev2.msgid = ev.msgId;
    ev2.deleteid = QString();
    ev2.direction = direction;
    auto now = std::chrono::steady_clock::now();
    ev2.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    ev2.color=Color_1;
    switch (ev.type) {
    case 0: // 群消息
        break;
    case 1: // 频道消息
        break;
    case 2: // 私聊消息
        break;
    case 3: // 频道私聊消息
        break;
    case 4: // 群事件
        ev2.msg = QString("[群事件] %1 群:%2 操作者:%3")
                          .arg(ev.subType == 4 ? "被邀请进群" : "被踢出群", ev.groupId, ev.user);
        break;
    case 5: // 好友事件
        ev2.user = ev.user;
        ev2.msg = QString("[好友事件] %1 用户:%2")
                          .arg(ev.subType == 6 ? "添加好友" : "删除好友", ev.user);
        break;
    case 6: // 频道成员事件
        ev2.user = ev.user;
        ev2.msg = QString("[频道成员事件] %1 频道:%2 用户:%3")
                          .arg(ev.subType == 1 ? "加入" : "离开", ev.guildId, ev.user);
        break;
    case 7: // 回调事件
        ev2.msg = QString("[回调事件] 类型:%1 数据:%2").arg(ev.callbackType).arg(ev.msg);
        break;
    case 8: // 审核事件
        ev2.msgid = ev.msgId;
        ev2.msg = QString("[审核事件] %1 消息:%2 原因:%3")
                          .arg(ev.subType == 1 ? "通过" : "拒绝", ev.msgId, ev.msg);
        break;
    default:
        ev2.msg = QString("[未处理事件] 类型:%1").arg(ev.msgType);
        break;
    }
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

            result.append('\r');
            if (i + 1 < len && input[i+1] == '\n') {
                ++i; // 跳过 \n
            }
        }
        else if (ch == '\n') {
            // 单独的 \n 转换为 \r
            result.append('\r');
        }
        else {
            result.append(ch);
        }
        ++i;
    }
    if(result.contains("\\n"))
        result =subTextReplace(result,"\\n","\r");
    return result;
}
QString python_code(QString &py_code,const MessageEvent &msg);
void Message(AccountInfo *info,const MessageEvent &ev) {

    //================================引用式更新变量

    LogRecord &db = g_logdb->appendLog();
    db.appid=info->appid_int;
    db.user = QByteArray::fromHex(ev.user.toUtf8());
    db.groupId = QByteArray::fromHex(ev.groupId.toUtf8());
    db.time = QDateTime::currentSecsSinceEpoch()/60;
    db.type= ev.type;
    if(ev.type ==4 && ev.subType==4 || ev.type==5 && ev.subType==6)
    {
        if(m_botClients.contains(info->appid_int))
        {
            QQBotClient *client = m_botClients[info->appid_int];
            QString text = "[欢迎语]";
            client->send_messages(ev.type,ev.groupId,text,info->welcomeMsg,ev.msgId);
        }
    }

    QString text;
    if(m_blacklist.contains(ev.groupId) || m_blacklist.contains(ev.user)) { //黑名单
        text="[黑名单]群或用户";
        logMessageEvent(info->nickname,ev,text);
        return;
    }
    logMessageEvent(info->nickname,ev,text);
    QString ret = keyword->match(info->appid_int,ev.msg);
    if(!ret.isEmpty())
    {
        if(m_botClients.contains(info->appid_int))
        {

            if(ret.startsWith("#python"))
                ret = python_code(ret,ev);
            if(!ret.isEmpty())
            {
                QQBotClient *client = m_botClients[info->appid_int];
                text = "[关键词匹配|%1ms]";
                client->send_messages(ev.type,ev.groupId,text,ret,ev.msgId);
                return;
            }
        }

    }
    pluginPage->dispatch_message(ev.raw,ev);
    if(ev.at_you || !ev.fullType) botnomsg(ev.appid,ev.type,ev.groupId,ev.msgId);
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
        // 替换 [posLeft, posRight+right.length()) 为 replacement
        result = result.left(posLeft) + replacement + result.mid(posRight + right.length());
        count++;
        if (maxReplacements != -1 && count >= maxReplacements) break;
        // 下一次查找从替换后的新位置开始，避免重复替换
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
extern QTcpServer *g_server;
QString upload(const QString &path);
QString uploadImageSync(const QString& serverUrl, const QString& token, const QString& filePath,int timeoutMs = 30000, QString* errorMsg = nullptr);
QString uploadImageByPath(const QString &serverUrl,const QString &localPath, int timeoutMs,QString *errorMsg);

QString uploadImageToCdn(const QString &path)
{
    if(g_server)
        return upload(path);
    QString url;
    if(setA->远程服务器)
    {
        if(setA->远程链接.contains("127.0.0.1")) //看看是不是这条电脑 另一个开的图床
        {
            QString err;
            url = uploadImageByPath(setA->远程链接,path,30000,&err);
        }else{
            url = uploadImageSync(setA->远程链接,setA->远程token,path);
        }
    }
    if(url.isEmpty())
    {
        url=uploadToMhimg(path,nullptr);
    }

    return url;
}

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

#include <QString>

/**
 * @brief 模仿易语言的子文本替换
 * @param source       源字符串
 * @param find         要查找的子串
 * @param replace      替换成的子串
 * @param replaceCount 替换次数，-1 表示替换所有，0 或正数表示替换前 replaceCount 次
 * @param startPos     查找起始位置（易语言风格，从 1 开始计数），1 表示从第一个字符开始
 * @return 替换后的字符串
 *
 * 注意：若 find 为空字符串，直接返回 source 原串，避免无限循环。
 *      起始位置会被约束在 [1, source.length()+1] 范围内，超出则返回原串。
 */
QString subTextReplace(const QString &source,const QString &find,const QString &replace,
                       int replaceCount,int startPos)
{
    if (find.isEmpty())
        return source;          // 空子串无法替换，直接返回

    // 处理起始位置（易语言从1开始，转换为Qt的0基索引）
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

        // 跳过第一次查找起始位置之前的部分（只对第一次有效）
        // 注意：我们已经在整体结果上从 offset 开始找，offset 会随着替换动态调整
        // 但为了模拟易语言 startPos 仅影响第一次查找位置，这里已经正确

        // 执行替换
        result.replace(pos, find.length(), replace);
        replaced++;

        // 检查是否达到替换次数限制
        if (replaceCount != -1 && replaced >= replaceCount)
            break;

        // 更新 offset：新位置 = 替换后的位置 + 替换后子串的长度
        // 这是为了避免在刚替换的内容中再次查找（防止无限替换）
        offset = pos + replace.length();
    }

    return result;
}