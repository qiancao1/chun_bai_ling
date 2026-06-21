#ifndef HTMLTOIMAGEWIDGET_H
#define HTMLTOIMAGEWIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <qradiobutton.h>

class HtmlToImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HtmlToImageWidget(QWidget *parent = nullptr);
    QTextEdit *imagePreview;

private:
    void onGenerate();
    QTextEdit   *htmlEditor;
    QRadioButton *radioLite,*radioFull;
    QPushButton *genBtn;
};

#endif // HTMLTOIMAGEWIDGET_H