#ifndef BQBGL_H
#define BQBGL_H

#include <QWidget>
#include <QStringList>
#include <qlistwidget.h>

QT_BEGIN_NAMESPACE
namespace Ui { class bqbgl; }
QT_END_NAMESPACE

class bqbgl : public QWidget
{
    Q_OBJECT

public:
    bqbgl(QWidget *parent = nullptr);
    ~bqbgl();
    QString meiju();  // 扫描目录，填充列表和m_imageFiles

private slots:
    void on_listWidget_itemClicked(QListWidgetItem *item);
    void on_pushButton_clicked();
    void on_pushButton_2_clicked();

private:


    Ui::bqbgl *ui;

};

#endif // BQBGL_H