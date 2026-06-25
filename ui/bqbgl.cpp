#include "bqbgl.h"
#include "global.h"
#include "ui_bqbgl.h"
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QListWidgetItem>
#include <QMessageBox>

bqbgl::bqbgl(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::bqbgl)
{
    ui->setupUi(this);
    // 构造函数中调用一次，初始化列表

}

bqbgl::~bqbgl()
{
    delete ui;
}

// 枚举 image/ 目录下的图片文件，存入 m_imageFiles，并刷新列表
QString bqbgl::meiju()
{
    QDir dir("image/");   // 这里使用 "image/" 目录，你可根据实际修改
    if (!dir.exists()) {
        // 目录不存在，可以尝试创建或者提示
        dir.mkpath(".");
        return QString();
    }

    // 设置图片过滤器（支持常见格式）
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.gif";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files);

    // 获取文件列表（仅文件名）
    QStringList m_image = dir.entryList();

    ui->listWidget->clear();
    ui->listWidget->addItems(m_image);

    return m_image.join(",");

}


void bqbgl::on_listWidget_itemClicked(QListWidgetItem *item)
{
    if (!item) return;

    QString fileName = item->text();
    QString filePath = "image/" + fileName;   // 构建完整路径
    QPixmap pixmap(filePath);

    if (!pixmap.isNull()) {
        ui->label->setPixmap(pixmap.scaled(ui->label->size(), Qt::KeepAspectRatio));
    } else {
        ui->label->clear();  // 加载失败则清空
    }
}

void bqbgl::on_pushButton_clicked()
{
    QListWidgetItem *currentItem = ui->listWidget->currentItem();
    if (!currentItem) {
        QMessageBox::information(this, "提示", "请先选择一张图片");
        return;
    }

    QString fileName = currentItem->text();
    QString filePath = "image/" + fileName;

    QFile file(filePath);
    if (file.remove()) {
        delete currentItem;
        ui->label->clear();
    } else {
        QMessageBox::warning(this, "错误", "删除文件失败：" + file.errorString());
    }
}
extern QString 拟人人设;
extern QString 拟人人设2;
extern QString 拟人人设_私聊;
extern QString 拟人人设1;

void bqbgl::on_pushButton_2_clicked()
{

    拟人人设=subTextReplace(拟人人设2,"【表情包】","【表情包】"+meiju());
    拟人人设_私聊=subTextReplace(拟人人设1,"【表情包】","【表情包】"+meiju());
}