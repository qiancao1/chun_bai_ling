#include "chatpage.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QPainter>
#include <QPixmap>
#include <QFileInfo>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QToolTip>
#include <QGroupBox>
#include <QGraphicsDropShadowEffect>
#include <QSet>
#include <QPair>
#include "global.h"
#include <QCache>
#include <qpainterpath.h>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QDir>
#include <QRunnable>
#include <QPointer>
#include <QMetaObject>
#include <QDateTime>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QMessageBox>
static QCache<QString, QPixmap> avatarCache;
static QCache<QString, QStringList> s_wrapCache; //行数缓存
static const int WRAP_CACHE_SIZE = 500;
int 聊天发送模式=0;



static QNetworkAccessManager *getNetworkManager() {
    static QNetworkAccessManager *nam = new QNetworkAccessManager();
    return nam;
}

#include <QTemporaryFile>
#include <QDir>
#include <QEventLoop>
#include <QTimer>

// 记录正在下载的媒体 URL 和对应的临时文件路径
static QMap<QString, QString> mediaDownloadingMap; // url -> tempFilePath
static QSet<QString> mediaDownloadingSet; // 避免重复下载
static bool extractImageInfo(const QString &content, bool &isLocalPath, QString &source) {
    if(!content.contains("[image,")) return false;
    QString tag = extractBetween(content, "path=", ",");
    if (!tag.isEmpty()) {
        isLocalPath = true;
        source = tag;
        return true;
    }
    if(tag.isEmpty()){
        tag = extractBetween(content, "path=", "]");
        if (!tag.isEmpty()) {
            isLocalPath = true;
            source = tag;
            return true;
        }
    }
    // [image,url=xxx]
    tag = extractBetween(content, "url=", ",");
    if (!tag.isEmpty()) {
        isLocalPath = false;
        source = tag;
        return true;
    }
    if(tag.isEmpty()){
        tag = extractBetween(content, "url=", "]");
        if (!tag.isEmpty()) {
            isLocalPath = false;
            source = tag;
            return true;
        }
    }
    return false;
}

// 异步下载媒体文件，下载完成后调用打开函数
static void downloadMediaAndOpen(const QString &url, QObject *receiver, const QString &slotName) {
    if (url.isEmpty()) return;
    if (mediaDownloadingSet.contains(url)) return;

    // 生成临时文件名（保留原始扩展名）
    QUrl u(url);
    QString fileName = u.fileName();
    QString suffix = QFileInfo(fileName).suffix();
    if (suffix.isEmpty()) suffix = "mp4"; // 默认

    QTemporaryFile tempFile(QDir::tempPath() + "/XXXXXX." + suffix);
    tempFile.setAutoRemove(false); // 手动管理删除
    if (!tempFile.open()) {
        qDebug() << "Failed to create temp file";
        return;
    }
    QString tempFilePath = tempFile.fileName();
    tempFile.close();

    mediaDownloadingSet.insert(url);
    mediaDownloadingMap[url] = tempFilePath;

    QNetworkReply *reply = getNetworkManager()->get(QNetworkRequest(QUrl(url)));
    QObject::connect(reply, &QNetworkReply::finished, [reply, url, tempFilePath, receiver, slotName]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QFile file(tempFilePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(data);
                file.close();
                qDebug() << "Media saved to:" << tempFilePath;

                // 通知调用者打开文件
                QMetaObject::invokeMethod(receiver, slotName.toUtf8().constData(),
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, tempFilePath));
            }
        } else {
            qDebug() << "Download failed:" << url << reply->errorString();
        }
        mediaDownloadingSet.remove(url);
        mediaDownloadingMap.remove(url);
        reply->deleteLater();
    });
}
static bool extractMediaInfo(const QString &content, QString &url, int &mediaType) {
    // 视频
    QString tag;
    tag = extractBetween(content, "url=", ",");
    if(tag.isEmpty())
        tag = extractBetween(content, "url=", "]");
    if(tag.isEmpty())
        tag = extractBetween(content, "path=", ",");
    if(tag.isEmpty())
        tag = extractBetween(content, "path=", "]");
    if(tag.isEmpty()) return false ;
    if(content.contains("[video,")){
        url = tag;
        mediaType = 1;
        return true;
    }
    if(content.contains("[audio,")){
        url = tag;
        mediaType = 2;
        return true;
    }
    bool isLocal;
    if (extractImageInfo(content, isLocal, url)) {
        mediaType = 3;
        return true;
    }
    return false;
}

// 记录正在下载的 URL，避免重复
static QSet<QString> &downloadingSet() {
    static QSet<QString> set;
    return set;
}

// 异步下载头像（如果本地不存在）
static void downloadAvatarIfNeeded(int appid,const QString &openid) {
    if (openid.isEmpty()) return;
    QString avatarPath = QString("./avatars/%1.png").arg(openid);
    if (QFile::exists(avatarPath)) return; // 已存在，无需下载

    QString url = QString("https://thirdqq.qlogo.cn/qqapp/%1/%2/640")
                      .arg(appid)
                      .arg(openid);
    if (downloadingSet().contains(url)) return; // 已经在下载中

    downloadingSet().insert(url);
    QNetworkReply *reply = getNetworkManager()->get(QNetworkRequest(QUrl(url)));
    QObject::connect(reply, &QNetworkReply::finished, [reply, avatarPath, url]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QFile file(avatarPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(data);
                file.close();
                qDebug() << "Avatar saved:" << avatarPath;
            }
        } else {
            qDebug() << "Download failed:" << url << reply->errorString();
        }
        downloadingSet().remove(url);
        reply->deleteLater();
    });
}
// 从消息内容中提取图片信息（支持 [image,path=...] 或 [image,url=...] 以及 [p,...] [v,...] 等）

// 图片缓存（用于消息中的图片，避免重复加载）
static QCache<QString, QPixmap> &imageCache() {
    static QCache<QString, QPixmap> cache(50); // 最多50张图片
    return cache;
}

// 异步下载网络图片（与头像下载类似）
static void downloadImageIfNeeded(const QString &url, const QPersistentModelIndex &persistentIndex) {
    if (url.isEmpty()) return;
    if (imageCache().contains(url)) return;
    if (downloadingSet().contains(url)) return;

    downloadingSet().insert(url);
    QNetworkReply *reply = getNetworkManager()->get(QNetworkRequest(QUrl(url)));
    QObject::connect(reply, &QNetworkReply::finished, [reply, url, persistentIndex]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QPixmap pix;
            if (pix.loadFromData(data)) {
                // 限制最大 128x128，保持比例
                if (pix.width() > 128 || pix.height() > 128) {
                    pix = pix.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
                imageCache().insert(url, new QPixmap(pix));
                // 刷新视图
                QAbstractItemModel *model = const_cast<QAbstractItemModel*>(persistentIndex.model());
                if (model)
                    emit const_cast<QAbstractItemModel*>(model)->dataChanged(persistentIndex, persistentIndex);
            }
        }
        downloadingSet().remove(url);
        reply->deleteLater();
    });
}

static QList<Message> readLatestMessagesFromBuffer(RingBuffer<LogEntry>& buffer,
                                                   int maxMessages,
                                                   const QString& contactId,
                                                   int appid2,
                                                   bool checkAppid,
                                                   QString* lastMsgId = nullptr)
{
    QList<Message> result;
    if (buffer.capacity() == 0) return result;
    int head = buffer.totalWritten(); // 需要用 totalWritten()
    if (head == 0) return result;

    int capacity = buffer.capacity();
    int firstLogical = (head > capacity) ? (head - capacity) : 0;
    QVector<Message> temp;
    temp.reserve(maxMessages);

    for (int logical = head - 1; logical >= firstLogical && temp.size() < maxMessages; --logical) {
        const LogEntry& e = buffer.at(logical % capacity);
        if (checkAppid && e.appid != appid2) continue;
        if (e.groupId != contactId) continue;
        if (!e.direction.isEmpty() && temp.size() < maxMessages) {
            if (!e.direction.startsWith("[黑名单]")) {
                Message me;
                me.msg = e.direction;
                me.user = e.user;
                me.timestamp = e.time;
                me.isSelf = true;
                me.hf = e.hf;
                me.ch = e.deleteid;
                temp.append(me);
            }
        }
        if (!e.msg.isEmpty() && temp.size() < maxMessages) {
            Message me;
            me.msg = e.msg;
            me.user = e.user;
            me.timestamp = e.time;
            me.name = e.user_name;
            me.hf = e.hf;
            temp.append(me);
            if (lastMsgId && lastMsgId->isEmpty()) {
                *lastMsgId = e.msgid;
            }
        }
    }
    std::reverse(temp.begin(), temp.end());
    result = temp.toList();
    return result;
}


// ==================== MessageListModel ====================
MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}
void MessageListModel::setMessages(QList<Message> &&msgs)
{
    beginResetModel();
    m_messages = std::move(msgs);
    endResetModel();

    for (const Message &msg : std::as_const(m_messages)) {
        if(msg.user.length()!=32) continue;
        if (!msg.isSelf && !msg.user.isEmpty()) {
            downloadAvatarIfNeeded(chatPage->m_appid,msg.user);
        }
    }
}

void MessageListModel::addMessage(const Message &msg)
{
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(msg);
    endInsertRows();
    if (!msg.isSelf && !msg.user.isEmpty()) {
        if(msg.user.length()!=32) return;
        downloadAvatarIfNeeded(chatPage->m_appid,msg.user);
    }
}
int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_messages.size())
        return QVariant();
    const Message &msg = m_messages[index.row()];
    switch (role) {
    case SenderRole: return msg.user;
    case ContentRole: return msg.msg;
    case IsSelfRole: return msg.isSelf;
    case TimestampRole: return msg.timestamp;
    case name : return msg.name.isEmpty() ? msg.user : msg.name;
    case hf : return msg.hf;
    case ch : return msg.ch;
    default: return QVariant();
    }
}
void MessageListModel::set_ch(const QModelIndex &index)
{
    if (!index.isValid() || index.row() >= m_messages.size())
        return ;
    Message &msg = m_messages[index.row()];
    msg.msg = "[已撤回]"+msg.msg;
    msg.ch="";


}
void MessageListModel::clear()
{
    beginResetModel();
    m_messages.clear();
    endResetModel();
}
QString replaceFileTag();
void BubbleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    // 获取消息数据
    QString sender = index.data(MessageListModel::SenderRole).toString();
    QString name = index.data(MessageListModel::name).toString();
    QString content = index.data(MessageListModel::ContentRole).toString();
    bool isSelf = index.data(MessageListModel::IsSelfRole).toBool();
    QString timestamp = index.data(MessageListModel::TimestampRole).toString();

    // ---------- 提取图片信息 ----------
    bool isLocalPath = true;
    QString imageSource;
    bool hasImage = extractImageInfo(content, isLocalPath, imageSource);
    QString nameA = extractBetween(content,"name=",",");
    if (content.contains("[image,"))
        content = replaceBetweenAll(content, "[image,", "]","");


    if (content.contains("[video,"))
        content = replaceBetweenAll(content, "[video,", "]", "[视频]"+nameA);
    else if (content.contains("[audio,"))
        content = replaceBetweenAll(content, "[audio,", "]", "[语音]"+nameA);
    else if (content.contains("[v,"))
        content = replaceBetweenAll(content, "[v,", "]", "[视频]"+nameA);
    else if (content.contains("[a,"))
        content = replaceBetweenAll(content, "[a,", "]", "[语音]"+nameA);

    else if (content.contains("[file,"))
        content = replaceFileTag(content);
    // ---------- 文本换行计算 ----------
    QFont textFont("Microsoft YaHei", 11);
    QFontMetrics fm(textFont);
    const int maxBubbleWidth = 360;
    QStringList lines;
    if (s_wrapCache.contains(content)) {
        lines = *s_wrapCache[content];
    } else {
        const QStringList paragraphs = content.split('\n');
        for (const QString &para : paragraphs) {
            QString remaining = para;
            while (!remaining.isEmpty()) {
                int lastGood = 0;
                int totalWidth = 0;
                for (int i = 0; i < remaining.length(); ++i) {
                    int charWidth = fm.horizontalAdvance(remaining.at(i));
                    if (totalWidth + charWidth > maxBubbleWidth) break;
                    totalWidth += charWidth;
                    lastGood = i + 1;
                }
                if (lastGood == 0) lastGood = 1;
                lines.append(remaining.left(lastGood));
                remaining = remaining.mid(lastGood);
            }
        }
        s_wrapCache.insert(content, new QStringList(lines), 1);
    }

    int textWidth = 0;
    for (const QString &line : std::as_const(lines)) {
        int w = fm.horizontalAdvance(line);
        if (w > textWidth) textWidth = w;
    }
    textWidth = qMin(qMax(textWidth, 48), maxBubbleWidth);
    int textHeight = lines.size() * fm.height() + 4; // 上下内边距

    // ---------- 图片加载与尺寸计算 ----------
    QPixmap imgPixmap;
    int imageHeight = 0, imageWidth = 0;
    if (hasImage) {
        if (isLocalPath && QFile::exists(imageSource)) {
            if (imageCache().contains(imageSource)) {
                imgPixmap = *imageCache()[imageSource];
            } else {
                imgPixmap.load(imageSource);
                if (!imgPixmap.isNull()) {
                    // 限制最大128x128
                    if (imgPixmap.width() > 128 || imgPixmap.height() > 128)
                        imgPixmap = imgPixmap.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    imageCache().insert(imageSource, new QPixmap(imgPixmap));
                }
            }
        } else if (!isLocalPath) {
            // 网络图片：尝试从缓存获取，否则发起异步下载
            if (imageCache().contains(imageSource)) {
                imgPixmap = *imageCache()[imageSource];
            } else {
                // 异步下载（传入持久索引以便刷新）
                QPersistentModelIndex persistentIndex(index);
                downloadImageIfNeeded(imageSource, persistentIndex);
                // 下载完成前不显示图片
            }
        }
        if (!imgPixmap.isNull()) {
            imageWidth = imgPixmap.width();
            imageHeight = imgPixmap.height() + 8; // 图片+下边距
        } else if (!isLocalPath) {
            // 占位高度（下载中）
            imageHeight = 128 + 8;
        }
    }

    // ---------- 整体尺寸计算 ----------
    QFont nameFont("Microsoft YaHei", 9);
    QFont timeFont("Microsoft YaHei", 8);
    int nameHeight = isSelf ? 0 : QFontMetrics(nameFont).height() + 4;
    int timeHeight = QFontMetrics(timeFont).height();
    int contentHeight = textHeight + imageHeight;
    int bubbleHeight = nameHeight + contentHeight + timeHeight;

    int totalHeight = qMax(bubbleHeight, 30) - (isSelf ? -8 : 16);


    QRect rect = option.rect;
    const int avatarSize = 40;
    const int margin = 20;
    int bubbleWidth = textWidth + 28;
    if (imageWidth > bubbleWidth - 24) // 让图片宽度不超过气泡内边距
        bubbleWidth = imageWidth + 24;
    bubbleWidth = qMin(bubbleWidth, maxBubbleWidth);

    QRect avatarRect;
    QRect bubbleRect;
    const int avatarTopMargin = 4;
    if (isSelf) {
        avatarRect = QRect(rect.right() - avatarSize - margin, rect.top() + avatarTopMargin, avatarSize, avatarSize);
        bubbleRect = QRect(rect.right() - bubbleWidth - avatarSize - margin*2, rect.top(), bubbleWidth, totalHeight);
    } else {
        avatarRect = QRect(rect.left() + margin, rect.top() + avatarTopMargin, avatarSize, avatarSize);
        bubbleRect = QRect(rect.left() + avatarSize + margin*2, rect.top(), bubbleWidth, totalHeight);
    }

    // ---------- 绘制头像（复用原逻辑） ----------
    QString avatarPath = isSelf ? QString("./avatars/%1.png").arg(chatPage->m_appid) : QString("./avatars/%1.png").arg(sender);
    QPixmap avatarPixmap;
    if (avatarCache.contains(avatarPath)) {
        avatarPixmap = *avatarCache[avatarPath];
    } else if (QFile::exists(avatarPath)) {
        QPixmap original(avatarPath);
        if (!original.isNull()) {
            QPixmap scaled = original.scaled(avatarSize, avatarSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = (scaled.width() - avatarSize) / 2;
            int y = (scaled.height() - avatarSize) / 2;
            QPixmap cropped = scaled.copy(x, y, avatarSize, avatarSize);
            QPixmap rounded(avatarSize, avatarSize);
            rounded.fill(Qt::transparent);
            QPainter p(&rounded);
            QPainterPath path;
            path.addEllipse(0, 0, avatarSize, avatarSize);
            p.setClipPath(path);
            p.drawPixmap(0, 0, cropped);
            p.end();
            avatarPixmap = rounded;
            avatarCache.insert(avatarPath, new QPixmap(avatarPixmap), 1);
        }
    }
    if (!avatarPixmap.isNull()) {
        painter->drawPixmap(avatarRect, avatarPixmap);
        painter->setPen(QPen(QColor(200,200,200),1));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(avatarRect);
    } else {
        painter->setPen(Qt::NoPen);
        QColor avatarColor = isSelf ? QColor(255,190,104) : QColor(228,238,214);
        painter->setBrush(avatarColor);
        painter->drawEllipse(avatarRect);
        painter->setPen(Qt::white);
        painter->setFont(QFont("Microsoft YaHei",14,QFont::Bold));
        painter->drawText(avatarRect, Qt::AlignCenter, isSelf ? "我" : name.left(1));
    }

    // 绘制气泡背景
    painter->setBrush(isSelf ? QColor(210,244,184) : QColor(255,255,255));
    painter->setPen(QPen(isSelf ? QColor(210,244,184) : QColor(238,232,226), 1));
    painter->drawRoundedRect(bubbleRect, 12, 12);

    // ---------- 绘制内容 ----------
    painter->save();
    painter->translate(bubbleRect.topLeft() + QPoint(12, 6));
    int yOffset = 0;

    if (!isSelf) {
        painter->setFont(nameFont);
        painter->setPen(QColor(136,136,136));
        painter->drawText(0, 4, name);
        yOffset += nameHeight;
    }

    // 绘制文本
    painter->setFont(textFont);
    painter->setPen(Qt::black);
    int y = 4;
    for (const QString &line : std::as_const(lines)) {
        painter->drawText(0, y + fm.ascent(), line);
        y += fm.height();
    }
    y += 4;

    // 绘制图片（如果有）
    if (!imgPixmap.isNull()) {
        // 居中或左对齐？这里左对齐
        painter->drawPixmap(0, y, imgPixmap);
        y += imgPixmap.height() + 4;
    } else if (hasImage && !isLocalPath && !imageCache().contains(imageSource)) {
        // 占位符：显示“加载中...”
        painter->setFont(QFont("Microsoft YaHei", 9));
        painter->setPen(QColor(150,150,150));
        painter->drawText(0, y + 20, "图片加载中...");
        y += 128 + 4;
    }

    // 绘制时间
    painter->setFont(timeFont);
    painter->setPen(QColor(170,170,170));
    int timeWidth = QFontMetrics(timeFont).horizontalAdvance(timestamp);
    painter->drawText(bubbleWidth - 24 - timeWidth, y + 8, timestamp);

    painter->restore();
}
QSize BubbleDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QString content = index.data(MessageListModel::ContentRole).toString();
    bool isSelf = index.data(MessageListModel::IsSelfRole).toBool();

    // 提取图片与文本
    bool isLocalPath = true;
    QString imageSource;
    bool hasImage = extractImageInfo(content, isLocalPath, imageSource);
    QString nameA = extractBetween(content,"name=",",");
    if (content.contains("[image,"))
        content = replaceBetweenAll(content, "[image,", "]","");
    else if (content.contains("[video,"))
        content = replaceBetweenAll(content, "[video,", "]", "[视频]"+nameA);
    else if (content.contains("[audio,"))
        content = replaceBetweenAll(content, "[audio,", "]", "[语音]"+nameA);
    else if (content.contains("[v,"))
        content = replaceBetweenAll(content, "[v,", "]", "[视频]"+nameA);
    else if (content.contains("[a,"))
        content = replaceBetweenAll(content, "[a,", "]", "[语音]"+nameA);

    else if (content.contains("[file,"))
        content = replaceFileTag(content);


    // 文本换行（复用 paint 中的算法）
    QFont textFont("Microsoft YaHei", 11);
    QFontMetrics fm(textFont);
    const int maxBubbleWidth = 360;
    QStringList lines;
    static QCache<QString, QStringList> wrapCache;
    if (wrapCache.contains(content)) {
        lines = *wrapCache[content];
    } else {
        const QStringList paragraphs = content.split('\n');
        for (const QString &para : paragraphs) {
            QString remaining = para;
            while (!remaining.isEmpty()) {
                int lastGood = 0;
                int totalWidth = 0;
                for (int i = 0; i < remaining.length(); ++i) {
                    int charWidth = fm.horizontalAdvance(remaining.at(i));
                    if (totalWidth + charWidth > maxBubbleWidth) break;
                    totalWidth += charWidth;
                    lastGood = i + 1;
                }
                if (lastGood == 0) lastGood = 1;
                lines.append(remaining.left(lastGood));
                remaining = remaining.mid(lastGood);
            }
        }
        wrapCache.insert(content, new QStringList(lines));
    }
    int textHeight = lines.size() * fm.height() + 4;

    // 图片高度估算
    int imageHeight = 0;
    if (hasImage) {
        if (isLocalPath && QFile::exists(imageSource)) {
            QPixmap tmp(imageSource);
            if (!tmp.isNull()) {
                int w = qMin(tmp.width(), 128);
                int h = tmp.height() * w / tmp.width();
                imageHeight = h + 8;
            } else {
                imageHeight = 128 + 8;
            }
        } else if (!isLocalPath) {
            imageHeight = 128 + 8; // 占位高度
        }
    }

    QFont nameFont("Microsoft YaHei", 9);
    QFont timeFont("Microsoft YaHei", 8);
    int nameHeight = isSelf ? 0 : QFontMetrics(nameFont).height() + 4;
    int timeHeight = QFontMetrics(timeFont).height();
    int contentHeight = textHeight + imageHeight;
    int bubbleHeight = nameHeight + contentHeight + timeHeight;
    int totalHeight = qMax(bubbleHeight, 30);
    if(isSelf) totalHeight+=16;
    return QSize(option.rect.width(), totalHeight);
}

// ==================== 联系人项控件（轻量，无变化）====================
class ContactItemWidget : public QWidget {
public:
    ContactItemWidget(const Contact &contact, QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(4);

        QLabel *avatar = new QLabel();
        avatar->setFixedSize(36, 36);
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setScaledContents(false);

        // 尝试加载本地头像（假设头像文件名为 contact.name 对应 openid）
        QString avatarPath = QString("./avatars/%1.png").arg(contact.id);
        QPixmap avatarPix;
        if (QFile::exists(avatarPath)) {
            QPixmap original(avatarPath);
            if (!original.isNull()) {
                // 生成圆形图片
                QPixmap rounded(36, 36);
                rounded.fill(Qt::transparent);
                QPainter p(&rounded);
                p.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addEllipse(0, 0, 36, 36);
                p.setClipPath(path);
                QPixmap scaled = original.scaled(36, 36, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                int x = (scaled.width() - 36) / 2;
                int y = (scaled.height() - 36) / 2;
                QPixmap cropped = scaled.copy(x, y, 36, 36);
                p.drawPixmap(0, 0, cropped);
                p.end();
                avatarPix = rounded;
            }
        }

        if (!avatarPix.isNull()) {
            avatar->setPixmap(avatarPix);
            avatar->setStyleSheet("background: transparent;");
            avatar->setText("");
        } else {
            // 回退到彩色文字圆形
            QString color = QString("#%1").arg(qHash(contact.name) % 0x1000000, 6, 16, QChar('0'));
            avatar->setStyleSheet(QString("background-color: %1; border-radius: 18px; color: white; font-size: 20px; font-weight: bold;").arg(color));
            avatar->setText(contact.name.isEmpty() ? "?" : contact.name.left(1));
        }

        // 右侧信息区域（保持不变）
        QWidget *info = new QWidget();
        info->setAttribute(Qt::WA_TranslucentBackground);
        QVBoxLayout *infoLayout = new QVBoxLayout(info);
        infoLayout->setContentsMargins(0,0,0,0);
        infoLayout->setSpacing(2);

        QLabel *nameLabel = new QLabel(contact.name);
        nameLabel->setStyleSheet("background: transparent; font-weight: bold; font-size: 14px;");
        QLabel *timeLabel = new QLabel(contact.lastMsgTime.isEmpty() ? "暂无消息" : contact.lastMsgTime);
        timeLabel->setStyleSheet("background: transparent; color: gray; font-size: 11px;");

        infoLayout->addWidget(nameLabel);
        infoLayout->addWidget(timeLabel);
        infoLayout->addStretch();

        layout->addWidget(avatar);
        layout->addWidget(info, 1);
        layout->addStretch();

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
};



// ==================== ChatPage 实现 ====================
ChatPage::ChatPage(QWidget *parent)
    : QWidget(parent), isGroupMode(1)  // 默认群聊模式
{
    initUI();
    QFile file("data/全量群.hash");
    if (file.open(QIODevice::ReadOnly)) {
        QDataStream in(&file);
        in.setVersion(QDataStream::Qt_5_15);
        in >> 全量群;
        file.close();
    }
    QFile file2("data/customGroupNames.hash");
    if (file2.open(QIODevice::ReadOnly)) {
        QDataStream in(&file2);
        in.setVersion(QDataStream::Qt_5_15);
        in >> customGroupNames;
        file2.close();
    }
    QFile file3("data/最近对话.hash");
    if (file3.open(QIODevice::ReadOnly)) {
        QDataStream in(&file3);
        in.setVersion(QDataStream::Qt_5_15);
        in >> 最近对话;
        file3.close();
    }
}

ChatPage::~ChatPage() {}

void ChatPage::initUI()
{
    setObjectName("chatPage");
    setStyleSheet(R"(
        QWidget#chatPage {
            background: #FFF8EF;
        }
        QWidget#sessionPanel, QWidget#rightPanel {
            background: #FFFFFF;
            border: none;
            border-radius: 10px;
        }
        QWidget#inputContainer {
            background: #FFFFFF;
            border: 1px solid #F1ECE6;
            border-radius: 10px;
        }
        QLabel#chatTitle {
            color: #17202A;
            font-size: 17px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#chatSubTitle, QLabel#chatMuted {
            color: #8A94A6;
            font-size: 12px;
            background: transparent;
        }
        QLabel#chatAvatar {
            background: #F4F8EA;
            border-radius: 10px;
            font-size: 20px;
        }
        QPushButton#modeButton {
            background: transparent;
            color: #8A94A6;
            border-radius: 10px;
            min-height: 24px;
            font-weight: 600;
        }
        QPushButton#modeButton:checked {
            background: #FFF0DE;
            color: #FF7F32;
        }
        QPushButton#roundToolButton {
            background: #F6F7F9;
            border: none;
            border-radius: 8px;
            min-width: 28px;
            min-height: 28px;
            color: #8A94A6;
            font-weight: bold;
        }
        QPushButton#roundToolButton:hover {
            background: #FFF0DE;
            color: #FF7F32;
        }
        QListWidget#contactList {
            border: none;
            background: transparent;
            outline: none;
        }
        QListWidget#contactList::item {
            height: 64px;
            border: none;
            margin: 2px 0px;
            border-radius: 10px;
            background: transparent;
        }
        QListWidget#contactList::item:selected {
            background: #FFF0DE;
        }
        QListWidget#contactList::item:hover {
            background: #FFF8EF;
        }
        QListView#messageList {
            background: transparent;
            border: none;
            outline: none;
        }
        QTextEdit#chatInput {
            border: none;
            background: transparent;
            padding: 8px 10px;
            font-size: 14px;
            color: #17202A;
        }
        QPushButton#sendButton {
            background: #FF7F32;
            color: white;
            border: none;
            border-radius: 10px;
            padding: 5px 12px;
            font-weight: 800;
            font-size: 12px;
        }
        QPushButton#sendButton:hover {
            background: #FF7F32;
        }
        QComboBox#sendTypeCombo {
            border: none;
            background: transparent;
            color: #8A94A6;
            border-radius: 8px;
            padding: 4px 8px;
        }
        QComboBox#sendTypeCombo:hover {
            background: #FFF0DE;
            color: #FF7F32;
        }
    )");

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // ========== 左侧会话面板 ==========
    QWidget *leftPanel = new QWidget;
    leftPanel->setObjectName("sessionPanel");
    leftPanel->setFixedWidth(224);
    leftPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto leftShadow = new QGraphicsDropShadowEffect(leftPanel);
    leftShadow->setOffset(0, 2);
    leftShadow->setColor(QColor(0, 0, 0, 15));
    leftShadow->setBlurRadius(10);
    leftPanel->setGraphicsEffect(leftShadow);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    leftLayout->setSpacing(4);

    // 会话标题
    QHBoxLayout *sessionHeader = new QHBoxLayout;
    QLabel *sessionTitle = new QLabel("会话列表");
    sessionTitle->setObjectName("chatTitle");
    QPushButton *addSessionBtn = new QPushButton("+");
    addSessionBtn->setObjectName("roundToolButton");
    addSessionBtn->setFixedSize(28, 28);
    addSessionBtn->hide();
    sessionHeader->addWidget(sessionTitle);
    sessionHeader->addStretch();
    sessionHeader->addWidget(addSessionBtn);
    leftLayout->addLayout(sessionHeader);

    // ========== 模式按钮栏（两行，每行三个） ==========
    QVBoxLayout *btnVerticalLayout = new QVBoxLayout;
    btnVerticalLayout->setSpacing(6);

    // 第一行：全量、群聊、私聊
    QHBoxLayout *row1 = new QHBoxLayout;
    row1->setSpacing(8);
    btnChat = new QPushButton("全量");
    btnGroupChat = new QPushButton("群聊");
    btnPrivateChat = new QPushButton("私聊");

    btnChat->setObjectName("modeButton");
    btnGroupChat->setObjectName("modeButton");
    btnPrivateChat->setObjectName("modeButton");

    btnChat->setCheckable(true);
    btnGroupChat->setCheckable(true);
    btnPrivateChat->setCheckable(true);

    row1->addWidget(btnChat);
    row1->addWidget(btnGroupChat);
    row1->addWidget(btnPrivateChat);

    // 第二行：频道、频道私聊、最近
    QHBoxLayout *row2 = new QHBoxLayout;
    row2->setSpacing(8);
    btnChannelChat = new QPushButton("频道");
    btnChannelPrivate = new QPushButton("频道私聊");
    btnRecentChat = new QPushButton("最近");
    btnChannelChat->setObjectName("modeButton");
    btnChannelPrivate->setObjectName("modeButton");
    btnRecentChat->setObjectName("modeButton");
    btnChannelChat->setCheckable(true);
    btnChannelPrivate->setCheckable(true);
    btnRecentChat->setCheckable(true);




    row2->addWidget(btnChannelChat);
    row2->addWidget(btnChannelPrivate);
    row2->addWidget(btnRecentChat);

    btnVerticalLayout->addLayout(row1);
    btnVerticalLayout->addLayout(row2);
    leftLayout->addLayout(btnVerticalLayout);  // 替换原来的 leftLayout->addLayout(btnLayout)




    // 联系人列表（重要：这里初始化 contactList）
    contactList = new QListWidget;
    contactList->setObjectName("contactList");
    contactList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contactList->setContextMenuPolicy(Qt::CustomContextMenu);

    leftLayout->addWidget(contactList, 1);

    mainLayout->addWidget(leftPanel);

    // ========== 右侧聊天区域 ==========
    QWidget *rightPanel = new QWidget;
    rightPanel->setObjectName("rightPanel");
    rightPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto rightShadow = new QGraphicsDropShadowEffect(rightPanel);
    rightShadow->setOffset(0, 2);
    rightShadow->setColor(QColor(0, 0, 0, 15));
    rightShadow->setBlurRadius(10);
    rightPanel->setGraphicsEffect(rightShadow);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // 聊天头部
    QWidget *chatHeader = new QWidget;
    QHBoxLayout *chatHeaderLayout = new QHBoxLayout(chatHeader);
    chatHeaderLayout->setContentsMargins(4, 4, 4, 4);
    chatHeaderLayout->setSpacing(4);
    QLabel *avatar = new QLabel("-");
    avatar->setObjectName("chatAvatar");
    avatar->setFixedSize(36, 36);
    avatar->setAlignment(Qt::AlignCenter);
    QVBoxLayout *titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(3);
    titleLabel = new QLabel("未选择会话");
    titleLabel->setObjectName("chatTitle");

    titleLayout->addWidget(titleLabel);

    chatHeaderLayout->addWidget(avatar);
    chatHeaderLayout->addLayout(titleLayout, 1);
    rightLayout->addWidget(chatHeader);

    // 消息列表视图
    msgModel = new MessageListModel(this);
    msgListView = new QListView;
    msgListView->setObjectName("messageList");
    msgListView->setModel(msgModel);
    msgListView->setItemDelegate(new BubbleDelegate(this));
    msgListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    msgListView->verticalScrollBar()->setSingleStep(16);  // 数值越小滚动越慢
    msgListView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(msgListView, &QListView::customContextMenuRequested, this, &ChatPage::showMessageContextMenu);
    connect(msgListView, &QListView::doubleClicked, this, &ChatPage::onMessageDoubleClicked);
    rightLayout->addWidget(msgListView, 1);

    // 输入区域
    QWidget *inputOuter = new QWidget;
    QVBoxLayout *inputOuterLayout = new QVBoxLayout(inputOuter);
    inputOuterLayout->setContentsMargins(4, 0, 4, 0);
    inputOuterLayout->setSpacing(0);
    QWidget *inputContainer = new QWidget;
    inputContainer->setObjectName("inputContainer");
    inputContainer->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *containerLayout = new QVBoxLayout(inputContainer);
    containerLayout->setContentsMargins(6, 2, 6, 6);
    containerLayout->setSpacing(2);

    inputEdit = new QTextEdit;
    inputEdit->setObjectName("chatInput");
    inputEdit->setPlaceholderText("输入消息(ctrl+回车键)发送...");
    inputEdit->setMaximumHeight(90);
    containerLayout->addWidget(inputEdit);
    inputEdit->installEventFilter(this);
    // 底部工具栏
    QHBoxLayout *toolLayout = new QHBoxLayout;
    toolLayout->setSpacing(2);

    auto createNavButton = [](const QIcon &icon, const QString &tooltip = "") {
        QPushButton *btn = new QPushButton();
        btn->setIcon(icon);
        btn->setIconSize(QSize(20, 20));
        btn->setCheckable(true);
        btn->setMinimumHeight(20);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(
            "QPushButton { background-color: white; border: none; margin: 0px; padding: 2px; }"
            "QPushButton:hover { background-color: #e0e0e0; }"
            "QPushButton:pressed { background-color: #c0c0c0; }"
            );
        return btn;
    };

    // 请确保图标资源存在，否则可暂时注释或替换为文字按钮
    btnSendImage = createNavButton(QIcon(":/icons/image.png"), "发送图片");
    btnSendAudio = createNavButton(QIcon(":/icons/audio.png"), "发送音频");
    btnSendVideo = createNavButton(QIcon(":/icons/video.png"), "发送视频");
    btnSendFile  = createNavButton(QIcon(":/icons/file.png"), "发送文件");

    comboSendType = new QComboBox;
    comboSendType->setObjectName("sendTypeCombo");
    comboSendType->addItems({"普通文本", "MarkDown"});

    btnSend = new QPushButton("发送");
    btnSend->setObjectName("sendButton");
    btnSend->setFixedHeight(28);

    toolLayout->addWidget(btnSendImage);
    toolLayout->addWidget(btnSendAudio);
    toolLayout->addWidget(btnSendVideo);
    toolLayout->addWidget(btnSendFile);
    toolLayout->addWidget(comboSendType);
    toolLayout->addStretch();
    toolLayout->addWidget(btnSend);
    containerLayout->addLayout(toolLayout);
    inputOuterLayout->addWidget(inputContainer);
    rightLayout->addWidget(inputOuter);
    mainLayout->addWidget(rightPanel, 1);

    // 信号连接
    connect(btnGroupChat, &QPushButton::clicked, this, &ChatPage::onGroupChatClicked);
    connect(btnPrivateChat, &QPushButton::clicked, this, &ChatPage::onPrivateChatClicked);
    connect(btnChat, &QPushButton::clicked, this, &ChatPage::onChatClicked);

    connect(btnChannelChat, &QPushButton::clicked, this, &ChatPage::onChannelChatClicked);
    connect(btnChannelPrivate, &QPushButton::clicked, this, &ChatPage::onChannelPrivateClicked);
    connect(btnRecentChat, &QPushButton::clicked, [this](){
        isGroupMode = 5;
        btnsetChecked();
        btnRecentChat->setChecked(true);
    });

    connect(contactList, &QListWidget::itemClicked, this, &ChatPage::onContactItemClicked);
    connect(contactList, &QListWidget::customContextMenuRequested, this, &ChatPage::showContactListContextMenu);
    connect(btnSend, &QPushButton::clicked, this, &ChatPage::onSendClicked);
    connect(btnSendImage, &QPushButton::clicked, this, &ChatPage::onSendImage);
    connect(btnSendAudio, &QPushButton::clicked, this, &ChatPage::onSendAudio);
    connect(btnSendVideo, &QPushButton::clicked, this, &ChatPage::onSendVideo);
    connect(btnSendFile, &QPushButton::clicked, this, &ChatPage::onSendFile);
    connect(comboSendType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [=](int index){
                聊天发送模式=index;
            });
}



// 打开本地媒体文件（用系统默认播放器）
void ChatPage::openDownloadedMedia(const QString &filePath) {
    if (QFile::exists(filePath)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        // 可选：设定一个定时器，30分钟后删除临时文件
        QTimer::singleShot(30 * 60 * 1000, [filePath]() {
            QFile::remove(filePath);
        });
    }
}

void ChatPage::onMessageDoubleClicked(const QModelIndex &index) {
    if (!index.isValid()) return;

    QString content = index.data(MessageListModel::ContentRole).toString();
    QString mediaUrl;
    int mediaType = -1;
    if (extractMediaInfo(content, mediaUrl, mediaType)) {
        if (mediaType == 3) { // 图片
            QUrl url(mediaUrl);
            if (url.isLocalFile())
                QDesktopServices::openUrl(QUrl::fromLocalFile(mediaUrl));
            else
                QDesktopServices::openUrl(url);
        } else {
            downloadMediaAndOpen(mediaUrl, this, "openDownloadedMedia");
        }
        return;
    }

    // 处理文件消息
    if (content.contains("[file,") || content.contains("[f,")) {
        QString filePath = extractBetween(content, "path=", "]");
        if (!filePath.isEmpty() && QFile::exists(filePath)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
            return;
        }
    }


}
bool ChatPage::eventFilter(QObject *obj, QEvent *event) {
    if (obj == inputEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() == Qt::ControlModifier) {
                onSendClicked();
                return true;
            } else {
                return false;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
void ChatPage::showContactListContextMenu(const QPoint &pos)
{
    QPoint globalPos = contactList->mapToGlobal(pos);
    QMenu menu;
    QListWidgetItem *item = contactList->itemAt(pos);
    if (item) {
        QAction *editAction = menu.addAction("编辑群昵称");
        connect(editAction, &QAction::triggered, this, [this, item]() {
            QString id = item->data(Qt::UserRole + 1).toString();
            bool ok;
            QString newNickname = QInputDialog::getText(this, "编辑群昵称 设置后重新点按钮即可", "请输入新的群昵称:", QLineEdit::Normal, customGroupNames.value(id), &ok);
            if (ok && !newNickname.isEmpty()) {
                customGroupNames.insert(id, newNickname); // 假设 customGroupNames 是 QMap<QString, QString>
                QFile file("data/customGroupNames.hash");
                if (file.open(QIODevice::WriteOnly)) {
                    QDataStream out(&file);
                    out.setVersion(QDataStream::Qt_5_15);
                    out << customGroupNames;
                    file.close();
                }
            }
        });
    }
    menu.exec(globalPos);
}



void ChatPage::btnsetChecked()
{
    btnRecentChat->setChecked(false);
    btnChat->setChecked(false);
    btnGroupChat->setChecked(false);
    btnPrivateChat->setChecked(false);
    btnChannelChat->setChecked(false);
    btnChannelPrivate->setChecked(false);
    msgModel->clear();
    currentContactId.clear();
    int mode=isGroupMode;
    isGroupMode=-1; //重置模式 防止其他代码动这个
    updateAllContactLists(mode);
    isGroupMode = mode;
}

void ChatPage::updateAllContactLists(int index)
{
    int bufferIdx=0;

    contactList->clear();//列表
    seen.clear(); //过滤器
    s_wrapCache.clear(); //聊天记录缓存
    avatarCache.clear(); //头像缓存
    switch (index) {
    case 0: // 全量群
        for (auto it = 全量群.begin(); it != 全量群.end(); ++it) {
            Contact c;
            c.id = it.key();
            c.name = customGroupNames.value(c.id);       // 没有 name，就用 key
            if(c.name.isEmpty()) c.name=it.key();
            c.lastMsgTime = "猜猜我是谁";      // 忽略
            appendContactCard(it.value(), c,0);
        }
        return;
    case 1: bufferIdx=1;break;// 普通群
    case 2: bufferIdx=2;break;// 私聊
    case 3: bufferIdx=3;break;// 频道
    case 4: bufferIdx=4;break;// 频道私聊
    case 5:
        for (auto it = 最近对话.begin(); it != 最近对话.end(); ++it) {
            Contact c;
            c.id = it.key();
            c.name = customGroupNames.value(c.id);       // 没有 name，就用 key
            c.lastMsgTime = "";      // 忽略
            int appid=0,type=0;
            parseFromId(it.value(),appid,type);
            appendContactCard(appid, c,type);
        }
        return;
    default:
        return;
    }
    int type=0;
    int total = m_logStore[bufferIdx].totalWritten();
    if (total <= 0) return;
    const QVector<LogEntry> &entries = m_logStore[bufferIdx].readLatest(total);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const LogEntry& e = *it;
        int appid = e.appid;
        if (m_currentBotIndex != -1) {
            if (appid != m_accounts[m_currentBotIndex]->appid_int) continue;
        }
        QPair<int, QString> key(appid, e.groupId);
        if (seen.contains(key)) continue;
        seen.insert(key);
        Contact c;
        c.id = e.groupId;

        if (bufferIdx == 3 || bufferIdx == 4)
        {
            c.name = e.user_name;
            if (!c.id.isEmpty()) {
                downloadAvatarIfNeeded(appid,c.id);
            }
            c.lastMsgTime = e.msg;
        }else{
            c.name = customGroupNames.value(e.groupId);
            c.lastMsgTime = e.user_name + ": " + e.msg;
        }
        if(c.name.isEmpty())
        {
            c.name=c.id;
        }
        appendContactCard(appid, c,bufferIdx-1);
    }
}
void ChatPage::appendContactCard(int appid,Contact c,int type)
{
    ContactItemWidget *widget = new ContactItemWidget(c);
    QListWidgetItem *item = new QListWidgetItem();
    item->setSizeHint(QSize(contactList->viewport()->width(), 64));
    item->setData(Qt::UserRole,appid);
    item->setData(Qt::UserRole+1,c.id);
    item->setData(Qt::UserRole+2,type);
    contactList->addItem(item);
    contactList->setItemWidget(item, widget);
}
void ChatPage::addContact(int type, int appid, const QString& id,const QString &openid, const QString& name ,const QString &msg,const QString &msgid,const QString &hf)
{
    if (isGroupMode == type || type==0) {
        if (m_currentBotIndex != -1) {
            if (m_appid != appid) return;
        }
        if(id==currentContactId)
        {
            m_msgid = msgid;
            addMessage(Message(openid,msg,false, QDateTime::currentDateTime().toString("hh:mm:ss"),name.isEmpty() ? openid : name,hf,""));
        }
        if(type==0)
        {
            if(全量群.contains(id)) return;
            全量群.insert(id,appid);
            QFile file("data/全量群.hash");
            if (file.open(QIODevice::WriteOnly)) {
                QDataStream out(&file);
                out.setVersion(QDataStream::Qt_5_15);
                out << 全量群;   // 直接序列化整个 QHash
                file.close();
            }
        }else{
            QPair<int, QString> key(appid, id);
            if (seen.contains(key)) return;  // 已存在，不重复添加
            seen.insert(key);
        }
        Contact c;
        c.id = id;                     // 这个 id 将用作头像文件名和 URL 中的 openid


        if(name.isEmpty())
            c.lastMsgTime = msg;
        else
            c.lastMsgTime = name+": "+msg;
        if(type==3){
            c.name = name.isEmpty() ? openid : name;
            if (!c.id.isEmpty()) {
                downloadAvatarIfNeeded(appid,c.id);
            }
        }else
        {
            c.name = customGroupNames.value(id);
        }
        if(c.name.isEmpty())
        {
            c.name=c.id;
        }
        if(type!=0)type--;
        appendContactCard(appid,c,type);

    }
}

void ChatPage::showMessageContextMenu(const QPoint &pos)
{
    QModelIndex index = msgListView->indexAt(pos);
    if (!index.isValid()) return;

    QMenu menu(this);
    QAction *at = menu.addAction("艾特他");
    QAction *hf = menu.addAction("回复");
    QAction *ch = menu.addAction("撤回");

    QAction *copyTextAction = menu.addAction("复制文本");
    QAction *copyAllAction = menu.addAction("复制全部内容");

    QAction *selectedAction = menu.exec(msgListView->viewport()->mapToGlobal(pos));
    if (selectedAction == copyTextAction) {
        QString content = index.data(MessageListModel::ContentRole).toString();
        QApplication::clipboard()->setText(content);
        QToolTip::showText(QCursor::pos(), "已复制文本", this);
    } else if (selectedAction == copyAllAction) {
        QString sender = index.data(MessageListModel::SenderRole).toString();
        QDateTime ts = index.data(MessageListModel::TimestampRole).toDateTime();
        QString content = index.data(MessageListModel::ContentRole).toString();
        QString fullMsg = QString("%1 %2: %3").arg(ts.toString("hh:mm"),sender,content);
        QApplication::clipboard()->setText(fullMsg);
        QToolTip::showText(QCursor::pos(), "已复制完整消息", this);
   } else if (selectedAction == at) {
        QString content = index.data(MessageListModel::SenderRole).toString();
        QString text = inputEdit->toPlainText().trimmed();
        inputEdit->setText("<@"+content+">"+text);

   } else if (selectedAction == hf) {
        QString content = index.data(MessageListModel::hf).toString();
        QString text = inputEdit->toPlainText().trimmed();
        inputEdit->setText(content+text);


   } else if (selectedAction == ch) {
       if (m_botClients.contains(m_appid)) {
            QString ch = index.data(MessageListModel::ch).toString();
            if(ch.isEmpty())
            {
                QMessageBox::warning(this,"撤回失败","撤回失败，撤回id为空 请确定是机器人发送的 或已经撤回过了");
                return;
            }
            QQBotClient *c= m_botClients[m_appid];
            QString res = c->delete_messages(m_type,currentContactId,ch);
            if(!res.contains("mes"))
                msgModel->set_ch(index);
            else
                QMessageBox::warning(this,"撤回失败",res);
       }
   }
}


void ChatPage::onChatClicked()
{
    isGroupMode = 0;
    btnsetChecked();
    btnChat->setChecked(true);
}

void ChatPage::onGroupChatClicked()
{
    isGroupMode = 1;
    btnsetChecked();
    btnGroupChat->setChecked(true);
}

void ChatPage::onPrivateChatClicked()
{
    isGroupMode = 3;
    btnsetChecked();
    btnPrivateChat->setChecked(true);
}

void ChatPage::onChannelChatClicked()
{
    isGroupMode = 2;
    btnsetChecked();
    btnChannelChat->setChecked(true);
}

void ChatPage::onChannelPrivateClicked()
{
    isGroupMode = 4;
    btnsetChecked();
    btnChannelPrivate->setChecked(true);
}


void ChatPage::onContactItemClicked(QListWidgetItem *item)
{
    m_appid = item->data(Qt::UserRole).toInt();
    QString id = item->data(Qt::UserRole+1).toString();
    m_type = item->data(Qt::UserRole+2).toInt();
    if (!id.isEmpty()) {
        currentContactId = id;
        loadChatHistory(m_appid,currentContactId,m_type);
    }
}
void ChatPage::onContactItemClicked2(int appid,const QString &id,int type)
{
    m_appid = appid;
    m_type = type;
    if (!id.isEmpty()) {
        currentContactId = id;
        loadChatHistory(m_appid,currentContactId,m_type);
    }
}

void ChatPage::loadChatHistory(int appid2,const QString &contactId,int type)
{
    int bufferIdx=0;

    switch (isGroupMode) {
    case 0:bufferIdx=1;break; // 全量群
    case 1: bufferIdx=1;break;// 普通群
    case 2: bufferIdx=2;break;// 私聊
    case 3: bufferIdx=3;break;// 频道
    case 4: bufferIdx=4;break;// 频道私聊
    case 5:
        bufferIdx=type+1;
        break;
    default:
        return;
    }
    QString lastMsgId;
QList<Message> msg = readLatestMessagesFromBuffer(m_logStore[bufferIdx], 100,
                                                      contactId, appid2, (m_currentBotIndex != -1), &lastMsgId);
    m_msgid = lastMsgId;

    if (msg.size()!=0) {
        msgModel->setMessages(std::move(msg));
    } else {
        msgModel->clear();
    }
    msgListView->scrollToBottom();
    QString name=customGroupNames.value(contactId);
    titleLabel->setText(name.isEmpty() ? contactId:name);
}

void ChatPage::addMessage(const Message &msg)
{
        msgModel->addMessage(msg);
        msgListView->scrollToBottom();
}

void ChatPage::onSendmsg(QString &text)
{
    if (text.isEmpty()) return;

    int appid = (isGroupMode == 0) ? 全量群.value(currentContactId, m_appid) : m_appid;
    int msgType=0;

    if(最近对话.contains(currentContactId)) //有点懵逼真的 写这个
    {
        if(isGroupMode==5)
            parseFromId(最近对话.value(currentContactId),appid,msgType);
        else
            msgType=m_type;
    } else{
        最近对话.insert(currentContactId,mergeToId(appid,m_type));
        QFile file("data/最近对话.hash");
        if (file.open(QIODevice::WriteOnly)) {
            QDataStream out(&file);
            out.setVersion(QDataStream::Qt_5_15);
            out << 最近对话;
            file.close();
        }
        msgType=m_type;
    }

    if (!m_botClients.contains(appid)) {
        if(g_CW.contains(appid))
        {
            CardWidget *cw=g_CW[appid];
            QMessageBox::warning(this, "提示", QString("群来源机器人未在线 appid:%1 昵称：%2 请登录机器人后再试试").arg(appid).arg(cw->m_info->nickname));
            return;
        }

        QMessageBox::warning(this, "提示", "群来源机器人未在线 appid" + QString::number(appid)+" 请登录机器人后再试试");
        return;
    }

    QQBotClient *client = m_botClients[appid];
    if(!client->m_info->online)
    {
        QMessageBox::warning(this, "提示", QString("群来源机器人未在线 appid:%1 昵称：%2 请登录机器人后再试试").arg(appid).arg(client->m_info->nickname));
        return;
    }
    QString contactId = currentContactId;
    QString msgText = text;
    QString msgIdNormal = m_msgid;   // 第一次发送用的 msgId
    QString msgIdRetry = (isGroupMode == 0) ? QString() : m_msgid;
    QString nickname = client->m_info->nickname;

    SendMessageTask *task = new SendMessageTask(client, msgType, contactId, msgText,
                                                msgIdNormal, msgIdRetry, nickname,true);
    QThreadPool::globalInstance()->start(task);
}

void ChatPage::onSendClicked()
{
    if (currentContactId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择一个聊天对象");
        return;
    }
    QString text = inputEdit->toPlainText().trimmed();
    onSendmsg(text);

}
void ChatPage::onSendImage()
{
    if (currentContactId.isEmpty()) return;
    QString path = QFileDialog::getOpenFileName(this, "选择图片", "", "图片 (*.png *.jpg *.jpeg *.bmp *.wepb *.ico *.gif *.jxr);;所有文件 (*.*)");
    if (!path.isEmpty()) {
        QString text=QString("[image,path=%1]").arg(path);
        inputEdit->append(text);
    }
}
void ChatPage::onSendAudio()
{
    if (currentContactId.isEmpty()) return;
    // 补全常见音频格式：mp3, wav, flac, m4a, ogg, aac, wma, amr, ape
    QString path = QFileDialog::getOpenFileName(this, "选择音频", "",
                                                "音频文件 (*.mp3 *.wav *.flac *.m4a *.ogg *.aac *.wma *.amr *.ape);;所有文件 (*.*)");
    if (path.isEmpty()) return;
    QString text = QString("[audio,path=%1]").arg(path);
    onSendmsg(text);
}

void ChatPage::onSendVideo()
{
    if (currentContactId.isEmpty()) return;
    // 补全常见视频格式：mp4, avi, mkv, mov, wmv, flv, webm, m4v, 3gp
    QString path = QFileDialog::getOpenFileName(this, "选择视频", "",
                                                "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.3gp);;所有文件 (*.*)");
    if (path.isEmpty()) return;
    QString text = QString("[video,path=%1]").arg(path);
    onSendmsg(text);
}

void ChatPage::onSendFile()
{
    if (currentContactId.isEmpty()) return;
    QString path = QFileDialog::getOpenFileName(this, "选择文件", "", "所有文件 (*.*)");
    if(path.isEmpty()) return ;
    QString text = QString("[file,path=%1]").arg(path);
    onSendmsg(text);
}