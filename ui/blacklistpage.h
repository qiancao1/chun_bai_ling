// blacklistpage.h
#ifndef BLACKLISTPAGE_H
#define BLACKLISTPAGE_H

#include <QWidget>
#include <QTableWidget>
#include <QHash>
#include <QString>

class QLabel;

// 黑名单现在按账号隔离：原来全局只有一份 data/blacklist.json，
// 多个机器人共用会串用户 ID（同一个 openid 在不同机器人下含义不同）。
// 存储改成 data/blacklist_<appid>.json，格式仍是 QDataStream 序列化的 QHash。
namespace Blacklist {
    QString filePath(int appid);                        // data/blacklist_<appid>.json
    QHash<QString, QString> &data(int appid);           // 取某账号的黑名单（首次访问从磁盘加载）
    bool save(int appid);                               // 落盘（appid<=0 直接返回 true）
    bool contains(int appid, const QString &id);        // 消息过滤用
    void forget(int appid);                             // 丢弃内存缓存（下次重新从磁盘读）
}

class BlacklistPage : public QWidget
{
    Q_OBJECT

public:
    explicit BlacklistPage(QWidget *parent = nullptr);
    ~BlacklistPage();

    void setAppId(int appid);            // 切换账号：换文件、重载、刷表
    int appId() const { return m_appid; }

    bool saveToFile();                   // 保存当前账号的黑名单到磁盘

    // 给 WebUI 用：黑名单在磁盘上是 QDataStream 序列化的 QHash（不是 JSON），
    // 网页那边没法直接读写，所以由这里做「JSON ↔ QHash + 落盘 + 刷新表格」的桥。
    QHash<QString, QString> webuiSnapshot(int appid) const;              // ID → 备注
    bool webuiApply(int appid, const QHash<QString, QString> &data);     // 整体替换并落盘

private slots:
    void onAddClicked();
    void onDeleteClicked();
    void onItemChanged(QTableWidgetItem *item);

private:
    void setupUI();
    void refreshTable();                 // 从哈希表刷新表格


    QTableWidget *m_table = nullptr;
    QLabel *m_titleLabel = nullptr;
    int m_appid = 0;                     // 当前显示的是哪个机器人的黑名单

};

#endif // BLACKLISTPAGE_H
