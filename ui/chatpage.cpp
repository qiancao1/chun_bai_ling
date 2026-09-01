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
#include <QThreadPool>
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

#include <QTimer>

// 记录正在下载的媒体 URL 和对应的临时文件路径
static QMap<QString, QString> mediaDownloadingMap; // url -> tempFilePath
static QSet<QString> mediaDownloadingSet; // 避免重复下载
static bool extractImageInfo(const QString &content, bool &isLocalPath, QString &source) {
    if(!content.contains("[image,")) return false;
    QString tag = extractBetween(content, "path=", ",");
    QString tag2 = extractBetween(content, "path=", "]");
    if(tag.size()>tag2.size() && tag2.size()>10)
        tag=tag2;
    if (!tag.isEmpty()) {
        isLocalPath = true;
        source = tag;
        return true;
    }


    tag = extractBetween(content, "url=", ",");
    tag2 = extractBetween(content, "url=", "]");
    if(tag.isEmpty() && !tag2.isEmpty())
        tag=tag2;
    if (tag.isEmpty()) return false;

    isLocalPath = false;
    source = tag;
    return true;

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
static void downloadAvatarIfNeeded(int appid, const QString &openid) {
    if (openid.isEmpty()) return;
    QString avatarPath = QString("./avatars/%1.png").arg(openid);

    // 如果本地已经存在裁剪好的 32x32 圆形图片，直接跳过
    if (QFile::exists(avatarPath)) return;

    QString url = QString("https://thirdqq.qlogo.cn/qqapp/%1/%2/100")
                      .arg(appid)
                      .arg(openid);
    if (downloadingSet().contains(url)) return; // 已经在下载中

    downloadingSet().insert(url);
    QNetworkReply *reply = getNetworkManager()->get(QNetworkRequest(QUrl(url)));
    QObject::connect(reply, &QNetworkReply::finished, [reply, avatarPath, url]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QPixmap original;
            if (original.loadFromData(data)) {
                // 1. 将原图缩放（按比例，可能超出 32x32 的框）
                QPixmap scaled = original.scaled(32, 32, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

                // 2. 创建 32x32 透明画布
                QPixmap rounded(32, 32);
                rounded.fill(Qt::transparent);

                // 3. 在画布上画圆并裁剪
                QPainter p(&rounded);
                p.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addEllipse(0, 0, 32, 32);
                p.setClipPath(path);

                // 将缩放后的图片居中画进去（因为 KeepAspectRatioByExpanding 可能会拉伸）
                int x = (scaled.width() - 32) / 2;
                int y = (scaled.height() - 32) / 2;
                QPixmap cropped = scaled.copy(x, y, 32, 32);
                p.drawPixmap(0, 0, cropped);
                p.end();

                // 4. 直接保存 32x32 的最终图片
                QFile file(avatarPath);
                if (file.open(QIODevice::WriteOnly)) {
                    rounded.save(&file, "PNG");
                    file.close();
                    qDebug() << "Avatar saved and cropped (32x32):" << avatarPath;
                }
            }
        }
        downloadingSet().remove(url);
        reply->deleteLater();
    });
}


QString BubbleDelegate::downloadImageIfNeeded(const QString &url) const{
    if (url.isEmpty()) return QString();


    // 1. 计算 MD5 并构造本地文件路径
    QString md5 = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex();
    QString filePath = QString("tmp/聊天图片/%1.png").arg(md5);
    if (m_imageCache.contains(url)) return filePath;
    if (downloadingSet().contains(url)) return QString();
    QFileInfo fileInfo(filePath);

    // 2. 若本地文件存在，直接加载并缓存
    if (fileInfo.exists()) {
        QPixmap pix;
        if (pix.load(filePath)) {
            // 保持与下载后一致的缩放逻辑（若文件已缩放则不会改变）
            if (pix.width() > 128 || pix.height() > 128) {
                pix = pix.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            m_imageCache.insert(url, new QPixmap(pix));
        }
        return filePath;
    }

    // 3. 确保目录存在
    QDir dir(fileInfo.absolutePath());
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 4. 开始下载（原有逻辑，增加保存文件）
    downloadingSet().insert(url);
    QNetworkReply *reply = getNetworkManager()->get(QNetworkRequest(QUrl(url)));
    QObject::connect(reply, &QNetworkReply::finished, [this,reply, url, filePath]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QPixmap pix;
            if (pix.loadFromData(data)) {
                // 限制最大 128x128，保持比例
                if (pix.width() > 128 || pix.height() > 128) {
                    pix = pix.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
                m_imageCache.insert(url, new QPixmap(pix));
                // 保存到本地文件
                if (!pix.save(filePath, "PNG")) {
                    // 可选：记录保存失败日志
                }
            }
        }
        downloadingSet().remove(url);
        reply->deleteLater();
    });
    return QString();
}


// ==================== MessageListModel ====================
MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// 放在全局工具类或 MessageListModel 中
static QString sanitizeText(const QString &input, const QString &defaultValue = "?") {
    if (input.isEmpty()) return defaultValue;

    QString output;
    output.reserve(input.size());
    for (const QChar &ch : input) {
        if (ch.isPrint() || ch.isSpace()) {
            if (ch != QChar::ReplacementCharacter &&
                (ch.category() != QChar::Other_Control || ch == '\n' || ch == '\r')) {
                output.append(ch);
            }
        }
    }
    if (output.trimmed().isEmpty()) {
        return defaultValue;
    }
    return output;
}
void MessageListModel::setMessages(QList<Message> &&msgs)
{
    beginResetModel();
    m_messages = std::move(msgs);
    for (Message &msg : m_messages) {

        msg.name = sanitizeText(msg.name, "未命名");

    }

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
    Message cleaned = msg;
    cleaned.name = sanitizeText(cleaned.name, "未命名");
    if (m_messages.size() >= 200) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_messages.removeFirst();
        endRemoveRows();
    }
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(cleaned);
    endInsertRows();
    if (!cleaned.isSelf && !cleaned.user.isEmpty()) {
        if(cleaned.user.length()!=32) return;
        downloadAvatarIfNeeded(chatPage->m_appid,cleaned.user);
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
    case ContentRole:
        if(msg.isSelf)
        {
            return msg.direction;
        }
        return msg.msg;
    case IsSelfRole: return msg.isSelf;
    case TimestampRole: return msg.timestamp;
    case name : return msg.name.isEmpty() ? msg.user : msg.name;
    case hf : return msg.hf;
    case ch :
        if(msg.isSelf)
        {
            return msg.plugin_ch;
        }
        return msg.ch;
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
void MessageListModel::set_sh(const QModelIndex &index)
{
    if (!index.isValid() || index.row() >= m_messages.size())
        return ;
    Message &msg = m_messages[index.row()];
    msg.msg = "[已删除]";
    msg.ch="";
}

void MessageListModel::clear()
{
    beginResetModel();
    m_messages.clear();
    endResetModel();
}
QString replaceFileTag();


void BubbleDelegate::drawDefaultAvatar(QPainter* painter, const QRect& rect, const QString& text, bool isSelf) const
{
    QColor color = isSelf ? QColor(255,190,104) : QColor(228,238,214);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawEllipse(rect);
    painter->setPen(Qt::white);
    //painter->setFont(QFont("Microsoft YaHei", 14, QFont::Bold));
    painter->drawText(rect, Qt::AlignCenter, text);
}

BubbleDelegate::CachedData BubbleDelegate::prepareMessageData(const QString &rawContent, bool isSelf, const QString &timestamp) const
{
    CachedData data;
    QString content = rawContent;

    // 1. 提取图片信息（如果有）
    bool isLocal = true;
    QString imagePath;
    bool hasImage = extractImageInfo(content, isLocal, imagePath);
    data.hasImage = hasImage;
    data.imagePath = imagePath;
    data.imageIsLocal = true;
    if(!isLocal){
        data.imagePath = downloadImageIfNeeded(imagePath);

    }
    // 2. 去除图片、视频、音频等标签，得到纯文本
    // 这里复用您原来的 replaceBetweenAll 等逻辑
    if (content.contains("[image,"))
        content = replaceBetweenAll(content, "[image,", "]","");
    else if (content.contains("[video,"))
        content = replaceBetweenAll(content, "[video,", "]", "[视频]");
    else if (content.contains("[audio,"))
        content = replaceBetweenAll(content, "[audio,", "]", "[语音]");
    else if (content.contains("[v,"))
        content = replaceBetweenAll(content, "[v,", "]", "[视频]");
    else if (content.contains("[a,"))
        content = replaceBetweenAll(content, "[a,", "]", "[语音]");
    else if (content.contains("[file,"))
        content = replaceFileTag(content);  // 您已有的函数
    data.displayText = content;

    // 3. 文本换行计算（与原 paint 完全一致）
    if (!m_textFm) {


        m_textFont = QFont();
        m_nameFont = QFont();
        m_timeFont = QFont();
        m_textFont.setPointSize(11); // 约等于 14px
        m_nameFont.setPointSize(9); // 约等于 14px
        m_timeFont.setPointSize(8); // 约等于 14px

        const_cast<BubbleDelegate*>(this)->m_textFm = new QFontMetrics(m_textFont);
        const_cast<BubbleDelegate*>(this)->m_nameFm = new QFontMetrics(m_nameFont);
        const_cast<BubbleDelegate*>(this)->m_timeFm = new QFontMetrics(m_timeFont);
    }

    const int maxBubbleWidth = 460;
    QStringList lines;
    const QStringList paragraphs = content.split('\n');
    for (const QString &para : paragraphs) {
        QString remaining = para;
        while (!remaining.isEmpty()) {
            int lastGood = 0;
            int totalWidth = 0;
            for (int i = 0; i < remaining.length(); ++i) {
                int charWidth = m_textFm->horizontalAdvance(remaining.at(i));
                if (totalWidth + charWidth > maxBubbleWidth) break;
                totalWidth += charWidth;
                lastGood = i + 1;
            }
            if (lastGood == 0) lastGood = 1;
            lines.append(remaining.left(lastGood));
            remaining = remaining.mid(lastGood);
        }
    }
    data.wrappedLines = lines;

    // 4. 计算文本宽度和高度
    int textWidth = 0;
    for (const QString &line : std::as_const(lines)) {
        int w = m_textFm->horizontalAdvance(line);
        if (w > textWidth) textWidth = w;
    }
    data.textWidth = qMin(qMax(textWidth, 48), maxBubbleWidth);
    data.textHeight = lines.size() * m_textFm->height() + 4;

    // 5. 计算总高度（用于 sizeHint）
    int nameHeight = isSelf ? 0 : m_nameFm->height() + 4;
    int timeHeight = m_timeFm->height();
    int imageHeight = hasImage ? 128 + 8 : 0;  // 默认图片占位高度，实际绘制时会调整
    int bubbleHeight = nameHeight + data.textHeight + imageHeight + timeHeight + 8;
    data.totalHeight = qMax(bubbleHeight, 30);
    if (isSelf) data.totalHeight += 16;

    return data;
}

// ---------- paint 实现 ----------
void BubbleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{

    // 1. 确保字体度量已初始化
    if (!m_textFm) {
        m_textFont = QFont();
        m_nameFont = QFont();
        m_timeFont = QFont();
        m_textFont.setPointSize(11); // 约等于 14px
        m_nameFont.setPointSize(9); // 约等于 14px
        m_timeFont.setPointSize(8); // 约等于 14px
        m_textFm = new QFontMetrics(m_textFont);
        m_nameFm = new QFontMetrics(m_nameFont);
        m_timeFm = new QFontMetrics(m_timeFont);
    }
    QElapsedTimer timer;
    timer.start();

    QString sender = index.data(MessageListModel::SenderRole).toString();
    QString name = index.data(MessageListModel::name).toString();
    QString content = index.data(MessageListModel::ContentRole).toString();
    bool isSelf = index.data(MessageListModel::IsSelfRole).toBool();
    QString timestamp = index.data(MessageListModel::TimestampRole).toString();

    // 3. 生成缓存 key，获取或创建缓存数据
    QString cacheKey = content + (isSelf ? "_self" : "_other");
    CachedData *cached = m_cache[cacheKey];

    if (!cached) {
        CachedData data = prepareMessageData(content, isSelf, timestamp);
        m_cache.insert(cacheKey, new CachedData(data));
        cached = m_cache[cacheKey];
        if (!cached) {

            CachedData temp = prepareMessageData(content, isSelf, timestamp);
            cached = &temp;  // 注意：不能保存指针，仅本次绘制使用

        }
    }

    // 为了安全，如果缓存获取失败（极少情况），我们使用本地数据
    CachedData localData;
    if (!cached) {
        localData = prepareMessageData(content, isSelf, timestamp);
        cached = &localData;
    }

    // 4. 从缓存数据中取出绘制所需字段
    const QStringList &lines = cached->wrappedLines;
    int textWidth = cached->textWidth;
    bool hasImage = cached->hasImage;
    QString imagePath = cached->imagePath;
    bool imageIsLocal = cached->imageIsLocal;


    //painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::TextAntialiasing, false);
    const int avatarSize = 40;
    const int margin = 20;
    const int maxBubbleWidth = 460;

    int textHeight = lines.size() * m_textFm->height() + 4;

    // 图片高度处理
    int imageHeight = 0;
    QPixmap imgPixmap;
    if (hasImage) {
        // 从缓存获取图片（若已加载）
        if (m_imageCache.contains(imagePath)) {
            imgPixmap = *m_imageCache[imagePath];
            if (!imgPixmap.isNull())
                imageHeight = imgPixmap.height() + 8;
        } else if (imageIsLocal && QFile::exists(imagePath)) {
            QPixmap original(imagePath);
            if (!original.isNull()) {
                QPixmap scaled = original.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                m_imageCache.insert(imagePath, new QPixmap(scaled));
                imgPixmap = scaled;
                imageHeight = imgPixmap.height() + 8;
            } else {
                imageHeight = 128 + 8; // 占位
            }
        } else {
            imageHeight = 128 + 8; // 网络图片占位
        }
    }

    // 名字和时间高度
    int nameHeight = isSelf ? 0 : m_nameFm->height() + 4;
    int timeHeight = m_timeFm->height();

    int bubbleHeight = nameHeight + textHeight + imageHeight + timeHeight + 8;
    int totalHeight = qMax(bubbleHeight, 30);
    totalHeight += -2;

    QRect rect = option.rect;
    int bubbleWidth = qMin(textWidth + 28, maxBubbleWidth);
    if (hasImage && !imgPixmap.isNull()) {
        bubbleWidth = qMax(bubbleWidth, imgPixmap.width() + 24);
    }
    bubbleWidth = qMax(bubbleWidth, 130)+24;

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

    // ---------- 绘制头像（使用缓存） ----------
    QString avatarPath = isSelf ? QString("./avatars/%1.png").arg(chatPage->m_appid) : QString("./avatars/%1.png").arg(sender);
    QPixmap avatarPixmap;
    if (m_avatarCache.contains(avatarPath)) {
        avatarPixmap = *m_avatarCache[avatarPath];
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
            m_avatarCache.insert(avatarPath, new QPixmap(avatarPixmap));
        }
    }

    if (!avatarPixmap.isNull()) {
        painter->drawPixmap(avatarRect, avatarPixmap);
        painter->setPen(QPen(QColor(200,200,200), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(avatarRect);
    } else {
        // 默认头像
        painter->setPen(Qt::NoPen);
        QColor avatarColor = isSelf ? QColor(255,190,104) : QColor(228,238,214);
        painter->setBrush(avatarColor);
        painter->drawEllipse(avatarRect);
        painter->setPen(Qt::white);
        //painter->setFont(QFont("Microsoft YaHei", 14, QFont::Bold));
        painter->drawText(avatarRect, Qt::AlignCenter, isSelf ? "我" : name.left(1));
    }

    // ---------- 绘制气泡背景 ----------

    painter->setPen(Qt::NoPen);
    painter->setBrush(isSelf ? QColor(210,244,184) : QColor(238,232,226));
    painter->drawRect(bubbleRect); // 快速画直角
    // ---------- 绘制内容 ----------
    painter->save();
    painter->translate(bubbleRect.topLeft() + QPoint(12, 6));

    int yOffset = 0;
    if (!isSelf) {
        painter->setFont(m_nameFont);
        painter->setPen(QColor(136,136,136));
        painter->drawText(0, yOffset + m_nameFm->ascent()-2, name);
        yOffset += nameHeight;
    }

    // 绘制文本
    painter->setFont(m_textFont);
    painter->setPen(Qt::black);
    int lineHeight = m_textFm->height();
    for (const QString &line : lines) {
        painter->drawText(0, yOffset + m_textFm->ascent(), line);
        yOffset += lineHeight;
    }
    yOffset += 4;

    // 绘制图片
    if (hasImage) {
        if (!imgPixmap.isNull()) {
            painter->drawPixmap(0, yOffset, imgPixmap);
            yOffset += imgPixmap.height() + 4;
        } else {
            // 占位

            painter->setPen(QColor(150,150,150));
            painter->drawText(0, yOffset + 20, "图片加载中...");
            yOffset += 128 + 4;
        }
    }

    // 时间
    painter->setFont(m_timeFont);
    painter->setPen(QColor(170,170,170));
    int timeWidth = m_timeFm->horizontalAdvance(timestamp);
    painter->drawText(bubbleWidth - 8 - timeWidth, yOffset + m_timeFm->ascent()-2, timestamp);

    painter->restore();

}

// ---------- sizeHint 实现 ----------
QSize BubbleDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{

    // 确保字体度量初始化
    if (!m_textFm) {
        m_textFont = QFont();
        m_nameFont = QFont();
        m_timeFont = QFont();
        m_textFont.setPointSize(11); // 约等于 14px
        m_nameFont.setPointSize(9); // 约等于 14px
        m_timeFont.setPointSize(8); // 约等于 14px
        m_textFm = new QFontMetrics(m_textFont);
        m_nameFm = new QFontMetrics(m_nameFont);
        m_timeFm = new QFontMetrics(m_timeFont);
    }

    QString content = index.data(MessageListModel::ContentRole).toString();
    bool isSelf = index.data(MessageListModel::IsSelfRole).toBool();
    QString timestamp = index.data(MessageListModel::TimestampRole).toString();

    QString cacheKey = content + (isSelf ? "_self" : "_other");
    CachedData *cached = m_cache[cacheKey];
    if (!cached) {
        CachedData data = prepareMessageData(content, isSelf, timestamp);
        m_cache.insert(cacheKey, new CachedData(data));
        cached = m_cache[cacheKey];
    }
    int height = cached ? cached->totalHeight : 80; // 保底高度
    return QSize(option.rect.width(), height);
}

#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>

class ContactListDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // 每个列表项的高度（加上间距，头像32px + 两行文字，建议给 60px）
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return QSize(-1, 40);
    }

    void paint (QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {

        if (!index.isValid()) return;

        // 1. 提取数据
        QString name = index.data(Qt::UserRole + 3).toString();
        QString lastMsg = index.data(Qt::UserRole + 4).toString();
        lastMsg.replace("\n", " "); // 原代码中的换行替换

        // 提取存进去的 Painted PIXMAP（如果在数据里就已经处理好了）
        QPixmap avatarPix = index.data(Qt::UserRole + 5).value<QPixmap>();
        QColor avatarColor = index.data(Qt::UserRole + 6).value<QColor>();
        QString avatarText = index.data(Qt::UserRole + 7).toString();

        // 2. 绘制背景
        painter->save();
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, QColor(230, 230, 230)); // 选中态颜色
        } else {
            painter->fillRect(option.rect, Qt::white); // 默认白底
        }

        // 3. 计算坐标（模仿原来的 layout 边距）
        int margin = 6;
        int iconSize = 32;
        int iconX = option.rect.x() + margin;
        int iconY = option.rect.y() + (option.rect.height() - iconSize) / 2;
        QRect iconRect(iconX, iconY, iconSize, iconSize);

        int textX = iconX + iconSize + margin;
        int textWidth = option.rect.width() - textX - margin;

        // 4. 绘制头像
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addEllipse(iconRect);
        painter->setClipPath(path);

        if (!avatarPix.isNull()) {
            painter->drawPixmap(iconRect, avatarPix);
        } else {
            // 回退：绘制圆形色块 + 文字
            painter->fillRect(iconRect, avatarColor);
            painter->setClipping(false); // 绘制文字时取消裁剪，以免字被切掉
            painter->setPen(Qt::white);
            QFont Font = QFont();
            Font.setBold(true);
            Font.setPointSize(16); // 约等于 14px
            painter->setPen(QFont::Bold); // 灰色
            painter->setFont(Font);

            painter->drawText(iconRect, Qt::AlignCenter, avatarText);
        }
        painter->setClipping(false);

        // 5. 绘制名字 (加粗 14px)
        painter->setPen(Qt::black);
        QFont nameFont = painter->font();
        nameFont.setBold(true);
        nameFont.setPointSize(10); // 约等于 14px
        painter->setFont(nameFont);
        QRect nameRect(textX, option.rect.y() + 2, textWidth, 20);
        //painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, "绘制最新消息绘制最新消息");
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, name.left(20));

        // 6. 绘制最新消息 (灰色 11px)
        painter->setPen(QColor(150, 150, 150)); // 灰色

        nameFont.setBold(false);
        nameFont.setPointSize(8); // 约等于 11px
        painter->setFont(nameFont);
        QRect msgRect(textX, option.rect.y() + 22, textWidth, 20);
        //painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter, "绘制最新消息绘制最新消息");
        painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter, lastMsg.left(20));

        painter->restore();

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

    QFile file3("data/最近对话.hash");
    if (file3.open(QIODevice::ReadOnly)) {
        QDataStream in(&file3);
        in.setVersion(QDataStream::Qt_5_15);
        in >> 最近对话;
        file3.close();
    }
}

ChatPage::~ChatPage() {}
QStandardItemModel *m_model=nullptr;
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
            font-size: 20px;
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
            min-width: 24px;
            min-height: 24px;
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
    leftLayout->setContentsMargins(2, 2,2, 2);
    leftLayout->setSpacing(4);

    // ========== 模式按钮栏（两行，每行三个） ==========
    QVBoxLayout *btnVerticalLayout = new QVBoxLayout;
    btnVerticalLayout->setSpacing(2);
    btnVerticalLayout->setContentsMargins(2,2,2,2);
    // 第一行：全量、群聊、私聊
    QHBoxLayout *row1 = new QHBoxLayout;
    row1->setContentsMargins(2,2,2,2);
    row1->setSpacing(3);
    btnChat = new QPushButton("全量");
    btnGroupChat = new QPushButton("群聊");
    btnPrivateChat = new QPushButton("私聊");



    btnChat->setCheckable(true);
    btnGroupChat->setCheckable(true);
    btnPrivateChat->setCheckable(true);

    btnChat->setFixedHeight(28);
    btnGroupChat->setFixedHeight(28);
    btnPrivateChat->setFixedHeight(28);


    row1->addWidget(btnChat);
    row1->addWidget(btnGroupChat);
    row1->addWidget(btnPrivateChat);

    // 第二行：频道、频道私聊、最近
    QHBoxLayout *row2 = new QHBoxLayout;
    row2->setSpacing(8);
    btnChannelChat = new QPushButton("频道");
    btnChannelPrivate = new QPushButton("频道私聊");
    btnRecentChat = new QPushButton("最近");

    btnChannelChat->setCheckable(true);
    btnChannelPrivate->setCheckable(true);
    btnRecentChat->setCheckable(true);

    btnChannelChat->setFixedHeight(28);
    btnChannelPrivate->setFixedHeight(28);
    btnRecentChat->setFixedHeight(28);

    row2->addWidget(btnChannelChat);
    row2->addWidget(btnChannelPrivate);
    row2->addWidget(btnRecentChat);

    btnVerticalLayout->addLayout(row1);
    btnVerticalLayout->addLayout(row2);
    leftLayout->addLayout(btnVerticalLayout);  // 替换原来的 leftLayout->addLayout(btnLayout)


    m_model  = new QStandardItemModel(this);
    contactList = new QListView(this);
    contactList->setModel(m_model); // 现在可以调用了！
    contactList->setItemDelegate(new ContactListDelegate(this));
    contactList->setSelectionMode(QAbstractItemView::SingleSelection); // 确保支持选择
    contactList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel); // 滚动丝滑
    contactList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 隐藏横向滚动条
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



    titleLabel = new QLabel("未选择会话");
    titleLabel->setObjectName("chatTitle");



    rightLayout->addWidget(titleLabel);

    // 消息列表视图
    msgModel = new MessageListModel(this);

    msgListView = new QListView;
    msgListView->setObjectName("messageList");
    msgListView->setModel(msgModel);
    msgListView->setItemDelegate(new BubbleDelegate(this));
    msgListView->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    msgListView->verticalScrollBar()->setSingleStep(8);

    // ================================================================

    msgListView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(msgListView, &QListView::customContextMenuRequested, this, &ChatPage::showMessageContextMenu);
    connect(msgListView, &QListView::doubleClicked, this, &ChatPage::onMessageDoubleClicked);
    rightLayout->addWidget(msgListView, 1);

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
    containerLayout->setContentsMargins(2, 2, 2, 2);
    containerLayout->setSpacing(2);

    inputEdit = new QTextEdit;
    inputEdit->setObjectName("chatInput");
    inputEdit->setPlaceholderText("输入消息(ctrl+回车键)发送...");
    inputEdit->setMaximumHeight(90);
    inputEdit->installEventFilter(this);
    containerLayout->addWidget(inputEdit);

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
    聊天发送模式=g_config["SendType"].toInt();
    comboSendType->setCurrentIndex(聊天发送模式);
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

    // 将原来的 &QListWidget::itemClicked 改为 QAbstractItemView::clicked
    connect(contactList, &QAbstractItemView::clicked, this, &ChatPage::onContactItemClicked);

    // customContextMenuRequested 是 QWidget 的信号，QListView 直接继承，不需要改类名
    connect(contactList, &QWidget::customContextMenuRequested, this, &ChatPage::showContactListContextMenu);
    connect(btnSend, &QPushButton::clicked, this, &ChatPage::onSendClicked);
    connect(btnSendImage, &QPushButton::clicked, this, &ChatPage::onSendImage);
    connect(btnSendAudio, &QPushButton::clicked, this, &ChatPage::onSendAudio);
    connect(btnSendVideo, &QPushButton::clicked, this, &ChatPage::onSendVideo);
    connect(btnSendFile, &QPushButton::clicked, this, &ChatPage::onSendFile);
    connect(comboSendType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [=](int index){
                聊天发送模式=index;
                g_config["SendType"] = index;
                saveConfig();
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
    QModelIndex index = contactList->indexAt(pos);
    if (index.isValid()) {

        QAction *sztx = menu.addAction("设置群头像");

        // --- 设置群头像逻辑 ---
        connect(sztx, &QAction::triggered, this, [this, index]() {
            // 3. 替换这里：从 index 中获取数据
            QString id = index.data(Qt::UserRole + 1).toString();

            if (id.isEmpty()) {
                QMessageBox::warning(this, "错误", "未获取到群ID");
                return;
            }

            QString fileName = QFileDialog::getOpenFileName(
                this, "选择群头像", "", "图片 (*.png *.jpg *.jpeg *.bmp *.gif)");
            if (fileName.isEmpty()) return;

            QPixmap pixmap(fileName);
            if (pixmap.isNull()) {
                QMessageBox::warning(this, "错误", "无法加载所选图片");
                return;
            }
            QPixmap scaled = pixmap.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            QDir dir("avatars");
            if (!dir.exists()) {
                if (!dir.mkpath(".")) {
                    QMessageBox::warning(this, "错误", "无法创建 avatars 目录");
                    return;
                }
            }

            QString avatarPath = QString("avatars/%1.png").arg(id);
            if (!scaled.save(avatarPath, "PNG")) {
                QMessageBox::warning(this, "错误", "保存头像失败，请检查目录权限");
                return;
            }
            QMessageBox::information(this, "成功", "群头像已更新");


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
QString getBotName(int appid);

void ChatPage::updateAllContactLists(int index)
{
    int bufferIdx=0;
    m_model->clear();
    seen.clear(); //过滤器
    s_wrapCache.clear(); //聊天记录缓存
    avatarCache.clear(); //头像缓存
    bool sw=false;

    switch (index) {
    case 0: // 全量群
        sw = g_logdb[1]->beginTransaction(true);
        contactList->setUpdatesEnabled(false);

        for (auto it = 全量群.begin(); it != 全量群.end(); ++it) {
            Contact c;
            c.id = it.key();


            int appid =it.value();
            if (m_currentBotIndex != -1) {
                if (appid != m_accounts[m_currentBotIndex]->appid_int) continue;
            }
            Message msg;
            if(sw)
            {
                g_logdb[1]->getLatestLogInTxn(g_logdb[1]->getCurrentTxn(),QString::number(appid), c.id, msg);
            }else{
                g_logdb[1]->getLatestLog(QString::number(appid),c.id,msg);

            }

            if(msg.Gname.isEmpty()){
                if (m_currentBotIndex == -1)
                    c.name =getBotName(appid)+":"+ it.key();
                else
                    c.name = it.key();
            }else{
                if (m_currentBotIndex == -1)
                    c.name =getBotName(appid)+":"+ msg.Gname;
                else
                    c.name = msg.Gname; //c 的和 msg无关
            }

            if(msg.name.isEmpty())
                c.lastMsgTime = "无信息";      // 忽略
            else
                c.lastMsgTime = msg.name+":"+msg.msg;      // 忽略
            addDataToModel(appid, c,0);

        }

        contactList->setUpdatesEnabled(true);
        if(sw) g_logdb[1]->commitTransaction();
        return;
    case 1: bufferIdx=1;break;// 普通群
    case 2: bufferIdx=2;break;// 私聊
    case 3: bufferIdx=3;break;// 频道
    case 4: bufferIdx=4;break;// 频道私聊
    case 5:

        contactList->setUpdatesEnabled(false);
        for (auto it = 最近对话.begin(); it != 最近对话.end(); ++it) {
            int appid=0,type=0;
            parseFromId(it.value(),appid,type);

            if (m_currentBotIndex != -1) {
                if (appid != m_accounts[m_currentBotIndex]->appid_int) continue;
            }
            Contact c;
            c.id = it.key();
            if(type==2 || type==0){
                if(m_botClients.contains(appid)){
                    MessageEvent ev;
                    ev.appid = appid;
                    ev.type = type;

                    ev.groupId =c.id;
                    ev.user = c.id;

                    g_botdb[appid]->getOrUpdateUser(m_botClients[appid],ev,true);

                    if(type==0)
                        c.name = ev.groupname;       // 没有 name，就用 key
                    else
                        c.name = ev.nickname;

                }
            }

            if(c.name.isEmpty()) c.name=c.id;
            if (m_currentBotIndex == -1)
                c.name =getBotName(appid)+":"+ c.name;

            c.lastMsgTime = "无信息";      // 忽略
            addDataToModel(appid, c,type);

        }
        contactList->setUpdatesEnabled(true);

        return;
    default:
        return;
    }
    int type=0;

    const QStringList list = g_logdb[bufferIdx]->getLatestKeys(1000);
    QSet<QPair<int, QString>> seen;
    sw = g_logdb[bufferIdx]->beginTransaction(true);
    QMap<int, QSet<QString>> appidGroups;   // appid -> 去重后的群ID集合
    contactList->setUpdatesEnabled(false);
    for (const QString &keyStr : list) {

        QStringList parts = keyStr.split(':');
        if (parts.size() != 3) continue;

        bool ok;
        int appid = parts[1].toInt(&ok);
        if (!ok) continue;

        QString groupId = parts[2];

        uint64_t seq = parts[0].toULongLong(&ok);
        if (!ok) continue;

        if (m_currentBotIndex != -1) {
            if (appid != m_accounts[m_currentBotIndex]->appid_int) continue;
        }

        QPair<int, QString> key(appid, groupId);
        if (seen.contains(key)) continue;  // 每个群组只取最新一条

        seen.insert(key);
        if(bufferIdx==1)
            appidGroups[appid].insert(groupId);   // 自动去重
        Message msg;
        if(sw)
        {
            g_logdb[bufferIdx]->readLogInTxn(g_logdb[bufferIdx]->getCurrentTxn(),QString::number(appid), groupId, seq, msg);

        }else{
            g_logdb[bufferIdx]->readLog(QString::number(appid), groupId, seq, msg);
        }
        Contact c;
        c.id = groupId;

        if (bufferIdx == 3 || bufferIdx == 4) {
            c.name = msg.name;           // 发送人昵称
            if (!c.id.isEmpty()) {
                downloadAvatarIfNeeded(appid, c.id);
            }
            c.lastMsgTime = msg.msg;     // 最新消息内容
        } else {
            c.name = msg.Gname;
            c.lastMsgTime = msg.name + ": " + msg.msg;
        }

        if (c.name.isEmpty()) {
            c.name = c.id;
        }
        if (m_currentBotIndex == -1)
            c.name = getBotName(appid)+":"+c.name;

        addDataToModel(appid, c, bufferIdx - 1);

    }



    contactList->setUpdatesEnabled(true);
    if(sw) g_logdb[bufferIdx]->commitTransaction();




    if(bufferIdx==1){
        uint32_t nowMin = BotDB::nowMinutes();   // 获取当前分钟数
        for (auto it = appidGroups.begin(); it != appidGroups.end(); ++it) {
            int appid = it.key();
            const QSet<QString>& groupSet = it.value();
            if (groupSet.isEmpty()) continue;

            // 转换为 QList
            QList<QString> groupIdList = groupSet.values();

            // 检查是否有对应的 BotDB 实例
            if (g_botdb.contains(appid)) {
                g_botdb[appid]->batchAddGroups(groupIdList, nowMin);
            } else {
                qWarning() << "未找到 appid" << appid << "对应的 BotDB 实例，跳过群组批量添加";
            }
        }
    }
}

void ChatPage::addDataToModel(int appid, const Contact& c, int type)
{
    QStandardItem *item = new QStandardItem();

    // 存储基础数据
    item->setData(appid, Qt::UserRole);
    item->setData(c.id, Qt::UserRole + 1);
    item->setData(type, Qt::UserRole + 2);
    item->setData(c.name, Qt::UserRole + 3);
    item->setData(c.lastMsgTime, Qt::UserRole + 4);

    // --- 预先生成并缓存头像（把原来 Widget 里的逻辑挪过来！） ---
    QPixmap avatarPix;
    QString avatarPath = QString("./avatars/%1.png").arg(c.id);
    if (QFile::exists(avatarPath)) {
        QPixmap original(avatarPath);
        if (!original.isNull()) {
            QPixmap rounded(32, 32);
            rounded.fill(Qt::transparent);
            QPainter p(&rounded);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addEllipse(0, 0, 32, 32);
            p.setClipPath(path);
            QPixmap scaled = original.scaled(32, 32, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = (scaled.width() - 32) / 2;
            int y = (scaled.height() - 32) / 2;
            QPixmap cropped = scaled.copy(x, y, 32, 32);
            p.drawPixmap(0, 0, cropped);
            p.end();
            avatarPix = rounded;
        }
    }

    // 回退备用信息（如果头像图片不存在，在委托里直接画色块和首字母）
    QColor fallbackColor = QColor::fromHsl(qHash(c.name) % 360, 200, 150);
    QString fallbackText = c.name.isEmpty() ? "?" : c.name.left(1);

    // 保存图片、颜色和备用文字到 Model
    item->setData(avatarPix, Qt::UserRole + 5);
    item->setData(fallbackColor, Qt::UserRole + 6);
    item->setData(fallbackText, Qt::UserRole + 7);


    m_model->appendRow(item);
}

void ChatPage::addContact(int type, const MessageEvent &ev,const QString &name)
{
    if (isGroupMode == type || type==0) {
        if (m_currentBotIndex != -1) {
            if (m_appid != ev.appid) return;
        }
        if(ev.groupId==currentContactId)
        {
            m_msgid = ev.msgId;
            if(存在)
            {
                QMetaObject::invokeMethod(this, [=]() {
                    addMessage(Message(ev.user,ev.msg,false, QDateTime::currentDateTime().toString("hh:mm:ss"),name.isEmpty() ? ev.user : name,ev.replyTo,ev.msgId));
                });
            }

        }
        if(type==0)
        {
            if(全量群.contains(ev.groupId)) return;
            全量群.insert(ev.groupId,ev.appid);
            QFile file("data/全量群.hash");
            if (file.open(QIODevice::WriteOnly)) {
                QDataStream out(&file);
                out.setVersion(QDataStream::Qt_5_15);
                out << 全量群;   // 直接序列化整个 QHash
                file.close();
            }
        }else{
            QPair<int, QString> key(ev.appid, ev.groupId);
            if (seen.contains(key)) return;  // 已存在，不重复添加
            seen.insert(key);
        }
        if(!存在)  return;

        Contact c;
        c.id = ev.groupId;                     // 这个 id 将用作头像文件名和 URL 中的 openid


        if(ev.nickname.isEmpty())
            c.lastMsgTime = ev.msg;
        else
            c.lastMsgTime = ev.nickname+": "+ev.msg;
        if(type==3){
            c.name = ev.nickname.isEmpty() ? ev.user : ev.nickname;
            if (!c.id.isEmpty()) {
                downloadAvatarIfNeeded(ev.appid,c.id);
            }
        }else
        {
            c.name = ev.groupname;
        }
        if(c.name.isEmpty())
        {
            c.name=c.id;
        }
        if(type!=0)type--;
        QMetaObject::invokeMethod(this, [=]() {
            addDataToModel(ev.appid,c,type);
        });


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
    QAction *sc = menu.addAction("太长了删除");
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
   } else if (selectedAction == sc) {
       msgModel->set_sh(index);
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


void ChatPage::onContactItemClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    m_appid = index.data(Qt::UserRole).toInt();
    QString id = index.data(Qt::UserRole+1).toString();
    m_type = index.data(Qt::UserRole+2).toInt();
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
//加载聊天记录
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
    QList<Message> msg = g_logdb[bufferIdx]->getRecentLogs(QString::number(appid2),contactId,2147483636,100);
    m_msgid.clear();  // 默认清空


    QString name;
    if (msg.size()!=0) {
        for (int i = msg.size()-1; i > 0; --i) {
            if (!msg[i].isSelf) {
                m_msgid = msg[i].ch;
                break;
            }
        }
        name= msg[0].Gname;
        msgModel->setMessages(std::move(msg));


    } else {
        msgModel->clear();
    }
    msgListView->scrollToBottom();

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



    SendMessageTask *task = new SendMessageTask(client, msgType, contactId, msgText,
                                                msgIdNormal,"[聊天室]",true);
    QThreadPool::globalInstance()->start(task);
}

void ChatPage::onSendClicked()
{
    if (currentContactId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择一个聊天对象");
        return;
    }
    QString text = inputEdit->toHtml();
    QString t2= inputEdit->toPlainText();
    qDebug() <<t2;
    const QStringList list = takeAllTextMiddle(text,"<img src=\"","\" alt=\"本地图片\" width=\"64\" />",false);
    for(const auto &t : list)
    {

        t2 = subTextReplace(t2,"￼","![img]("+t+")",1);
    }
    onSendmsg(t2);

}
void ChatPage::onSendImage()
{
    if (currentContactId.isEmpty()) return;
    QString path = QFileDialog::getOpenFileName(this, "选择图片", "", "图片 (*.png *.jpg *.jpeg *.bmp *.webp *.ico *.gif *.jxr);;所有文件 (*.*)");
    if (!path.isEmpty()) {

        //<img src="C:/Users/Airuan/Pictures/AI绘画/下载.png" alt="本地图片" />
        QString text = QString("<img src=\"%1\" width=\"64\" alt=\"本地图片\" />").arg(path);
        QTextCursor cursor = inputEdit->textCursor();
        cursor.insertHtml(text);
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