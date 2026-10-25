#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include <QListView>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QMap>
#include <QDateTime>
#include <QAbstractListModel>
#include <QStyledItemDelegate>
#include <QListWidget>
#include <qlabel.h>
#include "qqbotclient.h"
#include "placeholdertextedit.h"   // 如果你也有 QTextEdit 的替换
#include <QCache>
#include <QMutex>
#include <qstandarditemmodel.h>

// 消息结构
struct Message {
    QString user;
    QString msg;
    QString timestamp;
    QString name;
    QString hf; //回复用
    QString ch; //撤回消息用
    QString plugin_ch;
    QString direction;
    QString ref_name;
    QString ref_msg;
    QString Gname;
    int seq=0;
    int Color_0=0;
    bool isSelf=false;

    Message() {}
    Message(const QString& s, const QString& c, bool self,const QString &t,const QString &n,const QString &hf,const QString &ch)
        : user(s), msg(c), isSelf(self), timestamp(t) , name(n),hf(hf),ch(ch) {}
};

// 联系人结构
struct Contact {
    QString id;
    QString name;
    QString lastMsgTime;
};
// 在 ChatPage 类内部
struct RecentContact {
    int appid = 0;
    QString groupId;
    QString name;
    int type = 0; // 0群聊 1频道？私聊
};

struct UnifiedContact {
    int appid;
    QString id;      // 群ID 或 好友ID
    QString name;
    int type;        // 1群聊 2频道 3私聊 4频道私聊
};


// 消息列表模型
class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit MessageListModel(QObject *parent = nullptr);   // 添加这一行
    enum MessageRole {
        SenderRole = Qt::UserRole + 1,
        ContentRole,
        IsSelfRole,
        TimestampRole,
        name,
        hf,
        ch
    };
    void set_sh(const QModelIndex &index);
    void set_ch(const QModelIndex &index);
    // 往上翻加载更早的记录：插到最前面（保持原有顺序）
    void prependMessages(const QList<Message> &msgs);
    // 当前最早一条的 seq（没有则 0），给「往上滚动加载更多」当游标用
    int oldestSeq() const;
    void setMessages(QList<Message> &&msgs);
    void addMessage(const Message &msg);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void clear();
private:
    QList<Message> m_messages;

};




// 一条消息里的一张图片 —— 同一条消息可能带多张（QQ 把多图拆成多个 attachments，
// 每个都写成一个 [image,…] 标签），所以这里按「列表」而不是单张来存。
struct BubbleImage {
    QString source;      // 原始来源：本地路径 或 URL
    bool isLocal = false;
    QString localFile;   // 已落盘的缩略图文件（没有则为空）
    QRect relRect;       // 相对「图片区」左上角的矩形：排版时算好，paint 与双击命中共用同一份
};

// 气泡绘制委托
class BubbleDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit BubbleDelegate(QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    struct CachedData {
        QStringList wrappedLines;  // 换行后的文本行
        int textWidth = 0;         // 文本最大宽度
        int textHeight = 0;        // 文本总高度（不含名字和时间）
        QList<BubbleImage> images; // 这条消息的所有图片（可能多张，空=无图）
        bool hasImage = false;     // = !images.isEmpty()，保留这个字段给旧调用点
        int imagesHeight = 0;      // 图片区总高度（含行距与尾部间距，0=无图）
        int imagesTop = 0;         // 图片区相对气泡内容区顶点的 y 偏移（名字高 + 文本高 + 4）
        int bubbleWidth = 130;     // 气泡宽度（paint 与双击命中共用同一份公式）
        QString displayText;       // 纯文本（去除标签后）
        int bubbleHeight = 30;     // 气泡框高度
        int totalHeight = 42;      // item 总高度（气泡高 + self 的底部留白）
    };
    CachedData prepareMessageData(const QString &rawContent, bool isSelf, const QString &timestamp) const;

    // 预取一条消息里的图片：只在模型填充时调用，paint 里绝不再发请求/读盘
    void prefetchImages(const QString &rawContent);

    // 双击命中：pos 是视图坐标，itemRect 是该行的 visualRect()。
    // 返回命中的图片下标（对应 CachedData::images），没命中返回 -1。
    int imageIndexAt(const QModelIndex &index, const QRect &itemRect, const QPoint &pos) const;

signals:
    void imageLoaded();   // 有图片就绪，视图需要重排版

private:
    mutable QCache<QString, CachedData> m_cache; // 用消息内容作为 key
    mutable QCache<QString, QPixmap> m_avatarCache;  // 头像缓存
    mutable QCache<QString, QPixmap> m_imageCache;   // 图片缓存（key = 图片 URL）
    mutable QFont m_textFont;
    mutable QFont m_nameFont;
    mutable QFont m_timeFont;
    mutable QFontMetrics* m_textFm = nullptr;
    mutable QFontMetrics* m_nameFm = nullptr;
    mutable QFontMetrics* m_timeFm = nullptr;
    QString resolveImageFile(const QString &url) const;     // 只查内存/磁盘，绝不发请求
    void startImageDownload(const QString &url);            // 发请求，完成后 emit imageLoaded
    QPixmap imagePixmapFor(const QString &url, const QString &localFile) const; // 取图（key = URL）
    // 气泡矩形：paint 与双击命中必须走同一份几何
    QRect bubbleRectFor(const QRect &itemRect, bool isSelf, int bubbleWidth, int bubbleHeight) const;
    // 辅助：绘制占位头像
    void drawDefaultAvatar(QPainter* painter, const QRect& rect, const QString& text, bool isSelf) const;
};

class ChatPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();
    void btnsetChecked();
    void extracted(int &bufferIdx, QStringList &list,
                   QSet<QPair<int, QString>> &seen);
    void updateAllContactLists(int index);
    void addContact(int type, const MessageEvent &ev,const QString &name);
    int m_appid=0;
    int m_type=0;
    void addMessage(const Message &msg);
    PlaceholderTextEdit *inputEdit;
    QHash<QString,int> 全量群;
    QMutex m_GMutex;        // 保护 全量群：GUI 线程 / webui 线程 / 机器人线程都会访问
    QHash<QString,qint64> 最近对话;
    QString currentContactId;
    int isGroupMode=0;
    bool 存在=false;
    void onContactItemClicked2(int appid, const QString &id, int type);
    // 「日志」页右键 → 转到聊天室：切到对应分类并打开指定会话。
    // type 用的就是日志页的 tab 索引，和本页的 isGroupMode 是同一套编号：
    //   1=群聊 2=频道 3=私聊 4=频道私聊（0=事件日志，不适用）
    void openConversationFromLog(int appid, const QString &id, int type);

public slots:
    void onGroupChatClicked();
    void onPrivateChatClicked();
    void onChannelChatClicked();
    void onChannelPrivateClicked();
private slots:
    void onMessageDoubleClicked(const QModelIndex &index);
    void openDownloadedMedia(const QString &filePath); // 下载完成后打开
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:

    void onChatClicked();
    void onContactItemClicked(const QModelIndex &index);
    void onSendClicked();
    void onSendImage();
    void onSendAudio();
    void onSendVideo();
    void onSendFile();
    void showMessageContextMenu(const QPoint &pos);
    void showContactListContextMenu(const QPoint &pos);
    // 消息列表滚到顶 → 往上加载更早的聊天记录
    void onMessageScrollChanged(int value);


private:
    void initUI();
    void loadChatHistory(int appid, const QString &contactId, int type);
    void addDataToModel(int appid, const Contact& c, int type);

    // ===== 往上滚动加载更早记录（游标是「当前列表里最早一条的 seq」）=====
    void loadOlderHistory();
    void resetHistoryCursor();   // 切会话/清空列表时调用

    int     m_histBufferIdx = 0;     // g_logdb 下标（1群 2私聊 3频道 4频道私聊）
    int     m_histAppid     = 0;
    QString m_histContactId;
    int     m_histOldestSeq = 0;     // 0 = 不可用
    bool    m_histHasMore   = false; // 上一批是否装满（装满就还可能有更早的）
    bool    m_histLoading   = false; // 防止滚动事件重入

    int getmsgtype();
    void onSendmsg(QString &text);
    QPushButton *btnGroupChat, *btnPrivateChat , *btnChat,*btnChannelChat,*btnChannelPrivate,*btnRecentChat;

    QListView *msgListView;
    MessageListModel *msgModel;
    BubbleDelegate *m_bubbleDelegate = nullptr;  // 填充模型时用它预取图片
    // 双击消息时的鼠标位置（QListView::doubleClicked 只给 index，不给坐标）
    // —— 多图消息要靠它判断双击的是哪一张
    QPoint m_lastMsgPos;
    bool m_lastMsgPosValid = false;
    bool m_relayoutPending = false;              // 合并多次「图片就绪」引起的重排

    QPushButton *btnSendImage, *btnSendAudio, *btnSendVideo, *btnSendFile;
    QComboBox *comboSendType;
    QPushButton *btnSend;
    QHash<QString, QPixmap> m_avatarCache; // 用于缓存绘制好的 32x32 头像

    QLabel *titleLabel;
    QString m_msgid;

    QList<RecentContact> recentContacts; //最近
    QListView *contactList;//往里面添加新的成员
    QSet<QPair<int, QString>> seen;


};

#endif