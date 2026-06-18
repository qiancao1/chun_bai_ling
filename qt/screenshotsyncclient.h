#ifndef SCREENSHOTSYNCCLIENT_H
#define SCREENSHOTSYNCCLIENT_H

#include <QObject>
#include <QPixmap>

class ScreenshotSyncClient : public QObject
{
    Q_OBJECT
public:
    explicit ScreenshotSyncClient(QObject *parent = nullptr);

    // 同步截图 HTML，返回 QPixmap，如果失败则返回空
    QByteArray captureHtmlSync(const QString &html, int width = 800, int height = 600, int timeoutMs = 30000);
    // 同步截图 URL
    QByteArray captureUrlSync(const QString &url, int width = 800, int height = 600, int timeoutMs = 30000);

private:
    QString m_serverUrl;
};

#endif // SCREENSHOTSYNCCLIENT_H