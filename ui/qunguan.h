#ifndef QUNGUAN_H
#define QUNGUAN_H

#include <QWidget>
#include <qlistwidget.h>

namespace Ui {
class qunguan;
}

class qunguan : public QWidget
{
    Q_OBJECT

public:
    explicit qunguan(QWidget *parent = nullptr);
    ~qunguan();
    void 列表行被单击();

private slots:
    void on_pushButton_clicked();
    void onItemClicked(QListWidgetItem *item);



private:
    void refreshList();
    QString m_currentField;
    Ui::qunguan *ui;
};

#endif // QUNGUAN_H
