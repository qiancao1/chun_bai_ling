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
#include "QQBotClient.h"
#include "placeholdertextedit.h"   // 如果你也有 QTextEdit 的替换
#include <QCache>
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
    void setMessages(QList<Message> &&msgs);
    void addMessage(const Message &msg);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void clear();
private:
    QList<Message> m_messages;

};




// 气泡绘制委托
class BubbleDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    struct CachedData {
        QStringList wrappedLines;  // 换行后的文本行
        int textWidth;             // 文本最大宽度
        int textHeight;            // 文本总高度（不含名字和时间）
        bool hasImage;             // 是否包含图片标签
        QString imagePath;         // 图片路径（如果有）
        bool imageIsLocal;         // 是否本地图片
        QString displayText;       // 纯文本（去除标签后）
        int totalHeight;           // 整条消息的总高度（用于 sizeHint）
    };
    CachedData prepareMessageData(const QString &rawContent, bool isSelf, const QString &timestamp) const;

private:
    mutable QCache<QString, CachedData> m_cache; // 用消息内容作为 key
    mutable QCache<QString, QPixmap> m_avatarCache;  // 头像缓存
    mutable QCache<QString, QPixmap> m_imageCache;   // 图片缓存
    mutable QFont m_textFont;
    mutable QFont m_nameFont;
    mutable QFont m_timeFont;
    mutable QFontMetrics* m_textFm = nullptr;
    mutable QFontMetrics* m_nameFm = nullptr;
    mutable QFontMetrics* m_timeFm = nullptr;
    QString downloadImageIfNeeded(const QString &url) const;
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

    QHash<QString,qint64> 最近对话;
    QString currentContactId;
    int isGroupMode=0;
    bool 存在=false;
    void onContactItemClicked2(int appid, const QString &id, int type);

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


private:
    void initUI();
    void loadChatHistory(int appid, const QString &contactId, int type);
    void addDataToModel(int appid, const Contact& c, int type);

    int getmsgtype();
    void onSendmsg(QString &text);
    QPushButton *btnGroupChat, *btnPrivateChat , *btnChat,*btnChannelChat,*btnChannelPrivate,*btnRecentChat;

    QListView *msgListView;
    MessageListModel *msgModel;

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