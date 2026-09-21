// blacklistpage.h
#ifndef BLACKLISTPAGE_H
#define BLACKLISTPAGE_H

#include <QWidget>
#include <QTableWidget>
#include <QHash>

class BlacklistPage : public QWidget
{
    Q_OBJECT

public:
    explicit BlacklistPage(QWidget *parent = nullptr);
    ~BlacklistPage();
    bool saveToFile();                   // 保存黑名单到磁盘

    // 给 WebUI 用：黑名单在磁盘上是 QDataStream 序列化的 QHash（不是 JSON），
    // 网页那边没法直接读写，所以由这里做「JSON ↔ QHash + 落盘 + 刷新表格」的桥。
    QHash<QString, QString> webuiSnapshot() const;                 // ID → 备注
    bool webuiApply(const QHash<QString, QString> &data);          // 整体替换并落盘

private slots:
    void onAddClicked();
    void onDeleteClicked();
    void onItemChanged(QTableWidgetItem *item);

private:
    void setupUI();
    void refreshTable();                 // 从哈希表刷新表格
    bool loadFromFile();                 // 从磁盘加载黑名单


    QTableWidget *m_table;

};

#endif // BLACKLISTPAGE_H