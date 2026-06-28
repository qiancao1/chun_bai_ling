#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pystate.h>

#include "sandboxwindow.h"
#include "global.h"
#include <QtWidgets>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QPainter>

QString g_sandboxuuid;

namespace py = pybind11;


class LineNumberArea : public QWidget
{
public:
    LineNumberArea(CodeEditor *editor) : QWidget(editor), codeEditor(editor)
    {
        setContentsMargins(0,0,0,0);
    }

    QSize sizeHint() const override
    {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor *codeEditor;
};
// ---------- Python 语法高亮器 ----------
class PythonHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    PythonHighlighter(QTextDocument *parent = nullptr) : QSyntaxHighlighter(parent)
    {
        HighlightingRule rule;

        QStringList keywords = {
            "and", "as", "assert", "async", "await", "break", "class", "continue",
            "def", "del", "elif", "else", "except", "False", "finally", "for",
            "from", "global", "if", "import", "in", "is", "lambda", "None",
            "nonlocal", "not", "or", "pass", "raise", "return", "True", "try",
            "while", "with", "yield"
        };
        rule.pattern = QRegularExpression("\\b(" + keywords.join("|") + ")\\b");
        rule.format.setForeground(QColor(0xFF7F32));
        rule.format.setFontWeight(QFont::Bold);
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()");
        rule.format.setForeground(QColor(0x6A5ACD));
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("(\".*?\"|'.*?')");
        rule.format.setForeground(QColor(0xD2691E));
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("#[^\n]*");
        rule.format.setForeground(QColor(0x8A8A8A));
        rule.format.setFontItalic(true);
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("\\b[0-9]+\\b");
        rule.format.setForeground(QColor(0xB8860B));
        highlightingRules.append(rule);
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
            QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                QRegularExpressionMatch match = matchIterator.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;
};

// ---------- CodeEditor 实现 ----------
CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    lineNumberArea = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int maxLines = blockCount();
    while (maxLines >= 10) {
        maxLines /= 10;
        ++digits;
    }
    int space = 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(0xFFF0DE));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }
    setExtraSelections(extraSelections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), QColor(0xF7EFE5));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(QColor(0x8A8A8A));
            painter.drawText(0, top, lineNumberArea->width() - 4, fontMetrics().height(),
                             Qt::AlignRight, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

// 行号区域类


// ---------- SandboxWindow 实现 ----------
SandboxWindow::SandboxWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
    applyStyleSheet();

    codeEditor->setPlainText(R"(import time
import urllib.parse
import json
import base64

api = None

def get_plugin_info(uuid):
    import qiancao_sdk
    global api
    api = qiancao_sdk.QQApi(uuid)
    return {
        "name": "天气查询",
        "version": "1.0.2",
        "author": "Your Name",
        "description": "基于中国气象局 CMA 接口，查询国内城市实时天气",
        "icon": "",
        "requires": []
    }

def on_enable():
    pass

def on_disable():
    pass

def on_unload():
    pass

def on_set():
    pass

def _parse_resp(resp_str):

    try:
        data = json.loads(resp_str) if isinstance(resp_str, str) else resp_str
    except Exception as e:
        return False, None, f"响应不是合法 JSON：{str(e)}"

    success = data.get("success", False)
    if not success:
        error = data.get("error", "未知错误")
        return False, None, f"请求失败：{error}"

    # content 字段是 Base64 编码的 JSON
    try:
        content_bytes = base64.b64decode(data["content"])
        inner_data = json.loads(content_bytes.decode('utf-8'))
    except Exception as e:
        return False, None, f"解析 content 失败：{str(e)}"

    return True, inner_data, None

def query_weather(city):
    headers = {
        "Referer": "https://weather.cma.cn/web/weather/S1003.html",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36 Edg/148.0.0.0"
    }

    # ---------- 1. 搜索城市 ----------
    timestamp = int(time.time() * 1000)
    encoded_city = urllib.parse.quote(city)
    search_url = f"https://weather.cma.cn/api/autocomplete?q={encoded_city}&limit=10&timestamp={timestamp}"

    resp_str = api.http_request(search_url, method="GET", headers=headers, timeout=15)
    print("搜索原始响应:", resp_str)  # 调试

    success, search_data, err = _parse_resp(resp_str)
    if not success:
        return err

    # 处理搜索数据
    if search_data.get("code") != 0 or not search_data.get("data"):
        return f"未找到城市“{city}”，请检查名称是否正确"

    candidates = search_data["data"]
    selected_id = None
    selected_name = None

    # 精确匹配
    for item in candidates:
        parts = item.split("|")
        if len(parts) >= 4:
            cid, cname, pinyin, country = parts[0], parts[1], parts[2], parts[3]
            if country == "中国" and (cname == city or pinyin.lower() == city.lower()):
                selected_id = cid
                selected_name = cname
                break

    # 兜底：取第一个中国城市
    if not selected_id:
        for item in candidates:
            parts = item.split("|")
            if len(parts) >= 4 and parts[3] == "中国":
                selected_id = parts[0]
                selected_name = parts[1]
                break

    if not selected_id:
        return f"未找到“{city}”在国内的天气信息"

    # ---------- 2. 查询实时天气 ----------
    weather_url = f"https://weather.cma.cn/api/now/{selected_id}"
    resp_str = api.http_request(weather_url, method="GET", headers=headers, timeout=15)
    print("天气原始响应:", resp_str)  # 调试

    success, weather_data, err = _parse_resp(resp_str)
    if not success:
        return err

    if weather_data.get("code") != 0:
        return f"获取天气失败：{weather_data.get('msg', '未知错误')}"

    now = weather_data["data"]["now"]
    location = weather_data["data"]["location"]
    update_time = weather_data["data"]["lastUpdate"]

    info = (
        f"📍 {location['name']}（{location['path']}）\n"
        f"🌡️ 温度：{now['temperature']}°C\n"
        f"💧 湿度：{now['humidity']}%\n"
        f"🌬️ 风向：{now['windDirection']} {now['windScale']}（{now['windSpeed']} m/s）\n"
        f"🤔 体感温度：{now['feelst']}°C\n"
        f"☔ 降水量：{now['precipitation']} mm\n"
        f"⏱️ 数据时间：{update_time}"
    )
    return info

def on_message(msg):
    if not hasattr(msg, 'msg') or not isinstance(msg.msg, str):
        return
    text = msg.msg.strip()
    if not text.startswith("天气"):
        return
    city = text[2:].strip()
    if not city:
        return "请指定城市名，例如：天气 南宁"
    return query_weather(city)
)");
    new PythonHighlighter(codeEditor->document());
}

SandboxWindow::~SandboxWindow() {}


void SandboxWindow::setupUI()
{


    QWidget *central = new QWidget(this);
    //central->setObjectName("centralRoot");
    setCentralWidget(central);

    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // -------------------- 左侧区域（不变） --------------------
    QWidget *leftWidget = new QWidget;
    leftWidget->setObjectName("sideBar");
    leftWidget->setMinimumWidth(500);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    leftLayout->setSpacing(10);

    QHBoxLayout *titleLayout = new QHBoxLayout;
    QLabel *pyLabel = new QLabel("</> Py 测试代码");
    pyLabel->setStyleSheet("font-weight: bold; font-size: 16px; color: #FF7F32;");
    QLabel *subLabel = new QLabel("沙盒测试 | 执行 python 代码模拟收发");
    subLabel->setStyleSheet("font-size: 11px; color: #8A8A8A;");
    titleLayout->addWidget(pyLabel);
    titleLayout->addWidget(subLabel);
    titleLayout->addStretch();
    leftLayout->addLayout(titleLayout);

    codeEditor = new CodeEditor;
    codeEditor->setFont(QFont("Consolas", 12));
    codeEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    codeEditor->setStyleSheet(
        "QPlainTextEdit {"
        "   border: 1px solid #E7D9C8;"
        "   border-radius: 8px;"
        "   background: #FFFFFF;"
        "}"
        );
    leftLayout->addWidget(codeEditor, 3);

    QLabel *outputLabel = new QLabel("📟 输出 (print / stderr)");
    outputLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #263241; margin-top: 6px;");
    leftLayout->addWidget(outputLabel);

    outputLog = new QTextEdit;
    outputLog->setReadOnly(true);
    outputLog->setFont(QFont("Consolas", 10));
    outputLog->setStyleSheet(
        "QTextEdit {"
        "   background: #F9F9F9;"
        "   border: 1px solid #E7D9C8;"
        "   border-radius: 8px;"
        "   padding: 6px;"
        "}"
        );
    outputLog->setMaximumHeight(200);
    leftLayout->addWidget(outputLog, 1);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    QPushButton *saveBtn = new QPushButton("保存");
    QPushButton *openBtn = new QPushButton("打开");
    QPushButton *savePluginBtn = new QPushButton("保存到插件");
    buttonLayout->addWidget(saveBtn);
    buttonLayout->addWidget(openBtn);
    buttonLayout->addWidget(savePluginBtn);
    buttonLayout->addStretch();
    clearBtn = new QPushButton("🗑️ 清空对话");
    buttonLayout->addWidget(clearBtn);
    leftLayout->addLayout(buttonLayout);

    // -------------------- 右侧：气泡聊天区域 --------------------
    QWidget *rightWidget = new QWidget;
    rightWidget->setObjectName("contentWidget");
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(10);
    rightWidget->setMaximumWidth(400);

    QLabel *simLabel = new QLabel("📱 模拟对话（气泡模式）");
    simLabel->setStyleSheet("font-weight: bold; font-size: 16px; color: #FF7F32;");
    rightLayout->addWidget(simLabel);

    // 聊天显示区域（QListWidget 实现气泡列表）
    chatWidget = new QListWidget;
    chatWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chatWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatWidget->setSelectionMode(QAbstractItemView::NoSelection);      // 只复制文本，不选中 item
    chatWidget->setStyleSheet(
        "QListWidget {"
        "   background: #FFFFFF;"
        "   border: 1px solid #E7D9C8;"
        "   border-radius: 10px;"
        "   padding: 8px;"
        "}"
        "QListWidget::item { padding: 4px 0px; }"
        );
    rightLayout->addWidget(chatWidget, 1);

    // 输入区域：多行输入框 + 清空按钮 + 发送按钮

    // 创建多行输入框
    messageInput = new QTextEdit;
    messageInput->setPlaceholderText("输入消息（Ctrl+Enter 发送 / 点击发送按钮）...");
    messageInput->setFixedHeight(80);
    messageInput->setStyleSheet(
        "QTextEdit {"
        "   border: 1px solid #E7D9C8;"
        "   border-radius: 8px;"
        "   padding: 6px;"
        "   font-size: 12px;"
        "}"
        );
    messageInput->setFont(QFont("Segoe UI", 11));
    messageInput->installEventFilter(this);
    // 创建两个按钮

    QPushButton *sendBtn = new QPushButton("发送");
    QHBoxLayout *inputLayout = new QHBoxLayout;
    inputLayout->addWidget(messageInput, 1);   // 1 表示输入框会占据所有多余宽度

    inputLayout->addWidget(sendBtn);
    rightLayout->addLayout(inputLayout);

    mainLayout->addWidget(leftWidget, 1);
    mainLayout->addWidget(rightWidget, 1);

    // 连接信号
    connect(saveBtn, &QPushButton::clicked, this, &SandboxWindow::onSaveCodeClicked);
    connect(openBtn, &QPushButton::clicked, this, &SandboxWindow::onOpenCodeClicked);
    connect(savePluginBtn, &QPushButton::clicked, this, &SandboxWindow::onSavePluginClicked);
    connect(sendBtn, &QPushButton::clicked, this, &SandboxWindow::onSendClicked);
    connect(clearBtn, &QPushButton::clicked, this, &SandboxWindow::clearChat);
    // 支持 Ctrl+Enter 发送
    connect(messageInput, &QTextEdit::textChanged, this, [this]() { /* 可选: 动态调整高度 */ });
    // 实际 Ctrl+Enter 需要重写 keyPressEvent，这里简单用快捷键方式
    QShortcut *sendShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(sendShortcut, &QShortcut::activated, this, &SandboxWindow::onSendClicked);
}
bool SandboxWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == messageInput && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() == Qt::ControlModifier) {
                onSendClicked();
                return true;
            } else {
                return false;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
// -------------------- 添加气泡消息 --------------------
void SandboxWindow::addChatMessage(const QString &text, bool isUser)
{
    if (text.trimmed().isEmpty()) return;
    QString newtext = text+"\n";
    QWidget *bubbleWidget = new QWidget;
    QHBoxLayout *bubbleLayout = new QHBoxLayout(bubbleWidget);
    bubbleLayout->setContentsMargins(4, 4, 4, 4);
    bubbleLayout->setSpacing(4);

    QLabel *bubbleLabel = new QLabel(newtext);
    bubbleLabel->setWordWrap(true);
    bubbleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);  // 允许选中复制文本
    bubbleLabel->setMaximumWidth(400);
    bubbleLabel->setStyleSheet(
        QString("QLabel {"
                "   padding: 10px 14px;"
                "   border-radius: 18px;"
                "   background-color: %1;"
                "   color: %2;"
                "   font-size: 12px;"
                "}")
            .arg(isUser ? "#DCF8C6" : "#D3A9A2","#000000")

        );
    bubbleLabel->setFont(QFont("Segoe UI", 11));

    if (isUser) {
        // 用户消息：右对齐
        bubbleLayout->addStretch();
        bubbleLayout->addWidget(bubbleLabel);
        bubbleLayout->setAlignment(bubbleLabel, Qt::AlignRight);
    } else {
        // 机器人消息：左对齐
        bubbleLayout->addWidget(bubbleLabel);
        bubbleLayout->addStretch();
        bubbleLayout->setAlignment(bubbleLabel, Qt::AlignLeft);
    }

    QListWidgetItem *item = new QListWidgetItem(chatWidget);
    item->setSizeHint(bubbleWidget->sizeHint());
    chatWidget->setItemWidget(item, bubbleWidget);
    chatWidget->scrollToBottom();
}

// -------------------- 清空所有气泡 --------------------
void SandboxWindow::clearChat()
{
    outputLog->clear();
    chatWidget->clear();
}

void SandboxWindow::applyStyleSheet()
{
    setStyleSheet(R"(
        QMainWindow { background: transparent; }
        QWidget#centralRoot { background: #f7efe5; border-radius: 10px; }
        QWidget { color: #263241; font-family: "Segoe UI", "Microsoft YaHei", sans-serif; font-size: 13px; }
        QWidget#sideBar { background: #FEFEFC; border-right: 1px solid #F4E8DA; border-top-right-radius: 10px; }
        QWidget#contentWidget { background: #fff8ef; border-radius: 18px; }
        QPushButton { background: #FFF0DE; color: #FF7F32; font-weight: 600; border-radius: 8px; padding: 6px 16px; }
        QPushButton:hover { background: #FFE5C8; }
        QPushButton:pressed { background: #FFD7A8; }
        QLineEdit { background: #F9F9F9; border: 1px solid #F2E8DE; border-radius: 10px; padding: 8px 14px; }
        QLineEdit:focus { border: 1px solid #FFB066; background: #FFFFFF; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px; }
        QScrollBar::handle:vertical { background: #E7D9C8; border-radius: 4px; min-height: 40px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
    )");
}


// 槽函数
void SandboxWindow::onSendClicked()
{
    // 1. 获取用户输入的消息文本
    QString userInput = messageInput->toPlainText().trimmed();
    if (userInput.isEmpty())
        return;
    addChatMessage(userInput, true);
    // 2. 获取代码编辑器中的 Python 脚本
    QString pythonScript = codeEditor->toPlainText();
    if (pythonScript.trimmed().isEmpty()) {
        appendOutput("[错误] 代码编辑器为空，请先编写 Python 代码并定义 on_message(msg) 函数");
        return;
    }

    // 3. 构造 MessageEvent 对象，只填充 content 为用户输入，其余字段使用固定测试值
    MessageEvent event;
    event.msg = userInput;
    event.groupId = "test_group_123";       // 固定测试群ID
    event.user = "user_456";            // 固定发送人ID
    event.msgId = "msg_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    event.seq = QDateTime::currentSecsSinceEpoch();
    event.appid = 1001;
    event.user_int = 456;
    event.type = 0;                         // 0=群聊
    event.subType = 0;
    event.callbackType = 0;
    event.fullType = false;
    event.nickname = "测试用户";
    event.guildId = "";
    event.msgType = "GROUP_AT_MESSAGE_CREATE";
    event.extra = "";
    event.raw = "";
    event.callbackId = "";
    event.replyTo = "";


    messageInput->clear();

    // 5. 如果已有执行线程在运行，则提示并返回
    if (m_isRunning) {
        appendOutput("[警告] 已有脚本正在执行，请稍后再试");
        return;
    }

    m_isRunning = true;
    m_execThread = QThread::create([this, pythonScript, event]() {
        py::gil_scoped_acquire gil;

        auto sys = py::module_::import("sys");
        auto io = py::module_::import("io");
        py::object oldStdout = sys.attr("stdout");
        py::object oldStderr = sys.attr("stderr");
        py::object stdoutStr = io.attr("StringIO")();
        py::object stderrStr = io.attr("StringIO")();
        sys.attr("stdout") = stdoutStr;
        sys.attr("stderr") = stderrStr;

        py::dict global = py::globals();
        global["__builtins__"] = py::module_::import("builtins");
        global["__name__"] = "__main__";


        if (!pyfilepath.isEmpty()) {
            QFileInfo info(pyfilepath);
            QString scriptDir = info.absolutePath();  // 获取目录，不含文件名
            QString scriptPath = info.absoluteFilePath(); // 获取规范化的完整路径

            std::string script_dir = scriptDir.toStdString();
            std::string script_path = scriptPath.toStdString();

            sys.attr("path").attr("append")(script_dir);
            global["__file__"] = script_path;
        }
        int exitCode = 0;
        QString errorMessage;
        QString resultOutput;

        try {
            py::exec(pythonScript.toStdString(), global);

            if (!global.contains("on_message")) {
                throw std::runtime_error("脚本中未找到 on_message(msg) 函数");
            }
            py::module_::import("qq_api");


            if (!global.contains("get_plugin_info")) {
                throw std::runtime_error("脚本中未找到 get_plugin_info(uuid) 函数");
            }
            int randomValue = QRandomGenerator::global()->bounded(100000, 100000000 + 1);
            g_sandboxuuid=QString::number(randomValue);


            py::object info_json = global["get_plugin_info"](py::str(g_sandboxuuid.toStdString()));
            py::object result = global["on_message"](event);

            // ⑤ 处理返回值（如果是字符串则作为机器人回复）
            if (!result.is_none()) {
                try {
                    resultOutput = QString::fromStdString(py::str(result).cast<std::string>());
                } catch (...) {
                    resultOutput = QString::fromStdString(py::repr(result).cast<std::string>());
                }
            }

        } catch (py::error_already_set& e) {
            exitCode = 1;
            errorMessage = QString::fromStdString(e.what());
        } catch (const std::exception& e) {
            exitCode = 1;
            errorMessage = e.what();
        }
        g_sandboxuuid = QString();
        // 获取捕获的 stdout / stderr
        auto getOutput = [&](py::object& obj) -> QString {
            obj.attr("seek")(0);
            py::str s = py::cast<py::str>(obj.attr("read")());
            return QString::fromStdString(s.cast<std::string>());
        };
        QString outText = getOutput(stdoutStr);
        QString errText = getOutput(stderrStr);

        // 恢复原始 stdout/stderr
        sys.attr("stdout") = oldStdout;
        sys.attr("stderr") = oldStderr;

        // 回到主线程更新 UI
        QMetaObject::invokeMethod(this, [this, exitCode, outText, errText, errorMessage, resultOutput]() {
            // 显示 print 输出到输出框
            if (!outText.isEmpty())
                outputLog->append(outText);
            if (!errText.isEmpty())
                outputLog->append("[stderr] " + errText);

            if (exitCode != 0) {
                outputLog->append("[错误] 脚本执行或函数调用失败");
                if (!errorMessage.isEmpty())
                    outputLog->append("[错误] " + errorMessage);
            }
             addChatMessage(resultOutput, false);
            // 清理线程状态
            m_isRunning = false;
            m_execThread->deleteLater();
            m_execThread = nullptr;
        }, Qt::QueuedConnection);
    });

    m_execThread->start();
}

void SandboxWindow::onSavePluginClicked()
{
    if (codeEditor->toPlainText().trimmed().isEmpty()) {
        QMessageBox::information(this, "提示", "代码内容为空，无法导出插件！");
        return;
    }


    QDir appDir(QCoreApplication::applicationDirPath());
    QString pluginBasePath = appDir.filePath("plugin");

    if (!QDir(pluginBasePath).exists()) {
        if (!appDir.mkdir("plugin")) {
            QMessageBox::warning(this, "错误", "无法创建 plugin 目录！");
            return;
        }
        outputLog->append("[信息] 已创建 plugin 目录");
    }
    QString targetDir;
    while (true) {

        targetDir = QFileDialog::getExistingDirectory(
            this,
            "选择插件目录（将在其中创建 main.py）",
            pluginBasePath,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
            );

        if (targetDir.isEmpty()) {
            outputLog->append("[信息] 取消了插件导出");
            return;
        }
        QDir possibleDir(targetDir);
        QDir pluginDir(pluginBasePath);
        if (!possibleDir.absolutePath().startsWith(pluginDir.absolutePath())) {
            QMessageBox::warning(this, "错误", "选择的目录不在 plugin 文件夹下，请重新选择。");
            continue;
        }

        if (QDir(targetDir) == pluginDir) {
            QMessageBox::warning(this, "错误", "请选择一个具体的插件文件夹（plugin 的子目录），而不是 plugin 根目录。");
            continue;
        }
        QString mainPyPath = QDir(targetDir).filePath("main.py");
        if (QFile::exists(mainPyPath)) {
            QMessageBox::StandardButton btn = QMessageBox::question(
                this,
                "文件已存在",
                "main.py 已存在，是否覆盖？",
                QMessageBox::Yes | QMessageBox::No
                );
            if (btn != QMessageBox::Yes)
                continue;
        }
        break;
    }

    QString mainPyPath = QDir(targetDir).filePath("main.py");
    QFile file(mainPyPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法写入文件：" + mainPyPath);
        return;
    }

    QTextStream stream(&file);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));
    stream << codeEditor->toPlainText();
    file.close();
    targetDir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath())+"/");
    outputLog->append(QString("[信息] 已导出插件至 %1/main.py").arg(targetDir));

    // 6. 询问是否载入/重载插件
    int ret = QMessageBox::question(
        this,
        "插件管理",
        QString("插件已导出至 %1/main.py\n是否立即[载入|重载]插件？").arg(targetDir),
        QMessageBox::Yes | QMessageBox::No
        );

    if (ret == QMessageBox::Yes) {
        int index = pluginPage->findPluginIndex(targetDir);
        if(index!=-1)
        {
            if(pluginPage->Reload_Plugin(index))return;
            outputLog->append("[载入插件]重载失败..一般来说py插件不可能重载失败");
            return;
        }
        QList<int> arr{};
        QString err = pluginPage->LoadPlugin(targetDir,0,false,arr);
        if(err.isEmpty()) return;
        outputLog->append("[载入插件]" + err);
    } else {
        outputLog->append("[信息] 跳过插件载入");
    }
}

void SandboxWindow::onSaveCodeClicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "保存 Python 代码", QString(), "Python Files (*.py)");
    if (fileName.isEmpty())
        return;
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << codeEditor->toPlainText();
        file.close();
        QMessageBox::information(this, "成功", "代码已保存至 " + fileName);
    }
}

void SandboxWindow::onOpenCodeClicked()
{
    pyfilepath = QFileDialog::getOpenFileName(this, "打开 Python 代码", QString(), "Python Files (*.py)");
    if (pyfilepath.isEmpty())
        return;

    QFile file(pyfilepath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        codeEditor->setPlainText(in.readAll());
        file.close();
    }
}




void SandboxWindow::appendOutput(const QString &text)
{
    QMetaObject::invokeMethod(this, [this, text]() {
        outputLog->append(text);
        QScrollBar *sb = outputLog->verticalScrollBar();
        sb->setValue(sb->maximum());
    }, Qt::QueuedConnection);
}

void SandboxWindow::clearOutput()
{
    QMetaObject::invokeMethod(outputLog, &QTextEdit::clear, Qt::QueuedConnection);
}

void SandboxWindow::setCode(const QString &code)
{
    codeEditor->setPlainText(code);
}

QString SandboxWindow::getCode() const
{
    return codeEditor->toPlainText();
}


#include "sandboxwindow.moc"