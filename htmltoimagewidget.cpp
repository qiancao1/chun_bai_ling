#include "htmltoimagewidget.h"
#include <QMessageBox>
#include <QFile>
#include <QApplication>
#include <QPixmap>
#include <QVBoxLayout>
#include <qbuttongroup.h>
#include "global.h"

// 你的渲染函数声明（如果单独头文件，则包含即可）
QString renderInThread(const QString &htmlContent, int width = 400);

// 测试用的 HTML 源码
QString htmlTest = R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
</head>
<body>
    <h1 style="color:#2c3e50;">一级标题</h1>
    <h2 style="color:#e74c3c;">二级标题（红色）</h2>
    <p style="font-size:16px; color:#333;">这是一段普通文本，包含 <b>加粗</b> 和 <i>斜体</i> 以及 <span style="color:#3498db;">蓝色文字</span>。</p>

    <ul>
        <li>无序列表项 1</li>
        <li style="color:#27ae60;">无序列表项 2（绿色）</li>
        <li>无序列表项 3</li>
    </ul>

    <blockquote style="border-left:4px solid #ccc; padding-left:10px; color:#555;">
        这是一个引用块，可以多行。
    </blockquote>

    <pre style="background:#f4f4f4; padding:10px; border-radius:4px;">
        <code>
// 这是一段代码
int main() {
    printf("Hello, World!");
    return 0;
}
        </code>
    </pre>
    <p>
        <img src="C:/Users/Airuan/Pictures/AI绘画/下载.png" width="128" alt="本地图片" />
    </p>

    <p style="text-align:center; color:#999; font-size:12px;">底部小字，居中</p>
</body>
</html>
)";

QString htmlTest2 = R"(<div style="max-width:600px; margin:20px auto; padding:30px; border-radius:16px; background:linear-gradient(135deg, #667eea 0%, #764ba2 100%); color:white; font-family: Arial, sans-serif;">
    <h1 style="margin:0;">✨ 卡片</h1>
    <p style="font-size:18px; opacity:0.9;">使用渐变背景和圆角，测试复杂样式渲染。</p>
    <ul style="list-style:none; padding:0;">
        <li>✅ 列表项 1</li>
        <li>✅ 列表项 2</li>
        <li>✅ 列表项 3</li>
    </ul>
</div>
)";
HtmlToImageWidget::HtmlToImageWidget(QWidget *parent)
    : QWidget(parent)
{
    // ---------- 创建控件 ----------
    // 源码编辑器
    htmlEditor = new QTextEdit(this);
    htmlEditor->setPlaceholderText("在此粘贴 HTML 源码...");
    htmlEditor->setLineWrapMode(QTextEdit::NoWrap);
    htmlEditor->setPlainText(htmlTest);

    // 预览区（只读）
    imagePreview = new QTextEdit(this);
    imagePreview->setLineWrapMode(QTextEdit::NoWrap);
    imagePreview->setReadOnly(true);

    // 单选按钮（互斥）
    radioLite = new QRadioButton("精简版", this);
    radioFull = new QRadioButton("完整版", this);
    radioLite->setChecked(true);

    QButtonGroup *group = new QButtonGroup(this);

    group->addButton(radioLite);
    group->addButton(radioFull);

    // 生成按钮
    genBtn = new QPushButton("生成", this);
    QPushButton *genBtn2 = new QPushButton("精简版-例子", this);
    QPushButton *genBtn3 = new QPushButton("完整版-例子", this);
    // ---------- 布局 ----------
    // 顶部工具栏（左对齐）
    QHBoxLayout *toolBar = new QHBoxLayout;
    toolBar->addWidget(radioLite);
    toolBar->addWidget(radioFull);
    toolBar->addWidget(genBtn);
    toolBar->addWidget(genBtn2);
    toolBar->addWidget(genBtn3);
    toolBar->addStretch();       // 按钮靠左

    // 主内容区域（左右两栏）
    QHBoxLayout *contentLayout = new QHBoxLayout;
    contentLayout->addWidget(htmlEditor, 2);
    contentLayout->addWidget(imagePreview, 2);

    // 总布局（垂直排列）
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(toolBar);
    mainLayout->addLayout(contentLayout);

    connect(genBtn, &QPushButton::clicked, this, &HtmlToImageWidget::onGenerate);
    connect(genBtn2, &QPushButton::clicked, [this](){
        htmlEditor->setPlainText(htmlTest);
    });
    connect(genBtn3, &QPushButton::clicked, [this](){
        htmlEditor->setPlainText(htmlTest2);
    });
    connect(htmlEditor, &QTextEdit::textChanged, this, [this]() {
        if(!radioLite->isChecked()) return;
        QString text = htmlEditor->toPlainText();
        if (text.trimmed().startsWith("<!DOCTYPE html>", Qt::CaseInsensitive)) {
            imagePreview->setHtml(text);
        } else {
            text.replace("\n","\n\n");
            imagePreview->setMarkdown(text);
        }
    });
    onGenerate();
}

class ___htmltoimg : public QRunnable {
public:
    ___htmltoimg(const QString &text):m_text(text) {}
    void run() override {
        QByteArray data = ScreenA->captureHtmlSync(m_text);
        QString path = "tmp/image/htmltoimg.png";
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            QMetaObject::invokeMethod(qApp, []() {
                QMessageBox::warning(nullptr, "打开文件失败", "打开文件写出图片失败");
            }, Qt::QueuedConnection);
            return;
        }
        file.write(data);
        file.close();

        QMetaObject::invokeMethod(qApp, [this, path]() {
            htmltoimg->imagePreview->setText(
                QString("<img src=\"%1\" width=\"%2\" alt=\"本地图片\" />").arg(path).arg(htmltoimg->imagePreview->width())
                );
        }, Qt::QueuedConnection);
    }
private:
    QString m_text;
};

void HtmlToImageWidget::onGenerate()
{
    QString text = htmlEditor->toPlainText();

    if (radioLite->isChecked()) {
        imagePreview->setText(text);
    } else {
        auto *task = new ___htmltoimg(text);
        QThreadPool::globalInstance()->start(task);


    }
}