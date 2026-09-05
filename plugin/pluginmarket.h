#ifndef PLUGINMARKET_H
#define PLUGINMARKET_H

#include "global.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>

#include <QString>
#include <QList>
#include "netmanager.h"


/**
 * @brief parsePluginListFromJson 从 JSON 字节数据解析插件列表
 * @param jsonData 原始 JSON 数据（含可能的 BOM）
 * @param outList 输出：解析后的插件列表
 * @param errorMsg 输出：错误信息
 * @return 是否解析成功
 */
bool parsePluginListFromJson(const QByteArray& jsonData,
                             QList<PluginInfo2>& outList,
                             QString& errorMsg)
{
    // 移除 UTF-8 BOM
    QByteArray data = jsonData;
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data.remove(0, 3);
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        errorMsg = QString("JSON 解析失败，偏移 %1：%2")
                       .arg(parseError.offset)
                       .arg(parseError.errorString());
        return false;
    }

    QJsonObject root = doc.object();
    int code = root["code"].toInt();
    if (code != 0) {
        errorMsg = root["message"].toString("未知接口错误");
        return false;
    }

    QJsonArray list = root["data"].toObject()["list"].toArray();
    outList.clear();

    for (const QJsonValue& val : list) {
        QJsonObject item = val.toObject();
        PluginInfo2 info;
        info.id          = item["id"].toString();
        info.name        = item["name"].toString();
        info.iconPath    = item["icon"].toString();
        info.remark      = item["remark"].toString();
        info.detailUrl   = item["homepage"].toString();
        info.author      = item["author"].toString();
        info.versionCode = item["versionCode"].toInt();
        info.versionName = item["versionName"].toString();
        info.downloadUrl = item["downloadUrl"].toString();
        info.type        = item["type"].toString();

        // 兜底
        if (info.id.isEmpty()) info.id = info.name;
        if (info.type.isEmpty()) info.type = "未知";

        const QJsonArray tags = item["tags"].toArray();
        for (const QJsonValue& tag : tags) {
            info.tags << tag.toString();
        }

        outList.append(info);
    }

    return true;
}


bool fetchPluginListFromUrl(const QString& url,QList<PluginInfo2>& outList,
                            QString& errorMsg,int timeoutMs = 10000)
{
    QHash<QString ,QString> h;
    h["Referer"]="https://gitee.com/";
    h["Accept"]="application/json, text/plain, */*";
    h["Accept-Language"]="zh-CN,zh;q=0.9";
    h["User-Agent"]="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36";
    auto f =  NetManager::instance()->get(url,h,timeoutMs);
    QByteArray data = f.get();
    if (!parsePluginListFromJson(data, outList, errorMsg)) {
        return false;
    }
    return true;
}


bool updateGlobalPluginList(QString& errorMsg)
{
    const QString defaultUrl = "https://gitee.com/linglan2/pure-white-bell--plugin-sdk/raw/master/PluginList.json";
    return fetchPluginListFromUrl(defaultUrl, m_allPlugins, errorMsg);
}



#endif // PLUGINMARKET_H
