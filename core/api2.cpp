#include "cnbuploader.h"
#include "qqbotclient.h"
#include <QJsonDocument>
#include "global.h"
#include <QRandomGenerator>

QString get_url(int type,const QString &openid,const QString &text = QString(),const QString &text2 = QString());
void flushCosPutList(QList<CosPutPoolEntry> list);
QList<CosPutPoolEntry> takeCosPutPending();



void QQBotClient::addmsglog(const QString &response,int index,const QString &pname,const QString &text,qint64 now_us, int type,const QString &openid)
{

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QString message = obj["message"].toString();

    QString deleteid = obj["id"].toString();
    QJsonObject obj2 =obj["ext_info"].toObject();
    QString ref = obj2["ref_idx"].toString();

    int tabIndex = mapTypeToTabIndex(type);
    m_info->message_sent++;
    m_info->sent++;
    m_info->sent_day++;
    double diff_ms=0;
    bool ok=false;
    if(index>0)
    {
        qint64 us = g_logdb[tabIndex]->setBuffer_255(index,ok);
        qint64 diff_us = now_us - us;
        diff_ms = diff_us / 1000.0;
    }
    if(openid == chatPage->currentContactId)
    {
        QMetaObject::invokeMethod(this, [=]() {
            Message m("","",true, QDateTime::currentDateTime().toString("hh:mm:ss"),"","[ref,msg_idx="+ref+"]","");
            if(pname.contains("%1"))
                m.direction = pname.arg(diff_ms) + text;
            else
                m.direction = pname + text;
            if (deleteid.isEmpty() && message != "消息提交安全审核成功")
            {
                m.direction+="\n\n--------------------------\n\n"+response;

            }
            m.plugin_ch =deleteid;
            chatPage->addMessage(m);
        });

    }
    Message msg;
    if(ok)
    {
        g_logdb[tabIndex]->readLog(m_info->appid,openid,index,msg);
        msg.plugin_ch = deleteid;

        if(pname.contains("%1"))
            msg.direction = pname.arg(diff_ms) + text;
        else
            msg.direction = pname + text;

        msg.Color_0 = Color_0;
        if (deleteid.isEmpty() && message != "消息提交安全审核成功")
        {
            msg.direction+="\n\n--------------------------\n\n"+response;
            msg.Color_0 = 0xff0000;
        }

        g_logdb[tabIndex]->updateLog(m_info->appid,openid,index,msg);
        logPage->findRowBySeq(tabIndex,m_info->appid_int,index,msg.direction);
        msg.isSelf=true;
        msg.seq = index;
        if(ws_server) ws_server->broadcastMessage(msg,m_info->appid_int,type,openid);
        return ;
    }

    msg.isSelf = true;
    msg.plugin_ch = deleteid;
    msg.Color_0 = Color_0;
    if(!ref.isEmpty())
        msg.hf="[ref,msg_idx="+ref+"]";
    else
        msg.hf.clear();

    if(pname.contains("%1"))
        msg.direction = pname.arg(diff_ms) + text;
    else
        msg.direction = pname + text;

    if (deleteid.isEmpty() && message != "消息提交安全审核成功")
    {
        msg.direction+="\n\n--------------------------\n\n"+response;
        msg.Color_0 = 0xff0000;
    }

    msg.seq = g_logdb[tabIndex]->appendLog(m_info->appid,openid,msg);
    logPage->onNewLogAdded(tabIndex,0,m_info->appid_int,openid,msg);
    if(ws_server) ws_server->broadcastMessage(msg,m_info->appid_int,type,openid);

    DelFileSync_Cnb();
    return ;
}
QString QQBotClient::uploadRichMediaA(int targetType, const QString& openid,int fileType, const QString& filePath, bool &ok)
{


    qint64 expireTime=0;
    QString md5,info,url;
    if(filePath.startsWith("http"))
    {
        info = uploadRichMedia_url(targetType,openid,fileType,filePath,expireTime,ok);
    }else{
        info = uploadRichMedia(targetType,openid,fileType,filePath,expireTime,md5,ok,url);
    }
    if(!ok) return info;
    QString typeStr;
    switch (fileType) {
    case 1: typeStr = "image"; break;
    case 2: typeStr = "video"; break;
    case 3: typeStr = "audio"; break;
    case 4: typeStr = "file"; break;
    default: typeStr = "unknown";
    }
    return QString("[%1,path=%2,md5=%3,Time=%4]").arg(typeStr,info,md5).arg(expireTime);
}
QString QQBotClient::uploadRichMediaB(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename, bool &ok)
{
    qint64 expireTime=0;
    QString md5,url;
    QString info = uploadRichMedia(targetType,openid,fileType,data,filename,expireTime,md5,ok,url);
    if(!ok) return info;
    QString typeStr;
    switch (fileType) {
    case 1: typeStr = "image"; break;
    case 2: typeStr = "video"; break;
    case 3: typeStr = "audio"; break;
    case 4: typeStr = "file"; break;
    default: typeStr = "unknown";
    }
    return QString("[%1,path=%2,md5=%3,Time=%4]").arg(typeStr,info,md5).arg(expireTime);
}

// uploadRichMediaA 的池子版：入参/返回格式与 A 完全一致，调用点可直接替换
// http 链接 → uploadRichMedia_url（与 A 相同，不涉及 put 池）；本地文件 → uploadRichMediaPool
QString QQBotClient::uploadRichMediaPoolA(int targetType, const QString& openid,int fileType, const QString& filePath, bool &ok, bool usePool)
{
    qint64 expireTime=0;
    QString md5,info,url;
    if(filePath.startsWith("http"))
    {
        info = uploadRichMedia_url(targetType,openid,fileType,filePath,expireTime,ok);
    }else{
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            ok=false;
            return QString();
        }
        QByteArray fileData = file.readAll();
        file.close();
        QString filename = QFileInfo(filePath).fileName();
        info = uploadRichMediaPool(targetType,openid,fileType,fileData,filename,expireTime,md5,ok,url,usePool);
    }
    if(!ok) return info;
    QString typeStr;
    switch (fileType) {
    case 1: typeStr = "image"; break;
    case 2: typeStr = "video"; break;
    case 3: typeStr = "audio"; break;
    case 4: typeStr = "file"; break;
    default: typeStr = "unknown";
    }
    return QString("[%1,path=%2,md5=%3,Time=%4]").arg(typeStr,info,md5).arg(expireTime);
}

//上传富媒体
QString QQBotClient::uploadRichMedia_url(int targetType, const QString& openid,int fileType, const QString& fileurl,
                                         qint64& expireTime,bool &ok)
{
    ok=false;
    if(!fileurl.startsWith("http"))return QString();
    QJsonObject obj;
    obj["file_type"]=fileType;
    obj["url"]=fileurl;

    QString url = get_url(targetType, openid, "files");
    QString file_info,response;
    for(int i=0;i<10;i++)
    {
        response =PostSync(url,obj,QString(),300000);
        if (response.isEmpty()) return QString();
        QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
        if (respDoc.isNull()) return QString();
        QJsonObject respObj = respDoc.object();
        file_info = respObj["file_info"].toString();
        if(!file_info.isEmpty())
        {
            ok=true;
            expireTime = QDateTime::currentSecsSinceEpoch() + respObj["ttl"].toInt();
            return file_info;
        }
        QString err = respObj["message"].toString();
        if(err!="富媒体文件上传超时") return response;
        QThread::msleep(128);

    }
    return response;
}

QString QQBotClient::uploadRichMedia(int targetType, const QString& openid,int fileType, const QString& filePath,
                                     qint64& expireTime,QString &md5,bool &ok,QString &outurl) {
    ok=false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        //qWarning() << "无法打开文件:" << filePath;
        return QString();
    }
    QByteArray fileData = file.readAll();
    file.close();
    QFileInfo info(filePath);
    QString filename = info.fileName();
    return uploadRichMedia(targetType,openid,fileType,fileData,filename,expireTime,md5,ok,outurl);
}

QString QQBotClient::uploadRichMedia(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename,
                                     qint64& expireTime,QString &md5,bool &ok,QString &outurl) {


    qint64 fileSize = data.size();
    ok=false;

    // 2. 计算哈希值
    QCryptographicHash md5Hash(QCryptographicHash::Md5);
    md5Hash.addData(data);
    md5 = md5Hash.result().toHex();
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(data);
    QString sha1 = sha1Hash.result().toHex();
    int tenM = 10 * 1024 * 1024;
    QByteArray first10M = data.left(tenM);
    QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
    md5_10mHash.addData(first10M);
    QString md5_10m = md5_10mHash.result().toHex();

    // 视频分两路：
    //   ≤80M → uploadSmallVideo：prepare 固定申请 1K（返回 1 个分片）→ 整段数据写入 put 链接 →
    //          提交 → files 注册（视频处理可能「富媒体文件上传超时」，循环重试即可）
    //   >80M → 当文件（file_type=4）走原版分片上传（有多大传多大，上限 200M）
    if (fileType == 2) {
        if (fileSize <= 80LL * 1024 * 1024)
            return uploadSmallVideo(targetType, openid, data, filename, expireTime, md5, sha1, md5_10m, ok, outurl);
        fileType = 4;
    }

    // 3. 准备上传准备请求
    // 声明真实大小（有多大传多大）。
    QJsonObject prepJson;
    prepJson["file_type"] = fileType;
    prepJson["file_name"] = filename;
    prepJson["file_size"] = (qint64)fileSize;
    prepJson["md5"] = md5;
    prepJson["sha1"] = sha1;
    prepJson["md5_10m"] = md5_10m;
    //prepJson["block_size"] = fileSize;
    QString url = get_url(targetType, openid, "upload_prepare");

    QString response = PostSync(url, prepJson,QString(), 30000);

    if (response.isEmpty()) return QString();

    // 4. 解析响应获取 upload_id 和 parts
    QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
    if (respDoc.isNull()) return QString();
    QJsonObject respObj = respDoc.object();
    QString upload_id = respObj["upload_id"].toString();
    if (upload_id.isEmpty()) return response; // 错误信息

    QJsonArray parts = respObj["parts"].toArray();

    // 5. 准备分片完成确认用的 JSON 基座
    QJsonObject partFinishBase;
    partFinishBase["upload_id"] = upload_id;
    int start =0;
    const int MAX_RETRIES = 3;
    const int BASE_TIMEOUT_MS = 30000;
    QString finishUrl = get_url(targetType, openid, "upload_part_finish");
    if(g_neiw.isEmpty()){
        for (int i = 0; i < parts.size(); ++i) {
            QJsonObject part = parts[i].toObject();
            int index = part["index"].toInt();
            QString blockSize = part["block_size"].toString();
            int blockSizeA=blockSize.toInt();
            QString presignedUrl = part["presigned_url"].toString();


            QByteArray chunk = data.mid(start, blockSizeA);
            start += blockSizeA;
            bool success = false;
            int retry=0;
            int currentTimeout = BASE_TIMEOUT_MS;
            while (retry < MAX_RETRIES && !success) {
                try {

                    put(presignedUrl, chunk, "application/octet-stream", currentTimeout);

                    success = true;
                } catch (const std::exception &e) {
                    //qWarning() << "分片" << index << "上传失败 (尝试" << retry+1 << "):" << e.what();
                    retry++;
                    if (retry < MAX_RETRIES) {
                        int sleepMs = 1000 * (1 << (retry - 1));
                        QThread::msleep(sleepMs);
                        currentTimeout += 10000;
                    }
                }
            }
            if(success==false)
            {
                ok=false;
                return QString("在上传%1分片时重试多次失败").arg(index);
            }
            QJsonObject finishJson;
            finishJson["upload_id"] = upload_id;
            finishJson["part_index"] = index;
            finishJson["block_size"] = chunk.size();
            QCryptographicHash chunkMd5(QCryptographicHash::Md5);
            chunkMd5.addData(chunk);
            finishJson["md5"] = QString(chunkMd5.result().toHex());


            QString finishResp = PostSync(finishUrl, finishJson,QString(), 30000);

        }
    }else{

        QElapsedTimer times;
        times.start();
        int totalParts = parts.size();
        int startPos = 0;

        std::vector<std::future<QByteArray>> futures;
        QList<QByteArray> chunks;          // 保存分片数据
        QList<QJsonObject> finishJsons;

        for (int i = 0; i < totalParts; ++i) {
            QJsonObject part = parts[i].toObject();
            int index = part["index"].toInt();
            int blockSize = part["block_size"].toString().toInt();
            QString presignedUrl = part["presigned_url"].toString();
            QByteArray chunk = data.mid(startPos, blockSize);
            startPos += blockSize;
            chunks.append(chunk);

            std::future<QByteArray> fut = put2(presignedUrl, chunk, "application/octet-stream", BASE_TIMEOUT_MS);
            futures.push_back(std::move(fut));
            QJsonObject finishJson;
            finishJson["upload_id"] = upload_id;
            finishJson["part_index"] = index;
            finishJson["block_size"] = chunk.size();
            QCryptographicHash chunkMd5(QCryptographicHash::Md5);
            chunkMd5.addData(chunk);
            finishJson["md5"] = QString(chunkMd5.result().toHex());
            finishJsons.append(finishJson);
        }

        for (int j = 0; j < futures.size(); ++j) {
            bool success = false;
            int retry = 0;
            int currentTimeout = BASE_TIMEOUT_MS;
            const QByteArray &chunk = chunks[j]; // 保存的数据，用于重试
            while (retry < MAX_RETRIES && !success) {
                try {

                    QString resp;
                    if (retry == 0) {
                        // 第一次使用已存储的 future
                        resp = futures[j].get();

                    } else {

                        std::future<QByteArray> newFut = put2(
                            parts[j].toObject()["presigned_url"].toString(), // 直接用索引 j
                            chunk,
                            "application/octet-stream",
                            currentTimeout
                            );
                        resp = newFut.get();
                    }

                    success = true;
                } catch (const std::exception &e) {
                    //qWarning() << "分片" << finishJsons[j]["part_index"].toInt()
                    //    << "上传失败 (尝试" << retry+1 << "):" << e.what();
                    retry++;
                    if (retry < MAX_RETRIES) {
                        QThread::msleep(1000 * (1 << (retry - 1)));
                        currentTimeout += 10000;
                    }
                }
            }
            if (!success) {
                ok = false;
                return QString("分片%1重试多次失败").arg(finishJsons[j]["part_index"].toInt());
            }
            //QString finishResp = PostSync(finishUrl, finishJsons[j], QString(), 30000);
        }
        //AppendEventLog("分片上传完成 通知服务器："+QString::number(futures.size()));
        for (int j = 0; j < futures.size(); ++j) {

            QString finishResp = PostSync(finishUrl, finishJsons[j], QString(), 30000);

        }


        /*
        int totalFinish = finishJsons.size();
        int finishedCount = 0;
        bool hasError = false;
        QMutex mutex; // 保护计数器和错误标志（若回调在非主线程）
        QEventLoop loop;

        for (int j = 0; j < totalFinish; ++j) {
            QJsonObject finishJson = finishJsons[j]; // 拷贝一份，避免引用失效
            doWork(2000);

            PostAsync(finishUrl, finishJson, QString(), 30000,
                      [&, j](const QString& response, QNetworkReply::NetworkError error) {
                          // 回调可能在任意线程，必须加锁
                          QMutexLocker locker(&mutex);
                          finishedCount++;
                          if (error != QNetworkReply::NoError || response.isEmpty()) {
                              hasError = true;
                              AppendEventLog("分片" + QString::number(j) + "完成请求失败:" + response);

                          }
                          // 如果全部完成，退出事件循环
                          if (finishedCount == totalFinish) {
                              loop.quit();
                          }
                      });
        }

        // 等待所有完成请求结束
        loop.exec();

        if (hasError) {
            ok = false;
            return QString("部分分片完成请求失败");
        }
        */
        //AppendEventLog("所有分片上传并完成 耗时："+QString::number(times.elapsed()));



    }
    // 7. 完成上传，请求 /files
    // 服务端处理可能返回「富媒体文件上传超时」——数据已在 COS，重复提交同一请求
    // 服务端会重新处理，无需重新上传。
    QJsonObject filesJson;
    filesJson["upload_id"] = upload_id;

    QString filesUrl = get_url(targetType, openid, "files");
    QString filesResp;
    for (int attempt = 0; attempt < 10; ++attempt) {

        filesResp = PostSync(filesUrl, filesJson,QString(), 30000);
        if (filesResp.isEmpty()) return QString();

        QJsonDocument filesRespDoc = QJsonDocument::fromJson(filesResp.toUtf8());
        if (filesRespDoc.isNull()) return QString();
        QJsonObject filesObj = filesRespDoc.object();
        QString file_info = filesObj["file_info"].toString();

        if (!file_info.isEmpty()) {
            outurl = filesObj["raw_url"].toString();

            // 获取过期时间（秒为单位）
            expireTime = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
            ok=true;
            return file_info;
        }
        if (filesObj["message"].toString() != "富媒体文件上传超时") return filesResp; // 其他错误

    }
    return filesResp; // 重试耗尽，返回最后的错误
}

QString QQBotClient::uploadSmallVideo(int targetType, const QString& openid,
                                      const QByteArray& data, const QString& filename,
                                      qint64& expireTime, const QString& md5,
                                      const QString& sha1, const QString& md5_10m,
                                      bool& ok, QString& outurl)
{
    ok = false;
    outurl.clear();
    expireTime = 0;
    const qint64 fileSize = data.size();

    // 1. prepare：file_size 固定申请 1K（返回 1 个分片 + put 链接）
    QJsonObject prepJson;
    prepJson["file_type"] = 2;
    prepJson["file_name"] = filename;
    prepJson["file_size"] = (qint64)1024;
    prepJson["md5"]     = md5;
    prepJson["sha1"]    = sha1;
    prepJson["md5_10m"] = md5_10m;
    QString response = PostSync(get_url(targetType, openid, "upload_prepare"), prepJson, QString(), 30000);
    if (response.isEmpty()) return QString();
    QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
    if (respDoc.isNull()) return QString();
    QJsonObject respObj = respDoc.object();
    QString upload_id = respObj["upload_id"].toString();
    if (upload_id.isEmpty()) return response;   // 错误信息
    QJsonArray parts = respObj["parts"].toArray();
    if (parts.isEmpty()) return QString("upload_prepare 未返回分片");
    QJsonObject part = parts[0].toObject();
    const int partIndex = part["index"].toInt();
    const QString presignedUrl = part["presigned_url"].toString();
    if (presignedUrl.isEmpty()) return QString("upload_prepare 未返回 presigned_url");

    // 2. 整段视频写入 put 链接
    bool putOk = false;
    int retry = 0;
    int currentTimeout = 30000;
    while (retry < 3 && !putOk) {
        try {
            put(presignedUrl, data, "application/octet-stream", currentTimeout);
            putOk = true;
        } catch (const std::exception &) {
            retry++;
            if (retry < 3) {
                QThread::msleep(1000 * (1 << (retry - 1)));
                currentTimeout += 10000;
            }
        }
    }
    if (!putOk) return QString("put 视频数据失败(重试3次)");

    // 3. 提交（按真实数据）
    QJsonObject finJson;
    finJson["upload_id"]  = upload_id;
    finJson["part_index"] = partIndex;
    finJson["block_size"] = fileSize;
    finJson["md5"]        = md5;
    PostSync(get_url(targetType, openid, "upload_part_finish"), finJson, QString(), 30000);

    // 4. files 注册：超时循环重试
    QJsonObject filesJson;
    filesJson["upload_id"] = upload_id;
    QString filesUrl = get_url(targetType, openid, "files");
    QString filesResp;
    for (int attempt = 0; attempt < 10; ++attempt) {

        filesResp = PostSync(filesUrl, filesJson, QString(), 30000);
        if (filesResp.isEmpty()) return QString();
        QJsonDocument filesDoc = QJsonDocument::fromJson(filesResp.toUtf8());
        if (filesDoc.isNull()) return QString();
        QJsonObject filesObj = filesDoc.object();
        QString file_info = filesObj["file_info"].toString();

        if (!file_info.isEmpty()) {
            outurl = filesObj["raw_url"].toString();
            expireTime = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
            ok = true;
            return file_info;
        }
        if (filesObj["message"].toString() != "富媒体文件上传超时") return filesResp; // 其他错误

    }
    return filesResp; // 重试耗尽，返回最后的错误
}
QPair<int, QString> splitWrappedMsgId(const QString &wrapped) {
    if (wrapped.isEmpty()) return qMakePair(-1, QString());
    int firstBar = wrapped.indexOf('|');
    if (firstBar == -1) return qMakePair(-1, wrapped);
    int secondBar = wrapped.indexOf('|', firstBar + 1);
    if (secondBar == -1) return qMakePair(-1, wrapped);
    bool ok;
    int addr = QStringView(wrapped).mid(firstBar + 1, secondBar - firstBar - 1).toInt(&ok);
    if (!ok) addr = -1;
    QString realMsgId = wrapped.mid(secondBar + 1);
    return qMakePair(addr, realMsgId);
}
QString QQBotClient::send_Media(int type,const QString &openid,const QString &pname,const QString &info,qint64 now_us,
                                const QString &msgid,bool is_wakeup,bool noref, MessageLogContext ctx)
{
    QJsonObject json;
    json["msg_type"] = 7;
    if (info.isEmpty()) return R"({"msg":"要发送的富媒体标签码为空"})";
    QString info2=extractBetween(info,"path=",",");
    if (info2.isEmpty()) return R"({"msg":"无法从path获取info"})";
    QJsonObject refObj;
    refObj["file_info"] = info2;
    json["media"] = refObj;
    json["noref"] = noref;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    ctx.index=index;
    int seq_index=0;
    bool ok=false;
    if(index>=0){

        g_logdb[type+1]->setBuffer_250(index,ok);
    }
    if(ok)
        seq_index = 1;
    else if(noref) return "{}";
    else seq_index = 2;
    initjgt(json, QJsonArray(),"",realMsgId,is_wakeup,seq_index);
    QString url= get_url(type,openid,"messages");
    if(ctx.openid.isEmpty()){
        QString response= PostSync(url, json,QString(), 5000);

        addmsglog(response,index,pname,info,now_us,type,openid);

        return response;

    }
    QList<CosPutPoolEntry> cosPending = takeCosPutPending();   // 异步：pending 转交回调，HTTP 真正完成后才回池
    PostAsync(url, json, "", 5000,
              [this, ctx, cosPending](const QString &resp, QNetworkReply::NetworkError err) {
                  flushCosPutList(cosPending);   // 响应已回 = 服务器已收下消息，此时覆盖 COS 才安全
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
                  if(ctx.cb)
                      ctx.cb(resp,err);
              });
    return "{}";
}


QString QQBotClient::send_messages(int type, const QString &openid, const QString &text, const QString &info,
                                   const QJsonArray &prompt_keyboard, const QString &message_reference, const QString &msgid,
                                   bool is_wakeup, int seq_index, const MessageLogContext ctx,bool noref)
{
    QJsonObject json;
    if(info.isEmpty())
    {
        json["msg_type"] = 0;
    }else{
        json["msg_type"] = 7;
        json["media"] =QJsonObject{{"file_info",info}};
    }
    json["noref"] = noref;
    json["content"] = text;
    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup,seq_index);
    QString url = get_url(type, openid, "messages");
    if(ctx.openid.isEmpty()) return PostSync(url, json,QString(), 5000);
    QList<CosPutPoolEntry> cosPending = takeCosPutPending();   // 异步：pending 转交回调，HTTP 真正完成后才回池
    PostAsync(url, json, "", 5000,
              [this, ctx, cosPending](const QString &resp, QNetworkReply::NetworkError err) {
                  flushCosPutList(cosPending);   // 响应已回 = 服务器已收下消息，此时覆盖 COS 才安全
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
                  if(ctx.cb) ctx.cb(resp,err);
              });
    return QString();
}


QString QQBotClient::send_messages_ark(int type, const QString &openid,const QString &pname,
                                       const QJsonObject &ark, const QString &msgid,
                                       bool is_wakeup, int seq_index,const MessageLogContext ctx)
{
    QJsonArray prompt_keyboard;
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    auto [index, realMsgId] = splitWrappedMsgId(msgid);

    QJsonObject json;
    json["msg_type"] = 3;
    json["ark"] = ark;

    initjgt(json, prompt_keyboard, "", realMsgId, is_wakeup, seq_index);
    QString url = get_url(type, openid, "messages");

    if (!ctx.openid.isEmpty())
    {
        QString pnameCopy = pname;                     // 引用转为拷贝
        QString jsonString = QJsonDocument(ark).toJson(QJsonDocument::Compact);
        int indexCopy = index;
        qint64 now_us_copy = now_us;
        int typeCopy = type;
        QString openidCopy = openid;
        PostAsync(url, json, "", 5000,
                  [this, pnameCopy, jsonString, indexCopy, now_us_copy,
                   typeCopy, openidCopy,cb = ctx.cb]
                  (const QString &resp, QNetworkReply::NetworkError err) {
                      // 如果担心 this 被销毁，可以用 QPointer 检查（可选）
                      addmsglog(resp, indexCopy, pnameCopy, jsonString,
                                now_us_copy, typeCopy, openidCopy);
                      if(cb) cb(resp,err);
                  });
        return QString();   // 立即返回，结果通过回调处理
    }
    else
    {
        QString response = PostSync(url, json, QString(), 5000);
        QJsonDocument doc(ark);
        QString jsonString = doc.toJson(QJsonDocument::Compact);
        addmsglog(response, index, pname, jsonString, now_us, type, openid);
        return response;
    }
}

QString QQBotClient::send_messages_markdown(int type, const QString &openid,const QString &markdown,const QJsonArray &prompt_keyboard,
                                            const QJsonValue &keyboard,const QString &message_reference,
                                            const QString &msgid,bool is_wakeup,int seq_index,const MessageLogContext ctx,bool noref)
{
    QJsonObject json;
    json["msg_type"] = 2;
    json["markdown"] = QJsonObject{{"content", markdown}};
    json["noref"] = noref;
    if (keyboard.isArray()) {
        QJsonArray arr = keyboard.toArray();
        // 根据你的完整示例，标准格式是 {"content":{"rows": arr}}
        json["keyboard"] = QJsonObject{
            {"content", QJsonObject{{"rows", arr}}}
        };
    }
    // 如果传入的是对象，保留你原来的判断逻辑
    else if (keyboard.isObject()) {
        QJsonObject obj = keyboard.toObject();
        if (obj.contains("keyboard")) {
            json["keyboard"] = obj["keyboard"];
        } else if (obj.contains("content")) {
            json["keyboard"] = obj;
        } else if (obj.contains("rows")) {
            json["keyboard"] = QJsonObject{{"content", obj}};
        } else if (obj.contains("buttons")) {
            json["keyboard"] = QJsonObject{
                {"content", QJsonObject{{"rows", QJsonArray() << obj}}}
            };
        } else {
            // 兜底：默认忽略或按原样赋值
            json["keyboard"] = obj;
        }
    }

    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup,seq_index);
    QString url= get_url(type,openid,"messages");

    if(ctx.openid.isEmpty()) return PostSync(url, json,QString(), 5000);
    QList<CosPutPoolEntry> cosPending = takeCosPutPending();   // 异步：pending 转交回调，HTTP 真正完成后才回池
    PostAsync(url, json, "", 5000,
              [this, ctx, cosPending](const QString &resp, QNetworkReply::NetworkError err) {
                  flushCosPutList(cosPending);   // 响应已回 = 服务器已收下消息，此时覆盖 COS 才安全
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
                  if(ctx.cb) ctx.cb(resp,err);
              });
    return QString();
}
QString QQBotClient::send_messages_pd(const QString &url,const QString &msgId, const QString &content, const QString &imagePath,
                                      const QString &message_reference, int seq_index,const MessageLogContext ctx,bool noref)
{
    QByteArray postData;
    QString headers;
    bool useJson = imagePath.isEmpty() || imagePath.startsWith("http", Qt::CaseInsensitive);

    if (useJson) {
        QJsonObject obj;
        if (!imagePath.isEmpty() && imagePath.startsWith("http")) {
            obj["image"] = imagePath;
        }
        if (!content.isEmpty()) {
            obj["content"] = content;
        }
        obj["noref"] = noref;
        if (msgId.contains("INTERACTION") || msgId.contains("FRIEND_ADD") || msgId.contains("GROUP_MEMBER")) //GROUP_MEMBER_ADD
            obj["event_id"] = msgId;
        else
            obj["msg_id"] = msgId;

        if (!message_reference.isEmpty()) {
            QJsonObject refObj;
            refObj["message_id"] = message_reference;
            refObj["ignore_get_message_error"] = false;
            obj["message_reference"] = refObj;
        }
        headers = "application/json";
        postData = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    } else {
        QString boundary = QString("----WebKitFormBoundary%1")
        .arg(QString::number(QRandomGenerator::global()->generate(), 16));
        QByteArray body;
        if (!content.isEmpty()) {
            QByteArray contentData = content.toUtf8();
            body += "--" + boundary.toUtf8() + "\r\n";
            body += "Content-Disposition: form-data; name=\"content\"\r\n";
            body += "Content-Length: " + QByteArray::number(contentData.size()) + "\r\n";
            body += "\r\n";
            body += contentData + "\r\n";
        }
        if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
            QFile file(imagePath);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray imageData = file.readAll();
                file.close();
                body += "--" + boundary.toUtf8() + "\r\n";
                body += "Content-Disposition: form-data; name=\"file_image\"; filename=\"image.jpeg\"\r\n";
                body += "Content-Type: image/jpeg\r\n";
                body += "\r\n";
                body += imageData + "\r\n";
            }
        }
        QString idFieldName;
        if (msgId.contains("INTERACTION") || msgId.contains("FRIEND_ADD") || msgId.contains("GROUP_MEMBER"))
            idFieldName = "event_id";
        else
            idFieldName = "msg_id";

        QByteArray idData = msgId.toUtf8();
        body += "--" + boundary.toUtf8() + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + idFieldName.toUtf8() + "\"\r\n";
        body += "Content-Length: " + QByteArray::number(idData.size()) + "\r\n";
        body += "\r\n";
        body += idData + "\r\n";
        body += "--" + boundary.toUtf8() + "--\r\n";
        headers = QString("multipart/form-data; boundary=%1").arg(boundary);
        postData = body;
    }

    if (ctx.openid.isEmpty()) {
        return PostSync(url, postData, headers, 10000);
    } else {
        QHash<QString, QString> headers2;
        headers2.insert("X-Union-Appid", m_info->appid);
        headers2.insert("Authorization", "QQBot " + m_accessToken);
        headers2.insert("Content-Type", headers);

        postRawAsync(url, postData, headers2, 20000,
                     [this, ctx](const QString &resp, QNetworkReply::NetworkError err) {
                         addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                                   ctx.now_us, ctx.type, ctx.openid);
                     });
        return QString();
    }
}

QString QQBotClient::send_messages_mb(int type, const QString &openid,const QString &markdown,const QJsonArray &prompt_keyboard,
                                      const QJsonValue  &keyboard,const QString &message_reference,
                                      const QString &msgid,bool is_wakeup, int seq_index,const MessageLogContext ctx,bool noref)
{
    QJsonObject json;
    json["msg_type"] = 2;
    QJsonParseError err;
    QJsonDocument dom =QJsonDocument::fromJson(markdown.toUtf8(),&err);
    if(err.error !=QJsonParseError::NoError)
    {
        return QString();
    }

    json["markdown"] = dom.object();
    json["noref"] = noref;
    if (keyboard.isArray()) {
        QJsonArray arr = keyboard.toArray();
        // 根据你的完整示例，标准格式是 {"content":{"rows": arr}}
        json["keyboard"] = QJsonObject{
            {"content", QJsonObject{{"rows", arr}}}
        };
    }
    // 如果传入的是对象，保留你原来的判断逻辑
    else if (keyboard.isObject()) {
        QJsonObject obj = keyboard.toObject();
        if (obj.contains("keyboard")) {
            json["keyboard"] = obj["keyboard"];
        } else if (obj.contains("content")) {
            json["keyboard"] = obj;
        } else if (obj.contains("rows")) {
            json["keyboard"] = QJsonObject{{"content", obj}};
        } else if (obj.contains("buttons")) {
            json["keyboard"] = QJsonObject{
                {"content", QJsonObject{{"rows", QJsonArray() << obj}}}
            };
        } else {
            // 兜底：默认忽略或按原样赋值
            json["keyboard"] = obj;
        }
    }

    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup,seq_index);
    QString url= get_url(type,openid,"messages");
    if(ctx.openid.isEmpty()) return PostSync(url, json,QString(), 5000);
    QList<CosPutPoolEntry> cosPending = takeCosPutPending();   // 异步：pending 转交回调，HTTP 真正完成后才回池
    PostAsync(url, json, "", 5000,
              [this, ctx, cosPending](const QString &resp, QNetworkReply::NetworkError err) {
                  flushCosPutList(cosPending);   // 响应已回 = 服务器已收下消息，此时覆盖 COS 才安全
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
                  if(ctx.cb) ctx.cb(resp,err);

              });
    return QString();
}


QString QQBotClient::delete_messages(int type, const QString &openid, const QString &msgid,Callback callbacks)
{
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    QString url = get_url(type, openid, "messages", realMsgId);
    return Delete(url,QJsonObject(),QString(),10000,callbacks);
}
// 生成邀请链接
QString QQBotClient::generate_share_link(const QString& callback_data,Callback callbacks)
{
    QJsonObject json;
    if (!callback_data.isEmpty()) {
        QByteArray utf8Data = callback_data.toUtf8();
        if (utf8Data.size() > 32) {
            utf8Data = utf8Data.left(32);   // 截断到32字节
        }
        json["callback_data"] = QString::fromUtf8(utf8Data);
    }else{
        json["callback_data"] = m_info->appid;
    }
    return Post("https://api.bot.qq.com/v2/generate_url_link", json,QString(), 5000,callbacks);
}

//获取 群成员列表 频道成员列表
QString QQBotClient::get_members_list(const QString& group,const QString &cursor,Callback callbacks)
{
    QString url= get_url(0,group,"members?cursor=",cursor);
    return Get(url,"", 10000,callbacks);
}


QString QQBotClient::get_groups_list(const QString & cursor,Callback callbacks)
{
    QString url="https://api.bot.qq.com/users/@me/groups?cursor="+cursor;
    return Get(url,"", 10000,callbacks);
}
QString QQBotClient::get_users_list(const QString & cursor,Callback callbacks)
{

    QString url="https://api.bot.qq.com/users/@me/users?cursor="+cursor;
    return Get(url,"", 10000,callbacks);
}
QString QQBotClient::get_groups_members(const QString& group,const QString &user,Callback callbacks)
{
    return Get(get_url(0,group,"members",user),QString(), 10000,callbacks);
}

//回应回调
QString QQBotClient::respond_interaction(const QString &interaction_id, int code, const QString &data, Callback callbacks)
{
    QString url = "https://api.bot.qq.com/interactions/" + interaction_id;

    QJsonObject json;
    json["code"] = code;
    if (!data.isEmpty()) {
        json["data"] = data;
    }
    QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    // 传了回调 → 走 putAsync（回调式，不阻塞调用线程）；没传 → 保持原样同步 + try/catch
    if (callbacks)
        return put2(url, body, QString(), 5000, callbacks);
    try {
        return put(url,body,QString(),5000);
    } catch (const std::exception &e) {
        return e.what();  // 失败返回空字符串
    }

}
QString QQBotClient::get_groups_info(const QString& group,Callback callbacks)
{
    return Get(get_url(0,group,"info"),QString(), 10000,callbacks);
}
QString QQBotClient::get_groups_bot_state(const QString& group,Callback callbacks)
{
    return Get(get_url(0,group,"bot_state"),QString(), 10000,callbacks);
}
QString QQBotClient::del_members (const QString& group,const QString &user_list,bool add_blacklist,Callback callbacks)
{

    QString url = get_url(0,group,"batch_remove_members");
    QJsonObject obj;
    QStringList list = user_list.split(",");
    obj["member_openids"] = QJsonArray::fromStringList(list);
    obj["add_to_member_blacklist"]=add_blacklist;
    return Post(url,obj,QString(),300000,callbacks);

}

QString QQBotClient::get_member_blacklist (const QString& group,const QString &cursor,Callback callbacks)
{

    QString url  = get_url(0,group,"member_blacklist?limit=100&cursor=",cursor);
    return Get(url,QString(),300000,callbacks);
}
QString QQBotClient::member_blacklist (const QString& group,const QString &user_list,bool op,Callback callbacks)
{

    QString url = get_url(0,group,"batch_remove_members");
    QJsonObject obj;
    QStringList list = user_list.split(",");
    obj["member_openids"] = QJsonArray::fromStringList(list);
    obj["op"]=op;
    return Post(url,obj,QString(),300000,callbacks);

}

QString QQBotClient::approveGroupJoinRequest(const QString& group,const QString& user, bool op,const QString& joinRequestId,
                                             const QString& rejectReason,bool addToBlacklist,Callback callbacks)
{
    // 1. 构造 URL（替换路径参数）
    if(joinRequestId.isEmpty())
    {
        QString result=R"({"message":"joinRequestId 为空"})";
        if(callbacks)
            callbacks(result,QNetworkReply::NetworkError());
        return result;
    }
    QString url =get_url(0,group,"approval_join_request",user);

    // 2. 构建请求体 JSON
    QJsonObject requestBody;
    requestBody["op"] = op? "approve" : "decline";

    // 可选字段：只在有值时添加
    if (!joinRequestId.isEmpty()) {
        requestBody["join_request_id"] = joinRequestId;
    }
    if(!op){
        if (!rejectReason.isEmpty()) {
            requestBody["reject_reason"] = rejectReason;
        }
        requestBody["add_to_member_blacklist"] = addToBlacklist;
    }

    return Post(url, requestBody, QString(), 10000,callbacks);
}
// 在您的 Client 类中新增重载

QString QQBotClient::setGroupRestrictChatSetting(const QString& groupOpenId,const QString& memberOpenId,
                                                 int muteSeconds,Callback callbacks)
{
    // 1. 构造 URL
    QString url =get_url(0,groupOpenId,"restrict_chat_setting");;
    if(muteSeconds<0)
        muteSeconds=30;
    if(muteSeconds>=30*1440*60)
    {
        muteSeconds=30*1440*60-1;
    }
    QJsonObject memberObj;

    memberObj["member_openid"] = memberOpenId;

    if(muteSeconds!=0)
    {
        memberObj["op"] = "add";
        QDateTime expireTime = QDateTime::currentDateTime().addSecs(muteSeconds);
        QString expireStr = expireTime.toString(Qt::ISODate);
        int offsetSecs = expireTime.offsetFromUtc();
        int offsetHours = offsetSecs / 3600;
        int offsetMinutes = qAbs(offsetSecs % 3600) / 60;
        QString timezoneStr = (offsetSecs >= 0) ?
                                  QString("+%1:%2").arg(offsetHours, 2, 10, QChar('0')).arg(offsetMinutes, 2, 10, QChar('0')) :
                                  QString("-%1:%2").arg(-offsetHours, 2, 10, QChar('0')).arg(offsetMinutes, 2, 10, QChar('0'));
        QString rfc3339 = expireStr + timezoneStr;
        memberObj["mute_expire_at"] = rfc3339;
    }else{
        memberObj["op"] = "del";
    }

    QJsonArray membersArray;
    membersArray.append(memberObj);
    QJsonObject requestBody;
    requestBody["members"] = membersArray;


    return Post(url, requestBody, QString(), 10000,callbacks);
}
//设置禁言
QString QQBotClient::setGroupRestrictChatSetting(const QString& group, const QJsonArray& membersJson,Callback callbacks)
{
    QString url = get_url(0,group,"restrict_chat_setting");
    QJsonObject requestBody;
    requestBody["members"] = membersJson;
    return Post (url, requestBody, QString(), 10000,callbacks);
}

//获取加群列表
QString QQBotClient::getjoin_request_list(const QString& group,int limit,const QString &cursor,Callback callbacks)
{
    return Get(get_url(0,group,"join_request_list"), QString(), 10000,callbacks);
}

//获取禁言列表
QString QQBotClient::getGroupRestrictChatSetting(const QString& group,Callback callbacks)
{
    return Get(get_url(0,group,"restrict_chat_setting"), QString(), 10000,callbacks);
}

//设置禁言——频道
QString QQBotClient::set_mute(const QString& group,const QString &user,qint64 mute_seconds)
{

    QString url = QString("https://api.bot.qq.com/guilds/%1/mute").arg(group);
    QJsonObject obj;
    if(mute_seconds>31104000)//判定为时间戳
        obj["mute_end_timestamp"]=mute_seconds;
    else
        obj["mute_seconds"] = mute_seconds;
    if(!user.isEmpty()){
        QStringList list = user.split(",");
        obj["user_ids"] = QJsonArray::fromStringList(list);
    }

    return PatchSync(url,obj,QString(),10000) ;
}


// ==================== 自定义菜单接口 ====================

// 1. 查询菜单 (GET)
QString QQBotClient::getMenu(Callback callbacks)
{
    return Get("https://api.bot.qq.com/v2/menu", QString(), 10000, callbacks);
}

// 2. 创建/更新菜单 (POST)
QString QQBotClient::updateMenu(const QJsonObject& menuData, Callback callbacks)
{

    return put2("https://api.bot.qq.com/v2/menu", QJsonDocument(menuData).toJson(QJsonDocument::Compact), QString(), 10000, callbacks);
}

// ==================== 指令面板接口 ====================

// 4. 创建面板 (POST)
QString QQBotClient::createPanel(const QJsonObject& panelData, Callback callbacks)
{
    //qDebug() << panelData;
    return Post("https://api.bot.qq.com/v2/panels", panelData, QString(), 10000, callbacks);
}

// 5. 查询面板列表 (GET)
QString QQBotClient::listPanels(const QString& scope, int limit, const QString& cursor, Callback callbacks)
{
    QString url = "https://api.bot.qq.com/v2/panels?scope=" + scope;
    if (limit > 0) url += "&limit=" + QString::number(limit);
    if (!cursor.isEmpty()) url += "&cursor=" + cursor;
    return Get(url, QString(), 10000, callbacks);
}

// 6. 查询面板详情 (GET)
QString QQBotClient::getPanel(const QString& panelId, Callback callbacks)
{
    return Get("https://api.bot.qq.com/v2/panels/" + panelId, QString(), 10000, callbacks);
}

// 7. 修改面板 (PATCH)
QString QQBotClient::updatePanel(const QString& panelId, const QJsonObject& panelData, Callback callbacks)
{
    //qDebug() << panelData;
    return put2("https://api.bot.qq.com/v2/panels/" + panelId, QJsonDocument(panelData).toJson(QJsonDocument::Compact), QString(), 10000, callbacks);
}

// 8. 删除面板 (DELETE)
QString QQBotClient::deletePanel(const QString& panelId, Callback callbacks)
{
    return Delete("https://api.bot.qq.com/v2/panels/" + panelId,QJsonObject() ,QString(), 10000, callbacks);
}

// 9. 修改面板关联对象 (PATCH)
QString QQBotClient::updatePanelTarget(const QString& panelId, const QJsonObject& targetData, Callback callbacks)
{
    return put2("https://api.bot.qq.com/v2/panels/" + panelId + "/target", QJsonDocument(targetData).toJson(QJsonDocument::Compact), QString(), 10000, callbacks);
}
