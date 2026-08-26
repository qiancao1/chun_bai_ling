#include "apiprocessor.h"
#include "netmanager.h"   // 假设您已有 NetManager 单例，包含 getAsync/postAsync
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QEventLoop>
#include <QTimer>

// ---------- 辅助：从 JSON 中按路径提取值 ----------
static QString extractJsonValue(const QJsonDocument &doc, const QString &path)
{
    const QStringList parts = path.split(QRegularExpression(R"([./])"), Qt::SkipEmptyParts);
    QJsonValue current = doc.object(); // 假设根为对象
    for (const QString &part : parts) {
        if (current.isObject()) {
            current = current.toObject().value(part);
        } else if (current.isArray()) {
            bool ok;
            int index = part.toInt(&ok);
            if (ok && index >= 0 && index < current.toArray().size()) {
                current = current.toArray().at(index);
            } else {
                return QString();
            }
        } else {
            return QString();
        }
        if (current.isUndefined() || current.isNull())
            return QString();
    }
    // 转换为字符串
    if (current.isString())
        return current.toString();
    else if (current.isDouble())
        return QString::number(current.toDouble());
    else if (current.isBool())
        return current.toBool() ? "true" : "false";
    else if (current.isObject() || current.isArray())
        return QString::fromUtf8(QJsonDocument(current.toVariant().toJsonObject()).toJson(QJsonDocument::Compact));
    else
        return QString();
}

// ---------- 辅助：从参数字符串中提取 data 和路径 ----------
static void parseMarkParams(const QString &paramsStr, QByteArray &outBody, QStringList &outPaths)
{
    QString work = paramsStr.trimmed();
    if (work.isEmpty())
        return;

    // 1. 尝试提取 #data#... #data#（优先级高）
    QRegularExpression dataBlockRegex(R"(#data#(.*?)#data#)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch blockMatch = dataBlockRegex.match(work);
    if (blockMatch.hasMatch()) {
        outBody = blockMatch.captured(1).toUtf8();
        work.remove(blockMatch.capturedStart(), blockMatch.capturedLength());
        work = work.trimmed();
    } else {
        // 2. 尝试 data=xxx （直到下一个空格或结尾）
        QRegularExpression dataEqRegex(R"(data=([^\s]+))");
        QRegularExpressionMatch eqMatch = dataEqRegex.match(work);
        if (eqMatch.hasMatch()) {
            outBody = eqMatch.captured(1).toUtf8();
            work.remove(eqMatch.capturedStart(), eqMatch.capturedLength());
            work = work.trimmed();
        }
    }

    // 3. 剩余部分按空格拆分为路径
    if (!work.isEmpty()) {
        outPaths = work.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    }
}

// =================== 异步版本实现 ===================
AsyncApiProcessor::AsyncApiProcessor(const QString &text, Callback callback, QObject *parent)
    : QObject(parent), m_originalText(text), m_callback(callback)
{
    // 正则匹配 [get url=...] 或 [post url=...]
    // 注意：url 值不能包含空格或 ']'，所以用 [^\s\]]+
    QRegularExpression apiRegex(R"(\[(get|post)\s+url=([^\s\]]+)(?:\s+([^\]]*))?\])");
    QRegularExpressionMatchIterator it = apiRegex.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        MarkInfo info;
        info.startPos = match.capturedStart();
        info.endPos = match.capturedEnd();
        info.method = match.captured(1).toLower();
        info.url = match.captured(2).trimmed();
        QString params = match.captured(3).trimmed();
        parseMarkParams(params, info.body, info.paths);
        m_marks.append(info);
    }
}

void AsyncApiProcessor::start()
{
    if (m_marks.isEmpty()) {
        if (m_callback)
            m_callback(m_originalText);
        deleteLater();
        return;
    }
    m_currentIndex = 0;
    m_markReplacements.clear();
    m_pathValues.clear();
    processNextMark();
}

void AsyncApiProcessor::processNextMark()
{
    if (m_currentIndex >= m_marks.size()) {
        finalize();
        return;
    }

    const MarkInfo &mark = m_marks[m_currentIndex];
    QHash<QString, QString> headers;
    int timeout = 30000;

    if (mark.method == "post") {
        headers["Content-Type"] = "application/json";
    }

    auto callback = [this, index = m_currentIndex](const QString &response, QNetworkReply::NetworkError err) {
        bool ok = (err == QNetworkReply::NoError);
        const QString errorMsg = "请求api时错误...";
        const MarkInfo &curMark = m_marks[index];

        // 获取响应内容（可能为空）
        QString content = ok ? response : QString();

        // ===== 核心改动：无路径时直接使用响应原文 =====
        if (curMark.paths.isEmpty()) {
            m_markReplacements.append(content.isEmpty() ? errorMsg : content);
            m_currentIndex++;
            processNextMark();
            return;
        }

        // ===== 有路径：必须解析 JSON =====
        if (!ok || content.isEmpty()) {
            // 网络错误或响应为空
            m_markReplacements.append(""); // 标记移除
            for (int i = 0; i < curMark.paths.size(); ++i)
                m_pathValues.append(errorMsg);
            m_currentIndex++;
            processNextMark();
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            // 非 JSON 响应
            m_markReplacements.append("");
            for (int i = 0; i < curMark.paths.size(); ++i)
                m_pathValues.append(errorMsg);
            m_currentIndex++;
            processNextMark();
            return;
        }

        // 成功解析 JSON，提取路径值
        m_markReplacements.append(""); // 标记移除
        for (const QString &path : curMark.paths) {
            QString val = extractJsonValue(doc, path);
            m_pathValues.append(val.isEmpty() ? errorMsg : val);
        }

        m_currentIndex++;
        processNextMark();
    };

    if (mark.method == "get") {
        NetManager::instance()->getAsync(mark.url, headers, timeout, callback);
    } else {
        NetManager::instance()->postAsync(mark.url, mark.body, headers, timeout, callback);
    }
}
void AsyncApiProcessor::finalize()
{
    QString result = buildFinalResult();
    if (m_callback)
        m_callback(result);
    deleteLater();
}

QString AsyncApiProcessor::buildFinalResult() const
{
    QString result;
    int lastPos = 0;
    for (int i = 0; i < m_marks.size(); ++i) {
        const MarkInfo &mark = m_marks[i];
        result += m_originalText.midRef(lastPos, mark.startPos - lastPos);
        result += m_markReplacements[i];
        lastPos = mark.endPos;
    }
    result += m_originalText.midRef(lastPos);

    // 替换占位符 %1, %2, ...
    for (int i = 0; i < m_pathValues.size(); ++i) {
        QString placeholder = QString("%%1").arg(i + 1);
        result.replace(placeholder, m_pathValues[i]);
    }
    return result;
}

// =================== 同步版本实现 ===================
static QString syncRequest(const QString &method, const QString &url, const QByteArray &body,
                           int timeoutMs)
{
    QHash<QString, QString> headers;
    if (method == "post") {
        headers["Content-Type"] = "application/json";
    }
    std::future<QByteArray> f;
    if (method == "get") {
        f = NetManager::instance()->get(url, headers, timeoutMs);
    } else {
        f = NetManager::instance()->post(url, body, headers, timeoutMs);
    }
    return f.get();
}


QString buildFinalResult(const QString &originalText,
                         const QList<MarkInfo> &marks,
                         const QList<QString> &markReplacements,
                         const QList<QString> &pathValues)
{
    QString result;
    int lastPos = 0;
    for (int i = 0; i < marks.size(); ++i) {
        const MarkInfo &mark = marks[i];
        result += originalText.midRef(lastPos, mark.startPos - lastPos);
        result += markReplacements[i];
        lastPos = mark.endPos;
    }
    result += originalText.midRef(lastPos);

    for (int i = 0; i < pathValues.size(); ++i) {
        QString placeholder = QString("%%1").arg(i + 1);
        result.replace(placeholder, pathValues[i]);
    }
    return result;
}
QString processText(const QString &text, int timeoutMs) {
    // 解析标记
    QList<MarkInfo> marks;
    QRegularExpression apiRegex(R"(\[(get|post)\s+url=([^\s\]]+)(?:\s+([^\]]*))?\])");
    QRegularExpressionMatchIterator it = apiRegex.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        MarkInfo info;
        info.startPos = match.capturedStart();
        info.endPos = match.capturedEnd();
        info.method = match.captured(1).toLower();
        info.url = match.captured(2).trimmed();
        QString params = match.captured(3).trimmed();
        parseMarkParams(params, info.body, info.paths);
        marks.append(info);
    }

    if (marks.isEmpty())
        return text;

    QList<QString> markReplacements;
    QList<QString> pathValues;
    const QString errorMsg = "请求api时错误...";

    for (const MarkInfo &mark : marks) {
        QString response = syncRequest(mark.method, mark.url, mark.body, timeoutMs);

        // 没有指定路径 → 直接用响应原文（不管是不是 JSON）
        if (mark.paths.isEmpty()) {
            markReplacements.append(response.isEmpty() ? errorMsg : response);
            continue;
        }

        // 有路径 → 必须解析 JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || response.isEmpty()) {
            markReplacements.append("");
            for (int i = 0; i < mark.paths.size(); ++i)
                pathValues.append(errorMsg);
            continue;
        }

        markReplacements.append("");
        for (const QString &path : mark.paths) {
            QString val = extractJsonValue(doc, path);
            pathValues.append(val.isEmpty() ? errorMsg : val);
        }
    }

    return buildFinalResult(text, marks, markReplacements, pathValues);
}

