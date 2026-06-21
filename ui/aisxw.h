#ifndef AISXW_H
#define AISXW_H

#include <QWidget>
#include <qlistwidget.h>

namespace Ui {
class aisxw;
}

class aisxw : public QWidget
{
    Q_OBJECT

public:
    explicit aisxw(QWidget *parent = nullptr);
    ~aisxw();

private slots:
    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();

    void on_pushButton_clicked();

    void on_listWidget_itemClicked(QListWidgetItem *item);

private:
    QString m_openid;
    Ui::aisxw *ui;
};

#endif // AISXW_H
