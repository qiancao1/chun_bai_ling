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