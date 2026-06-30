#ifndef AIFUJIA_H
#define AIFUJIA_H

#include "AccountInfo.h"

#include <QWidget>
#include <qtablewidget.h>
struct fujia{
    QString name;
    QString role;
    QString model;
};

struct ModelData {
    QString name;
    QList<int> enabledInterfaceIndices; // 全局接口列表中的索引
};

namespace Ui {
class Aifujia;
}

class Aifujia : public QWidget
{
    Q_OBJECT

public:
    explicit Aifujia(QWidget *parent = nullptr);
    ~Aifujia();
    QString fujia_jy(QString &zl, QString &role);
    void initmode(QList<ModelData> &modelist);
    void initdata(AccountInfo *acc);

private slots:
    void on_add_row_clicked();

    void on_removs_row_clicked();

    void on_save_data_clicked();


    void on_tableWidget_itemChanged(QTableWidgetItem *item);

    void on_tableWidget_itemClicked(QTableWidgetItem *item);

private:

    void load_data();
    void save_data();
    Ui::Aifujia *ui;
    QList<fujia> fujiaList;
};

#endif // AIFUJIA_H
