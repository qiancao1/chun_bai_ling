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
int plugin_n=0;
void botnomsg(int appid,int type,const QString &openid,const QString &msgid)
{

    int tabIndex=type + 1;
    if(tabIndex<1 || tabIndex>4) return;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    if(index<0) return;


    int n = g_logdb [tabIndex]->incrementBufferStatus(index);
    if(n == 255) return; //255代表被处理了
    //qDebug()<< "未回应计数：" <<entry.n;
    if(n>=plugin_n && m_botClients.contains(appid))
    {

        QQBotClient *c =  m_botClients[appid];
        if(c->m_info->fallbackReply.isEmpty()) return;
        QString text="[未被处理回应]";
        c->send_messages(type,openid,text,c->m_info->fallbackReply,msgid);
    }

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
QString python_code(const QString &py_code,const MessageEvent &msg);
QString ruqunhy(const AccountInfo *info,const MessageEvent &ev);
QString 内置指令(MessageEvent &ev)
{
    QString text;
    if(ev.msg=="我的id" || ev.msg=="我的ID")
    {
        text = QString("**我的ID**\n>user_id:%1\n个人ID:%2\n群ID：%3").arg(ev.user_int).arg(ev.user, ev.groupId);
    }

    return text;
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
    if(ret.isEmpty()) ret = schedule->ppzl(ev,text);

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
    if(ev.at_you || !ev.fullType) botnomsg(ev.appid,ev.type,ev.groupId,ev.msgId);
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

QString ruqunhy(const AccountInfo *info,const MessageEvent &ev)
{
    if(ev.type!=0) return QString(); //0代表群
    if(ev.member_role<2)//设置入群提示,取消入群提示 0群主 1管理 2群成员
    {
        if(ev.msg=="设置入群提示")
        {
            auto *db = g_botdb[info->appid_int];
            GroupRecord gid;
            db->getGroupInfo(ev.groupId,gid);
            if (!(gid.bitmap & 2))  return "当前已经设置 入群提示"; //已经关闭入群提示
            gid.bitmap &= ~2;
            db->addGroup(ev.groupId,gid);
            return "设置入群提示成功";

        }else if(ev.msg=="取消入群提示"){
            auto *db = g_botdb[info->appid_int];
            GroupRecord gid;
            db->getGroupInfo(ev.groupId,gid);
            if (gid.bitmap & 2)  return "当前没有设置 入群提示"; //已经关闭入群提示
            gid.bitmap |= 2;
            db->addGroup(ev.groupId,gid);
            return "取消入群提示成功 如需打开 请发送 [设置入群提示]()";
        }
    }
    if(ev.subType==2)
    {
        if(info->rqhy.isEmpty()) return QString();
        auto *db = g_botdb[info->appid_int];
        GroupRecord gid;
        db->getGroupInfo(ev.groupId,gid);
        if (gid.bitmap & 2) return QString(); //已经关闭入群提示
        qint64 now = QDateTime::currentSecsSinceEpoch();
        qint64 CD=gid.xychy_time - now;
        if(CD <= 0 || info->fasjg<=0 || CD > info->fasjg)
        {
            if(info->fasjg>0)
            {
                gid.xychy_time= now + info->fasjg;
                db->addGroup(ev.groupId,gid);
            }
            if(!info->rqhy.startsWith("#python")) return info->rqhy;
            return python_code(info->rqhy,ev);
        }
    }
    return QString();
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

