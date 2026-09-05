#ifndef APIPROCESSOR_H
#define APIPROCESSOR_H

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>
#include <QNetworkReply>
#include <functional>
struct MarkInfo {
    QString method;
    QString url;
    QByteArray body;
    QStringList paths;
    int startPos;
    int endPos;
};
// ---------- 异步处理器 ----------
class AsyncApiProcessor : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QString& result)>;


    AsyncApiProcessor(const QString &text, Callback callback, QObject *parent = nullptr);
    void start();   // 开始异步处理

private slots:
    void processNextMark();

private:


    QString m_originalText;
    Callback m_callback;
    QList<MarkInfo> m_marks;
    int m_currentIndex = 0;
    QList<QString> m_markReplacements;  // 每个标记对应的替换文本（无路径则替换为响应，有路径则替换为空）
    QList<QString> m_pathValues;        // 所有有路径标记提取的值（按顺序展平）

    void finalize();
    QString buildFinalResult() const;
};


// ---------- 同步处理器 ----------


#endif // APIPROCESSOR_H