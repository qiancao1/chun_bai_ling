#ifndef BOTSET_H
#define BOTSET_H

#include <QWidget>
#include <qlistwidget.h>

namespace Ui {
class botset;
}

class botset : public QWidget
{
    Q_OBJECT

public:
    explicit botset(QWidget *parent = nullptr);
    ~botset();
    void 列表行被单击();

private slots:
    void on_pushButton_clicked();

private:
    Ui::botset *ui;

};

#endif // BOTSET_H
