#include "AppWindow.h"
#include "PythonParser.h"
#include "global.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStatusBar>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qscrollbar.h>

#include <QDialog>

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
bool g_不审核=false;

QString g_system=R"(1.注意 你可以直接调用读写工具 请勿输出要用户手动复制 注意本地没main.py文件时 你要生成一个main.py文件
2.你主要内容 帮助用户 编写插件代码,编写代码最好的方式是 每个功能都单独整一个py 文件
3.你可以使用读写功能修改某个文件 或查看 注意写出目录只能设置的路径 你写出只需要 xxx.py即可
4.当用户要求你编码插件时,你直接写到文件,不需要输出要用户复制,当用户提出某个功能有问题时 你可以读取文件 并且写出
5.用户可能并不喜欢 /开头的指令 你可以先询问再 一般来说不会以/开头
6.你可以需要 在main.py中 get_plugin_info 的 description 包含指令 因为并不是所有用户都看得懂代码 所以直接说明指令 当然 description 支持md格式文本 是 QTextEdit
7.由于机器人 发送 是md格式 你可以在回复文本 添加 # ``` *** ![]() >  []() 等语法 注意[显示文本](点击后文本)语法是可点击按钮 例子 [开始游戏](加入谁是卧底) 当然 可以[开始游戏]() 圆括号内容不写内容 自动用[]内容
8.当前系统环境是 win环境
9.请在 返回的文本中 多添加[指令]() 等文本 因为在QQ点击这个标签可以快捷插入聊天框
10.请注意 插件可能是在 多群环境运行 请根据用户说的类型来决定是 分群玩还是 分个人玩
11.请分多个py文件 写出 因为 当某个文件需要修改 如果内容太多就很麻烦 最好一个函数一个文件 这个由你决定
12.如果引用到第三方库 将数据写到 requirements.txt 文件 没有就自己创建
13.使用下面ButtonGroup 类创建按钮 api.send_messageEx(msg,回复内容+ButtonGroup.to_json()) 发送按钮
========
#当前文件目录！！！ 请不要执行cmd查看目录 这里有实时文件目录 请勿读取 ai对话.json 这个是你的上下文存储文件
注意 格式是 文件名 (文件大小) 读取就读文件名即可 文件大小你看看就知道了
=======
{{文件目录}}
========
消息结构体
# 当接收到消息事件时，event 对象包含以下字段（均为可读写属性）：
# msg.groupid      : 群ID 发送消息无条件使用这个字段 私聊环境也可用传这个参数 包括 频道 和 频道私聊 因为可用让代码同时支持 各种事件来源(字符串)
# msg.user         : 发送者标识 32字节hex(字符串)
# msg.msgid        : 本条消息的唯一 ID（字符串）
# msg.msg          : 消息内容 里面包含[image,name=xxx,url=xxx] 另外还有 语音[audio,name=xx,url=xx] 视频[video,name=xx,url=xx] 文件[file,name=xx,url=xx]等标签 (字符串)
# msg.member_role  : 发送人权限 0群主 1管理员 2群员 (整数)
# msg.appid        : 应用/机器人 ID(整数)
# msg.user_id      : 用户 ID（整数）
# msg.type         : 事件类型（如群聊、私聊等）0群聊 1判断 2私聊 3判断私聊(整数)
# msg.nickname     : 发送者昵称
# msg.guildId      : 频道/服务器 ID（仅频道消息有效）(字符串)
# msg.at_you       : 布尔值，是否 @ 了当前机器人 (bool)
# msg.raw          : 原始数据（JSON 字符串） (字符串)
# msg.callbackid   : 回调 ID（用于匹配异步回调） (字符串)
# msg.replyto      : 回复目标消息 ID（若本条为回复消息）  是个标签 使用方式 api.send_messageEx(msg,msg.replyto+回复内容) (字符串)
========================

插件主要文件main.py 下面是main.py内容
======
#main.py
api = None

def get_plugin_info(uuid):
    import qiancao_sdk
    global api
    api = qiancao_sdk.QQApi(uuid)
    return {
        "name": "我的机器人",
        "version": "1.0.0",
        "author": "me",
        "description": "支持多种匹配",
        "requires": [], #pip库
        "equals": [ #相等
            {"key": "/ping", "fun": "on_ping"},
        ],
        "contains": [
            #{"key": "天气", "fun": "on_weather", "case_sensitive": False}
        ],
        "startswith": [
            #{"key": "天气", "fun": "query_weather"} #匹配指令头 有对应的 query_weather 函数即可
        ],
        "regex": [
            {"key": r"^/echo (.+)$", "fun": "on_echo"} #正则
        ]
    }

def on_enable(): #不用可以删除
    pass

def on_disable(): #不用可以删除
    pass

def on_unload(): #不用可以删除
    pass

def on_set(): #不用可以删除
    pass

def on_message(msg):  #弃用但是保留 equals 等信息没命中才会 触发本函数 如果 不需要这个函数可以删除 equals 等没有值时 默认启用
    #api.outlog(msg.msg)
    pass

def on_ping(msg):
    return "pong"

def on_echo(msg):
    return "你的id: " + msg.user


================提供的api=============
#qiancao_sdk.py 以下api都是 同步返回 不是异步 调用api时自动释放gil
import json
import qq_api
from typing import Optional, Union, Dict, List, Any

class QQApi:
    API_OUTLOG = 1
    #其他API 省略...
    def __init__(self, uuid: str):
        self.uuid = uuid

    def _callback(self, api_id: int, appid: int, *args):
        padded = list(args) + [""] * (8 - len(args))
        padded = [str(x) if x is not None else "" for x in padded]
        return qq_api.Callback(self.uuid, api_id, appid, *padded)

    # ---------- 具体 API 封装 ----------
    def outlog(self, text: str, color_rgb: Optional[int] = None) -> Dict:
        return self._callback(self.API_OUTLOG,0, text, str(color_rgb) if color_rgb is not None else None)

    def send_messageEx(self,msg: qq_api.MessageEvent, text: str, is_wakeup: bool = False) -> Dict:
        return self._callback(self.API_SEND_MESSAGES,msg.appid,
                              msg.type, msg.groupid, text,msg.msgid,
                              "true" if is_wakeup else "false", None, None)

    def send_message(self,appid: int, type_: int, openid: str, text: str,msgid: str = "", is_wakeup: bool = False) -> Dict:
        """
        发送普通消息。
        :param type_: 消息类型，0=群聊，1=频道，2=私聊，3=频道私聊
        :param openid: 接收者的 openid
        :param text: 消息内容
        :param message_reference: 引用消息ID（可选）
        :param msgid: 消息ID，空字符串表示主动模式
        :param is_wakeup: 是否为私聊的唤醒消息（与 msgid 互斥，仅私聊有效）
        """
        # API_SEND_MESSAGES: _1=type, _2=openid, _3=text,  _4=msgid, _5=is_wakeup, _7=None, _8=None
        return self._callback(self.API_SEND_MESSAGES,appid,
                              type_, openid, text, msgid,
                              "true" if is_wakeup else "false", None, None)

    def delete_message(self,appid: int, type_: int, openid: str, msgid: str) -> Dict:
        """
        撤回消息。
        :param type_: 消息类型，0=群聊，1=频道，2=私聊，3=频道私聊
        :param openid: 会话对象ID
        :param msgid: 要删除的消息ID
        """
        return self._callback(self.API_DELETE_MESSAGES,appid, type_, openid, msgid)

    def respond_interaction(self, appid: int,interaction_id: str, code: int, data: str) -> Dict:
        """
        响应交互事件。
        :param interaction_id: 交互ID
        :param code: 响应码（如 0 表示成功）
        :param data: 响应数据（JSON 字符串）
        """
        return self._callback(self.API_RESPOND_INTERACTION,appid,interaction_id, str(code), data)

    def botlist(self) -> List[Dict[str, Any]]:
        """
        获取 Bot 列表，每个字典包含以下字段（由 C++ 提供）：
        - appid, name, qq, avatarPath, received, send, online, id, union_openid, startup_time
        - online_duration: 已格式化的在线时长字符串，例如 "2天3小时5分钟"
        """
        return = self._callback(self.API_BOT_LIST, 0)

    def get_openid(self,appid: int ,user_id:int) -> Dict:
        return self._callback(self.API_GET_USER_OPENID, appid, str(user_id))

    def get_user_name(self,appid:int ,user_id:int) -> Dict:
        return self._callback(self.API_GET_USER_NAME, appid, str(user_id))
    #释放gil 的http
    def http_request(self, url: str, method: str = "GET", headers: dict = None, body: bytes = None, timeout: int = 30) -> dict:
        headers_json = json.dumps(headers or {})
        body_b64 = base64.b64encode(body).decode('ascii') if body is not None else ""
        return self._callback(self.API_HTTP, 0, url, method.upper(), headers_json, body_b64, str(timeout))

     # ---------- 补充的 API 封装 ----------
    def get_user_id(self, appid: int, user: str) -> Dict:
        """
        根据用户整数ID获取用户内部ID（或用户信息）
        :param appid: Bot appid
        :param user: 32字节那个
        :return: 整数id
        """
        return self._callback(self.API_ID_GET_USER_ID, appid, str(user_id))

    def htmlimg1(self, text: str, width: int) -> Dict:
        """
        将HTML文本渲染为图片（方式1 ） 返回图片标签 ![img](路径) 拼接到文本发送即可
        :param text: HTML文本
        :param width: 图片宽度（或其它整型参数）
        """
        return self._callback(self.API_ID_HTMLIMG1, 0, text, str(width))

    def htmlimg2(self, text: str, width: int, height: int, extra: int = 0) -> Dict:
        """
        将HTML文本渲染为图片（方式2） 返回图片标签 ![img](路径) 拼接到文本发送即可
        :param text: HTML文本
        :param width: 宽度
        :param height: 高度
        :param extra: 额外参数，默认为0（http请求api超时时间）
        """
        return self._callback(self.API_ID_HTMLIMG2, 0, text, str(width), str(height), str(extra))

    def add_timer(self, appid: int, remark: str, time_str: str, execute_count: int, code: str) -> Dict:
        """
        添加定时任务
        :param appid: Bot appid
        :param remark: 备注（参数1）
        :param time_str: 定时时间（参数2）
        :param execute_count: 执行次数，超出销毁（参数3）
        :param code: Python代码（参数4）
        """
        return self._callback(self.API_ID_DS, appid, remark, time_str, str(execute_count), code)

    def get_member(self,appid: int,  openid : str, uset : str) -> Dict:
        """
        查询 某个用户 在群 昵称 身份 返回文本json需要解析 { "member_openid": "32字节hex", "username": "群昵称", "member_role": "owner", "bot": false, "joined_at": "2026-03-23T14:46:25+08:00", "union_openid": "和member_openid一样" }
        :param appid: Bot appid
        :param openid: 群id（参数1）
        :param uset: 用户id（参数2）
        """
        return self._callback(self.API_ID_GET_MEMBER, appid, openid, uset)


class ButtonGroup:
    def __init__(self):
        self.rows = [[]]          # 二维列表，每个元素是一个按钮字典
        self.row = 0
        self.col = 0

    def _random_id(self, length=8):
        return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

    def add(self,
            name: str,                          # 按钮显示文本
            data: str,                          # 按钮携带的数据（回调时返回）
            action_type: int = 2,               # 动作类型：0=链接, 1=回调, 2=发送（默认2）
            btn_id: str = None,                 # 按钮唯一ID，不传则自动生成
            enter: bool = False,                # 是否立即发送（点击后立即执行）
            reply: bool = False,                # 是否引用原消息
            color: int = 1,                     # 按钮颜色样式，默认1
            permission_type: int = 2,           # 权限类型：0=部分人, 1=管理员, 2=全部（默认2）
            specify_users: list = None,         # 指定可用用户列表（permission_type=0时有效） -->ev.user 获取
            visited_label: str = "visited",     # 回调后按钮显示的文本（默认"visited"）
            unsupport_tip: str = None,          # 不支持时的提示文本（回调code4弹窗）
            modal_content: str = None,          # 确认框内容（最多40字符）
            modal_confirm: str = None,          # 确认按钮文本（最多4字符）
            modal_cancel: str = None,           # 取消按钮文本（最多4字符）
            subscribe_id: int = None,           # 订阅模板ID（整数）
            custom_subscribe_id: str = None):   # 自定义订阅模板ID（字符串）
        """
        在当前行列位置添加一个按钮，完成后自动移动到下一列。
        参数均为英文，含义与原易语言类一致。
        """
        ...

    def newrow(self):
        """换行，列归零"""
        self.row += 1
        self.col = 0
        # 预先创建空行（防止索引越界）
        while len(self.rows) <= self.row:
            self.rows.append([])

    def to_json(self, indent=None) -> str:
        """输出 JSON 字符串，顶层包含 content.rows"""
        return json.dumps({"content": {"rows": self.rows}},
                          ensure_ascii=False,
                          indent=indent,
                          separators=(',', ':') if indent is None else None)
)";



#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QFont>




void AppWindow::addMessage(const QString &text, bool isUser)
{
    if (text.trimmed().isEmpty()) return;
    QMetaObject::invokeMethod(this, [this, text, isUser]() {
        // 1. 卡片背景与边框（颜色稍微调深一点，确保AI的框在白色背景下能明显看出来）
        QString cardBg = isUser ? "#E8F5E9" : "#EEF2F6";   // 用户浅绿，AI 浅灰蓝
        QString cardBorder = isUser ? "#4CAF50" : "#9C27B0";

        // 2. 【关键修改】将角色名从代码块中抽离出来
        // 角色名单独用 <div> 包裹，文本颜色改为黑色，并保留换行
        QString rolePrefix = isUser ? "👤 用户:" : "🤖 AI:";
        QString roleHtml = QString("<div style='color: #000000; font-weight: bold; margin-bottom: 6px; font-size: 14px;'>%1</div>").arg(rolePrefix);

        // 3. 纯文本内容使用代码块包裹（不再包含角色名）
        QString wrapped = "```" + text + "```\n";
        wrapped.replace("\n\n\n\n", "\n\n");
        wrapped.replace("\n\n\n", "\n\n");

        // 4. 拼接 HTML
        QString finalHtml = roleHtml; // 先拼上角色名
        QStringList parts = wrapped.split("```");

        for (int i = 0; i < parts.size(); ++i) {
            if (i % 2 == 0) {
                // 外部文本（外层换行符等）
                QString escaped = parts[i].toHtmlEscaped();
                escaped.replace("\n", "<br>");
                finalHtml += escaped;
            } else {
                // 【关键修改】代码块内部：确保文本颜色为黑色（配合你的 HTML 样例要求）
                QString codeBg = isUser ? "#3398DE" : "#353834";
                QString codeTextColor = "#ffffff"; // 纯黑文字

                finalHtml += QString(
                                 "<pre style='background:%1; color:%2; padding:8px 10px; border-radius:6px; "
                                 "font-family:Consolas, monospace; font-size:12px; white-space:pre-wrap; "
                                 "word-wrap:break-word; margin:0;'>"
                                 "<code>%3</code></pre>"
                                 ).arg(codeBg, codeTextColor, parts[i].toHtmlEscaped());
            }
        }

        // 5. 构建外层卡片
        QString cardHtml = QString(
                               "<div style='margin-bottom: 8px; "
                               "background: %1; "
                               "border-left: 5px solid %2; "
                               "border-radius: 6px; "
                               "padding: 6px 10px; "
                               "box-shadow: 0 1px 2px rgba(0,0,0,0.03);'>"
                               "  %3"
                               "</div>"
                               ).arg(cardBg, cardBorder, finalHtml);

        QTextCursor cursor(chatTextEdit->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertHtml(cardHtml);
        chatTextEdit->moveCursor(QTextCursor::End);
        chatTextEdit->ensureCursorVisible();
    }, Qt::QueuedConnection);
}



bool confirmCommandExecutionGui(const QString &model,const QString &cmd, QWidget *parent = nullptr) {
    QDialog dialog(parent);
    dialog.setWindowTitle("⚠️ 确认执行系统命令");
    dialog.setMinimumSize(900, 500);
    dialog.setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    // 顶部提示
    QLabel *label = new QLabel(
        "AI 请求执行以下命令，请仔细核对。建议先点击「AI检查」辅助判断："
        );
    label->setWordWrap(true);
    mainLayout->addWidget(label);

    // ----- 命令显示区域 -----
    QPlainTextEdit *textEdit = new QPlainTextEdit();
    textEdit->setPlainText(cmd);
    textEdit->setReadOnly(true);
    textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);  // 长命令不换行，横向滚动
    QFont font("Courier New", 10);
    font.setStyleHint(QFont::Monospace);
    textEdit->setFont(font);
    textEdit->setMinimumHeight(150);
    mainLayout->addWidget(textEdit, 1);  // 拉伸占满

    // ----- AI 检查结果显示区域 -----
    QLabel *aiStatusLabel = new QLabel("🤖 AI检查状态：尚未检查");
    aiStatusLabel->setStyleSheet("padding: 5px; background-color: #f0f0f0; border-radius: 3px;");
    aiStatusLabel->setWordWrap(true);
    mainLayout->addWidget(aiStatusLabel);

    // ----- 底部按钮布局 -----
    QHBoxLayout *btnLayout = new QHBoxLayout();

    // 1. 复制按钮
    QPushButton *copyBtn = new QPushButton("📋 复制命令");
    QObject::connect(copyBtn, &QPushButton::clicked, [&cmd]() {
        QApplication::clipboard()->setText(cmd);
    });

    QPushButton *aiCheckBtn = new QPushButton("🤖 AI检查");
    QObject::connect(aiCheckBtn, &QPushButton::clicked, [=]() {
        // 1. 禁用按钮，防止重复点击
        aiCheckBtn->setEnabled(false);
        aiCheckBtn->setText("⏳ 检查中...");
        aiStatusLabel->setText("🤖 AI检查状态：正在调用模型...");
        aiStatusLabel->setStyleSheet("padding: 5px; background-color: #fff3cd; border-radius: 3px;");

        // 用 QPointer 保护按钮和标签（它们是 QObject 子类，会被 dialog 析构时删除）
        QPointer<QPushButton> btnPtr = aiCheckBtn;
        QPointer<QLabel> labelPtr = aiStatusLabel;

        // 启动后台线程
        std::thread([=]() {
            // 【子线程】执行耗时 AI 检查
            QString result = ai_ui->Ai_post(
                model,
                "你的主要任务是 检查下面 代码是否危害系统 比如格式化 删除文件 或者 覆盖写入系统文件 "
                "\n请返回 如果无危险返回【无危险】 如果可能有问题返回【危险】并且给理由 当然还可以输出其他内容 因为用户根据你说的进行判断\n\n" + cmd,
                0
                );

            // 【切回主线程】更新 UI
            QMetaObject::invokeMethod(qApp, [=]() {
                // 检查控件是否还存在（若对话框已关闭，则二者为 null）
                if (btnPtr.isNull() || labelPtr.isNull()) {
                    return; // 对话框已销毁，忽略结果
                }

                // 恢复按钮
                btnPtr->setEnabled(true);
                btnPtr->setText("🤖 AI检查");

                // 更新标签
                labelPtr->setText("🤖 AI检查结果：" + result);

                // 根据结果变色
                if (result.contains("高危") || result.contains("危险")) {
                    labelPtr->setStyleSheet("padding: 5px; background-color: #ffcccc; border-radius: 3px; color: #cc0000;");
                } else if (result.contains("中危")) {
                    labelPtr->setStyleSheet("padding: 5px; background-color: #fff3cd; border-radius: 3px; color: #856404;");
                } else {
                    labelPtr->setStyleSheet("padding: 5px; background-color: #d4edda; border-radius: 3px; color: #155724;");
                }
            }, Qt::QueuedConnection);
        }).detach();
    });

    // 3. 确认执行按钮
    QPushButton *yesBtn = new QPushButton("✅ 确认执行");
    yesBtn->setStyleSheet("color: green; font-weight: bold;");

    // 4. 取消按钮（默认焦点）
    QPushButton *noBtn = new QPushButton("❌ 取消");
    noBtn->setDefault(true);
    noBtn->setStyleSheet("color: red; font-weight: bold;");

    // 布局组装
    btnLayout->addWidget(copyBtn);
    btnLayout->addWidget(aiCheckBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(yesBtn);
    btnLayout->addWidget(noBtn);

    mainLayout->addLayout(btnLayout);

    // ----- 信号连接 -----
    QObject::connect(yesBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(noBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // ----- 执行并返回 -----
    return dialog.exec() == QDialog::Accepted;
}
bool confirmCommandExecution(const QString &model, const QString &cmd, QWidget *parent=nullptr) {
    // 如果已经在主线程，直接调用
    if(!g_不审核)
    {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            return confirmCommandExecutionGui(model, cmd, parent);
        } else {
            // 否则，通过 invokeMethod 转到主线程，并等待结果
            bool result = false;
            QEventLoop loop;
            QMetaObject::invokeMethod(qApp, [&]() {
                // 确保在主线程中执行
                result = confirmCommandExecutionGui(model, cmd, parent);
                loop.quit(); // 对话框关闭后，退出事件循环
            }, Qt::QueuedConnection);

            // 阻塞当前线程，直到 loop.quit() 被调用
            loop.exec();
            return result;
        }
    }
    return true;
}

AppWindow::AppWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("AI生成插件 请先打开一个文件夹，如果没有你就创建一个 打开 仅限 框架目录/plugin/里面 然后就可以让ai写代码了");
    resize(1200, 700);
    m_dir = g_config["aicode_dir"].toString();
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    // --- 顶部栏 ---
    QHBoxLayout *topLayout = new QHBoxLayout();
    pathLabel = new QLabel("当前文件夹：未选择");
    pathLabel->setStyleSheet("font-weight: bold;");
    QPushButton *openBtn = new QPushButton("打开文件夹");
    connect(openBtn, &QPushButton::clicked, this, &AppWindow::openFolder);
    topLayout->addWidget(pathLabel);
    topLayout->addStretch();
    topLayout->addWidget(openBtn);
    mainLayout->addLayout(topLayout);

    // --- 三栏分割器 ---
    QSplitter *splitter = new QSplitter(Qt::Horizontal);

    fileModel = new QFileSystemModel(this);
    fileModel->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);

    // 2. 先设置模型的根路径（告诉模型去哪里加载）
    if(m_dir.isEmpty()){
        QString appDir = QCoreApplication::applicationDirPath();
        fileModel->setRootPath(appDir);
    }else
    {
        fileModel->setRootPath(m_dir);
    }
    // 3. 创建视图
    fileTree = new QTreeView();
    fileTree->setModel(fileModel);           // 【关键！】必须先设置模型
    fileTree->setFixedWidth(200);
    fileTree->setHeaderHidden(true);        // 隐藏表头（只留文件名）
    fileTree->setColumnHidden(1, true);     // 隐藏“大小”
    fileTree->setColumnHidden(2, true);     // 隐藏“类型”
    fileTree->setColumnHidden(3, true);     // 隐藏“修改日期”
    fileTree->setRootIsDecorated(false);    // 不显示根节点的展开图标（看个人喜好）
    fileTree->setFont(QFont("Segoe UI", 10));
    connect(fileTree, &QTreeView::clicked, this, &AppWindow::onFileClicked);
    splitter->addWidget(fileTree);
    QTimer::singleShot(0, this, [=]() {
        QString appDir = QCoreApplication::applicationDirPath();
        QModelIndex rootIdx;
        if(m_dir.isEmpty()){
            QString appDir = QCoreApplication::applicationDirPath();
            rootIdx = fileModel->index(appDir);
        }else
        {
            rootIdx = fileModel->index(m_dir);
        }
        if (rootIdx.isValid()) {
            fileTree->setRootIndex(rootIdx);
        } else {
            qDebug() << "根目录索引无效，可能是路径权限问题";
        }
    });


    codeEditor = new CodeEditor();
    codeEditor->setPlaceholderText("Python 代码将显示在这里……");
    codeEditor->setFont(QFont("Consolas", 10));
    splitter->addWidget(codeEditor);
    new PythonHighlighter(codeEditor->document());
    // 3. AI 聊天区


    QVBoxLayout *mainLayout2 = new QVBoxLayout(this);
    mainLayout2->setContentsMargins(2, 2, 2, 2);

    chatTextEdit = new QTextEdit(this);
    chatTextEdit->setReadOnly(true);        // 只读但允许选择复制
    chatTextEdit->setUndoRedoEnabled(false);
    chatTextEdit->document()->setDefaultFont(QFont("Segoe UI", 11));
    chatTextEdit->setStyleSheet(
        "QTextEdit {"
        "    background: #f5f5f5;"
        "    border: none;"
        "    padding: 10px;"
        "}"
        );

    mainLayout2->addWidget(chatTextEdit);
    // 输入区
    QHBoxLayout *inputLayout = new QHBoxLayout;
    inputLayout->setContentsMargins(0, 0, 0, 0);
    messageInput = new QTextEdit;
    messageInput->setPlaceholderText("输入消息（Ctrl+Enter发送）...");
    messageInput->setFixedHeight(70);
    // 核心修正：禁止水平滚动 + 启用 CSS 强制断行
    messageInput->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    messageInput->setLineWrapMode(QTextEdit::WidgetWidth);
    messageInput->setStyleSheet(
        "QTextEdit {"
        "   border: 1px solid #E7D9C8;"
        "   border-radius: 8px;"
        "   padding: 6px;"
        "   word-break: break-all;"  // 关键！让长数字也能换行
        "}"
        );
    sendBtn = new QPushButton("发送");
    clearBtn = new QPushButton("清空对话");
    inputLayout->addWidget(messageInput, 1);
    mainLayout2->addLayout(inputLayout);
    QHBoxLayout *inputLayout0 = new QHBoxLayout;
    QLabel *bq=new QLabel("模型：");
    //bq->setAlignment(Qt::AlignRight);//右对齐
    bq->setMaximumWidth(40);
    inputLayout0->addWidget(bq);
    modelCombo = new QComboBox;
    clearBtn->setMaximumWidth(100);
    sendBtn->setMaximumWidth(100);
    inputLayout0->addWidget(modelCombo);
    nosh = new QCheckBox("不审核");
    inputLayout0->addWidget(nosh);
    inputLayout0->addWidget(clearBtn);
    inputLayout0->addWidget(sendBtn);
    mainLayout2->addLayout(inputLayout0);

    connect(nosh, &QCheckBox::clicked, this, [=]() {
        g_不审核 = nosh->isChecked();
    });
    // 信号连接
    connect(sendBtn, &QPushButton::clicked, this, [=]() {
        QString text = messageInput->toPlainText().trimmed();
        if (!text.isEmpty()) {
            messageInput->clear();
            onSendMessage(text);

        }
    });
    connect(ai_ui, &AiWidget::modelListUpdated, this, [this]() {
        // 【关键】使用 invokeMethod 强制在主线程执行 UI 更新
        // 哪怕发射信号的线程是子线程，Qt 也会自动切换到主线程来执行 lambda
        QMetaObject::invokeMethod(this, [this]() {
            setModels();
        }, Qt::QueuedConnection);
    });
    setModels();
    connect(clearBtn, &QPushButton::clicked, this, &AppWindow::clearChat);


    QWidget *container = new QWidget;   // 创建一个容器窗口部件
    container->setLayout(mainLayout2);  // 将布局设置给容器
    splitter->addWidget(container);     // 将容器添加到分割器


    splitter->setSizes(QList<int>() << 200 << 500 << 500);
    mainLayout->addWidget(splitter);
    m_fun=QJsonArray();
    内置函数();

    if(!m_dir.isEmpty()) Folder();
    statusBar()->showMessage("就绪");
}

AppWindow::~AppWindow() {}

void AppWindow::openFolder()
{
    m_dir = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (m_dir.isEmpty()) return;
    m_dir = m_dir+"/";
    g_config["aicode_dir"] = m_dir;
    saveConfig();
    Folder();
}
void AppWindow::Folder()
{
    pathLabel->setText("当前文件夹：" + m_dir);
    fileTree->setRootIndex(fileModel->setRootPath(m_dir));

    QString filePath = QDir(m_dir).filePath("ai对话.json");
    QFile file(filePath);
    bool loadSuccess = false;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error == QJsonParseError::NoError) {
            sxw = doc.object();
            loadSuccess = true;
        } else {
            sxw = QJsonObject();
        }
    } else {
        sxw = QJsonObject();
    }

    // 清空聊天显示
    chatTextEdit->clear();   // 替换原来的 chatModel->clearMessages()

    QString mode = sxw["model"].toString();
    int index = modelCombo->findText(mode);
    if (index != -1)
        modelCombo->setCurrentIndex(index);

    if (loadSuccess && sxw.contains("messages") && sxw["messages"].isArray()) {
        const QJsonArray msgs = sxw["messages"].toArray();
        for (const QJsonValue &val : msgs) {
            QJsonObject msgObj = val.toObject();
            QString role = msgObj["role"].toString();
            QString content = msgObj["content"].toString();
            if (role == "user") {
                addMessage(content, true);
            } else if (role == "assistant") {

                QString displayContent;
                bool hasContent = !content.trimmed().isEmpty();
                bool hasToolCalls = msgObj.contains("tool_calls") && msgObj["tool_calls"].isArray();

                // 提取工具名称（如果有）
                QStringList toolNames;
                if (hasToolCalls) {
                    QJsonArray calls = msgObj["tool_calls"].toArray();
                    for (const QJsonValue &callVal : calls) {
                        QJsonObject callObj = callVal.toObject();
                        if (callObj.contains("function") && callObj["function"].isObject()) {
                            QJsonObject funcObj = callObj["function"].toObject();
                            QString name = funcObj["name"].toString();
                            if (!name.isEmpty())
                                toolNames << name;
                        }
                    }
                }

                if (hasContent) {
                    displayContent = content;  // 保留原始内容（包括换行）
                }
                // 如果有工具调用，将工具信息附加到内容后面（或单独显示）
                if (!toolNames.isEmpty()) {
                    QString toolMsg = "\n\n[调用工具: " + toolNames.join(", ") + "]";
                    if (hasContent) {
                        displayContent += toolMsg;
                    } else {
                        displayContent = toolMsg;  // 没有文本内容，只显示工具信息
                    }
                }

                // 如果既有内容又有工具，或者只有工具，都需要显示；如果什么都没有则跳过
                if (!displayContent.trimmed().isEmpty()) {
                    addMessage(displayContent, false);
                }
            }else if (role == "tool") {
                    // 提取工具名（如果有）
                    QString toolName = msgObj["name"].toString();
                    QString displayContent = content;
                    if (!toolName.isEmpty()) {
                        addMessage("[工具返回值 (" + toolName + ")] " + displayContent, false);
                    } else {
                        addMessage("[工具返回值] " + displayContent, false);
                    }
            }
        }
    }
}
#include <QTextCodec>
QString listDirectoryEntries(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) return QString();

    QFileInfoList infoList = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    QStringList lines;
    for (const QFileInfo& info : infoList) {
        QString name = info.fileName();
        if (name == "ai对话.json" || name.endsWith(".tmp", Qt::CaseInsensitive)) continue;

        QString size = info.isFile() ? QString::number(info.size()) + " bytes"
                                     : (info.isDir() ? "<DIR>" : "?");
        lines << name + " (" + size + ")";

        if (info.isFile() && name.endsWith(".py", Qt::CaseInsensitive)) {
            QFile f(info.absoluteFilePath());
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray data = f.readAll();
                f.close();
                QTextCodec *codec = QTextCodec::codecForName("UTF-8");
                QString content = codec ? codec->toUnicode(data) : QString::fromLocal8Bit(data);
                QStringList pyLines;
                QRegularExpression regex(R"(^\s*(?:async\s+)?(def|class)\s+\w+\s*[\(:]?)");
                for (const QString& line : content.split('\n')) {
                    if (regex.match(line).hasMatch()) pyLines << line;
                }
                lines << pyLines;
            }
        }
    }
    return lines.join("\n");
}


void AppWindow::onFileClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    QFileInfo fileInfo = fileModel->fileInfo(index);

    // 单击保存上一个选中的文件（仅在选中文件时记录）
    if (fileInfo.isFile()) {
        if (!lastSelectedFile.isEmpty()) {
            statusBar()->showMessage("上次选中文件：" + lastSelectedFile);
        }
        lastSelectedFile = fileInfo.fileName();
    }

    // 如果是文件夹，仅显示路径
    if (fileInfo.isDir()) {
        codeEditor->setPlainText("当前选中目录：" + fileInfo.absoluteFilePath());
        return;
    }

    // 1. 文件大小限制：超过 100KB 不予加载
    if (fileInfo.size() > 100 * 1024) {
        codeEditor->setPlainText(QString("错误：文件大小为 %1 KB，超过 100KB 预览限制，无法打开。").arg(fileInfo.size() / 1024));
        return;
    }

    // 2. 允许预览的文本扩展名列表
    static const QSet<QString> allowedExtensions = {".py", ".txt", ".log", ".json", ".ini", ".xml",".html",".js",".css",".h",".hpp",".c",".cpp"};

    // 获取小写后缀（带点）
    QString suffix = "." + fileInfo.suffix().toLower();

    if (allowedExtensions.contains(suffix)) {
        // 如果后缀在白名单内，读取文件内容
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            // 强制 UTF-8 编码
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            stream.setEncoding(QStringConverter::Utf8);
#else
            stream.setCodec("UTF-8");
#endif
            codeEditor->setPlainText(stream.readAll());
            file.close();
        } else {
            codeEditor->setPlainText("无法读取文件内容（权限或文件被占用）");
        }
    } else {
        // 如果后缀不在白名单内，只显示路径，防止二进制乱码
        codeEditor->setPlainText("非文本预览文件（仅支持 .py .txt .log .json .ini .xml），仅显示路径：" + fileInfo.absoluteFilePath());
    }
}

void AppWindow::内置函数(const QString &Nmae,const QString &remark,const QStringList &params)
{

    QJsonObject functionObj;
    functionObj["name"] = Nmae;
    functionObj["description"] = remark;

    QJsonObject parameters;
    parameters["type"] = "object";
    QJsonObject properties;
    QJsonArray required;

    int paramIndex = 1;
    for (const QString &paramName : params) {
        if (paramName.isEmpty()) {
            break;
        }
        QString pKey = QString("p%1").arg(paramIndex);
        QJsonObject paramDef;
        paramDef["type"] = "string";
        paramDef["description"] = paramName;
        properties[pKey] = paramDef;
        required.append(pKey);
        ++paramIndex;
    }

    if (!properties.isEmpty()) {
        parameters["properties"] = properties;
        parameters["required"] = required;
        functionObj["parameters"] = parameters;
    } else {
        functionObj["parameters"] = QJsonObject(); // 无参数时留空对象
    }

    QJsonObject toolObj;
    toolObj["type"] = "function";
    toolObj["function"] = functionObj;
    m_fun.append(toolObj);
}
void AppWindow::内置函数()
{

    内置函数("w_file", "将文本内容写入到指定的文件中，如果文件不存在则创建", {"文件路径", "文件内容"});
    内置函数("r_file", "读取指定文件的内容并以文本形式返回", {"文件路径"});
    内置函数("delete_file", "删除某个文件", {"文件路径"});
    内置函数("append_file", "追加内容", {"文件路径","追加文本"});

    内置函数("LoadPlugin", "添加当前插件到框架 如果不存在就添加 存在就重载 ", {"无参数"});
    内置函数("uninstall_Plugin", "卸载当前插件", {"无参数"});
    内置函数("testplugin", "测试插件 测试指令是否正确返回", {"测试的指令"});
    内置函数("run_python", "执行python代码 捕获print 日志", {"python代码"});

    内置函数("getFunctionCode", "读取某个py文件的某个函数代码", {"py文件名","函数名 如 my_function","类名 如果有 没有可空 如 MyClass"});
    内置函数("replaceFunction", "替换个py文件的某个函数代码", {"py文件名","函数名 如 my_function","替换的代码 如","类名 如果有 没有可空 如 MyClass"});


    内置函数("byss","必应搜索",QStringList() << "搜索关键词 如 原神"<<"页码 1开始 如 1");
    内置函数("llwye","浏览网页 返回提取后的文本 当用户发送链接时 可以使用 也可以请求某些api 当用户让你帮我对接api时",QStringList() << "链接 如 https://www.baidu.com");
    内置函数("exec_cmd", "在操作系统命令行中执行一个指令，并返回执行结果 注意 py环境是 是python3.14t.exe", {"执行命令"});

}
QString onMessageReceived2(const MessageEvent &msg,int i);
QString browseWeb(const QString &urlString);
QString AppWindow::tools_fun(const QString &tool_name, const QString &args, const QString &model)
{
    // 1. 解析参数
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(args.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        return "错误：工具参数解析失败，JSON 格式不正确。";
    }
    QJsonObject obj = doc.object();

    // 2. 确保工作目录有效
    if (m_dir.isEmpty()) {
        return "错误：当前未打开任何文件夹，无法执行文件操作或命令。";
    }

    QString result;
    if (tool_name == "w_file") {
        // ---------- 写文件 ----------
        QString fileName = obj["p1"].toString();
        QString content = obj["p2"].toString();
        if (fileName.isEmpty()) return "错误：写文件必须提供文件路径 (p1)";

        QString fullPath = QDir(m_dir).filePath(fileName);
        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return "错误：无法打开或创建文件 " + fileName + "。请检查权限或路径。";
        }
        QTextStream out(&file);
        // 【关键修改】强制 UTF-8 编码写入
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif
        out << content;
        file.close();
        result = "写入成功，已保存至文件：" + fileName;

    } else if (tool_name == "r_file") {
        // ---------- 读文件 ----------
        QString fileName = obj["p1"].toString();
        if (fileName.isEmpty()) return "错误：读文件必须提供文件路径 (p1)";

        QString fullPath = QDir(m_dir).filePath(fileName);
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return "错误：无法打开文件 " + fileName + "。文件可能不存在或权限不足。";
        }
        QTextStream in(&file);
        // 【关键修改】强制 UTF-8 编码读取
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        result = in.readAll();
        file.close();

    } else if (tool_name == "exec_cmd") {
        QString cmd = obj["p1"].toString();
        if (cmd.isEmpty()) {
            return "错误：执行命令不能为空 (p1)";
        }
        if (!confirmCommandExecution(model,cmd, this)) {
            return "管理员阻止了本次cmd的执行 可能误点 或者代码可能危害系统";
        }
        QProcess process;
        process.setWorkingDirectory(m_dir);


#if defined(Q_OS_WIN)
        process.setNativeArguments("/c " + cmd); // 使用 cmd /c 执行
        process.start("cmd.exe"); //m_dir
#else
        process.start(cmd);
#endif

        if (!process.waitForStarted(5000)) {
            return "错误：执行命令超时或无法启动。";
        }

        // 等待进程运行结束（30秒超时）
        if (!process.waitForFinished(30000)) {
            process.kill(); // 超时强行杀进程
            return "错误：命令执行时间过长（超过30秒），已强制终止。";
        }
        QByteArray stdoutData = process.readAllStandardOutput();
        QByteArray stderrData = process.readAllStandardError();

        // 组装返回结果
        QString result = "命令执行完毕。\n";
        if (!stdoutData.isEmpty()) {
            // 用 fromLocal8Bit 解决 Windows 下 CMD 输出中文乱码问题
            result += "【标准输出】\n" + QString::fromLocal8Bit(stdoutData) + "\n";
        }
        if (!stderrData.isEmpty()) {
            result += "【标准错误】\n" + QString::fromLocal8Bit(stderrData) + "\n";
        }
        if (stdoutData.isEmpty() && stderrData.isEmpty()) {
            result += "无输出内容。";
        }

        return result; // 直接 return 结果，避免后续继续拼接
    }else if(tool_name == "byss")
    {
        int y = obj["p2"].toInt();
        if(y<=0) y=1;
        result = browseWeb("https://cn.bing.com/search?q="+ QUrl::toPercentEncoding(obj["p1"].toString()) +"&first="+QString::number(y*10));
    }else if(tool_name == "llwy")
        result = browseWeb(obj["p1"].toString());
    
    else if (tool_name == "delete_file") {
        QString filePath = obj["p1"].toString();
        if (filePath.isEmpty()) {
            return "错误：文件路径不能为空 (p1)";
        }

        // 基于 m_dir 转为绝对路径（若已是绝对路径则保持不变）
        QString absPath = QDir(m_dir).absoluteFilePath(filePath);
        QFileInfo info(absPath);
        if (!info.exists()) {
            return "错误：文件不存在：" + absPath;
        }
        if (!info.isFile()) {
            return "错误：路径指向的不是一个文件：" + absPath;
        }

        QFile file(absPath);
        if (!file.remove()) {
            return "错误：删除文件失败：" + file.errorString();
        }
        return "文件删除成功：" + absPath;
    }
    else if (tool_name == "append_file") {
        QString filePath = obj["p1"].toString();
        QString content = obj["p2"].toString();   // 内容从 p2 获取
        if (filePath.isEmpty()) {
            return "错误：文件路径不能为空 (p1)";
        }

        // 基于 m_dir 转为绝对路径
        QString absPath = QDir(m_dir).absoluteFilePath(filePath);
        QFile file(absPath);

        // 以追加模式打开（若文件不存在则自动创建）
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            return "错误：无法打开文件进行追加写入：" + file.errorString();
        }

        QTextStream out(&file);
        // 推荐指定编码为 UTF-8，避免中文乱码（与你的 exec_cmd 中 fromLocal8Bit 一致）
        out.setCodec("UTF-8");
        out << content;   // 不加换行，若需要换行请追加 "\n"
        file.close();

        return "内容追加成功：" + absPath;
    }

    else if(tool_name == "LoadPlugin"){

        int index = pluginPage->findPluginIndex(m_dir);
        if(index!=-1)
        {
            AppendEventLog("[重载插件]"+m_pluginList[index].name);
            bool enabled = m_pluginList[index].enabled;
            if (m_pluginList[index].enabled) pluginPage->disable_Plugin(m_pluginList[index]);//调禁用

            pluginPage->uninstall_Plugin(m_pluginList[index]);//里面会重置enabled 变量
            m_pluginList[index].enabled = enabled;

            QString err;
            py::gil_scoped_acquire gil;
            if (m_pluginList[index].type==0)
            {
                err = pluginPage->LoadPlugin_py(m_pluginList[index]);
            }
            if(err.isEmpty())
            {
                QMetaObject::invokeMethod(qApp, [=]() {
                    pluginPage->updatePluginItemInUI(index);
                }, Qt::QueuedConnection);

                return "重载成功";
            }
            err ="[重载插件]"+m_pluginList[index].name+" 失败 错误信息:"+err;
            AppendEventLog(err);
            QMetaObject::invokeMethod(qApp, [=]() {
                pluginPage->removePlugin(index);
            }, Qt::QueuedConnection);

            return err;
        }
        QList<int> arr;
        QString err =pluginPage->LoadPlugin(m_dir,0,false,arr);

        if(err.isEmpty()){
            pluginPage->savePlugins();
            err = "载入成功";
        }
        return err;
    }
    else if(tool_name == "uninstall_Plugin"){

        int index = pluginPage->findPluginIndex(m_dir);
        if(index!=-1)
        {
            QMetaObject::invokeMethod(qApp, [=]() {
                pluginPage->uninstall_Plugin2(index);
                pluginPage->savePlugins();
            }, Qt::QueuedConnection);

            return "卸载成功";
        }

        return "当前插件未载入 不能卸载";
    }
    else if(tool_name == "testplugin"){

        int index = pluginPage->findPluginIndex(m_dir);
        if(index!=-1)
        {
            MessageEvent ev;
            ev.appid=10020001;
            ev.groupId = "A9F143DA1BB80A104EE08554CB8399BA";
            ev.user = "A9F143DA1BB80A104EE08554CB8399BA";
            ev.user_int = 1;
            ev.at_you = true;
            ev.msg = obj["p1"].toString();
            ev.msgId = "msgid";
            ev.nickname = "test";
            return "测试返回结果(如果是空代表没返回)：" + onMessageReceived2(ev,index);
        }

        return "当前插件未载入 不能测试";
    } else if(tool_name == "run_python"){
        QString py_code = obj["p1"].toString();
        if (!confirmCommandExecution(model,py_code, this)) {
            return "管理员阻止了本次 py_code 的执行 可能误点 或者代码可能危害系统";
        }
        return python_code(py_code);
    }
    else if(tool_name == "getFunctionCode"){
        QString pyfile_name = obj["p1"].toString();
        QString myfun =  obj["p2"].toString();
        QString myclass =  obj["p3"].toString();
        if(pyfile_name.isEmpty()) return "参数1路径为空";
        pyfile_name = m_dir+pyfile_name;
        return getFunctionCode(pyfile_name,myfun,!myclass.isEmpty(),myclass);
    }
    else if(tool_name == "replaceFunction"){
        QString pyfile_name = obj["p1"].toString();
        QString myfun =  obj["p2"].toString();
        QString py_code = obj["p3"].toString();
        QString myclass =  obj["p4"].toString();
        if(pyfile_name.isEmpty()) return "参数1路径为空";
        pyfile_name = m_dir+pyfile_name;
        // 1. 创建备份
        QString backupPath = pyfile_name + ".tmp";
        if (QFile::exists(backupPath)) QFile::remove(backupPath);
        if (!QFile::copy(pyfile_name, backupPath)) {
            return "无法创建备份文件: " + backupPath;
        }

        // 2. 在备份上执行替换
        QString err;
        bool ok = replaceFunction(backupPath, myfun, py_code, !myclass.isEmpty(), myclass,&err);
        if (!ok) {
            QFile::remove(backupPath);
            return "替换失败 可能寻找函数语法错误 请使用其他工具替换";
        }

        // 3. 语法检查（尝试多种编码）
        QString script = QString(
                             "import sys, ast\n"
                             "def check_syntax(filename):\n"
                             "    encodings = ['utf-8', 'gbk', 'gb2312', 'latin-1']\n"
                             "    content = None\n"
                             "    for enc in encodings:\n"
                             "        try:\n"
                             "            with open(filename, 'r', encoding=enc) as f:\n"
                             "                content = f.read()\n"
                             "            break\n"
                             "        except UnicodeDecodeError:\n"
                             "            continue\n"
                             "    if content is None:\n"
                             "        print('Error: Cannot decode file with known encodings')\n"
                             "        return\n"
                             "    try:\n"
                             "        ast.parse(content)\n"
                             "        print('SYNTAX_OK')\n"
                             "    except SyntaxError as e:\n"
                             "        print(f'SyntaxError: {e}')\n"
                             "    except Exception as e:\n"
                             "        print(f'Error: {e}')\n"
                             "check_syntax(r'%1')\n"
                             ).arg(backupPath);

        QString result = python_code(script);
        if (result.contains("SYNTAX_OK")) {
            // 4. 语法通过，覆盖原文件
            if (!QFile::remove(pyfile_name)) {
                if (!QFile::rename(backupPath, pyfile_name)) {
                    return "替换成功但无法覆盖原文件，请检查权限，备份保留在 " + backupPath;
                }
            } else {
                if (!QFile::rename(backupPath, pyfile_name)) {
                    return "替换成功但无法重命名备份为原文件，备份位于 " + backupPath;
                }
            }
            QFile::remove(backupPath);
            return "替换成功 语法检查通过";
        } else {
            // 5. 语法不通过，删除备份，保留原文件
            QFile::remove(backupPath);
            return "替换后语法检查 不通过 返回：" + result + "\n本次文件未替换 请更新数据后尝试";
        }
    }
    else {
        result = "错误：未知的工具名称 " + tool_name;
    }

    return result;
}

bool matchRule(const Rule &rule, const QString &msg);
QString onMessageReceived2(const MessageEvent &msg,int i) {

    try {
        py::gil_scoped_acquire gil;

        QString reply;
        // 1. 优先按规则匹配
        for (const Rule &rule : std::as_const(m_pluginList[i].python.rules)) {
            if (matchRule(rule, msg.msg)) {
                // 调用规则对应的函数
                py::object ret = rule.function(msg);
                if (!ret.is_none() && py::isinstance<py::str>(ret)) {
                    if(reply.isEmpty())
                        reply = QString::fromStdString(py::str(ret).cast<std::string>());
                    else
                        reply += "\n" + QString::fromStdString(py::str(ret).cast<std::string>());
                }
            }
        }
        return reply;


    } catch (const std::exception &e) {
        return "[Python] " + m_pluginList[i].name + " 错误: " + e.what();
    } catch (...) {
        return "[Python] " + m_pluginList[i].name + " 未知错误";
    }

}
void AppWindow::setModels()
{
    QString currentText = modelCombo->currentText();  // 假设 comboModel 是 QComboBox*
    modelCombo->clear();
    for (const auto &m : std::as_const(ai_ui->modelList))
    {
        modelCombo->addItem(m.name);
    }
    int index = modelCombo->findText(currentText);
    if (index != -1)
        modelCombo->setCurrentIndex(index);
}
void AppWindow::clearChat()
{
    if(m_dir.isEmpty()) return;
    W_file(m_dir+"/ai对话.json","{}");
    sxw = QJsonObject();
    chatTextEdit->clear();
}
QString AppWindow::Ai_posts(const QString &model) //内部使用请勿公开
{
    QString err;
    for ( const auto &m : std::as_const(ai_ui->modelList))
    {
        if(m.name==model)
        {
            sxw["model"] = m.name;
            int kswz = m.enabledInterfaceIndices.size();
            for(int i = 0;i<kswz;++i)//接口循环
            {
                //模型开始下标 原子+1
                int jk= model_index % m.enabledInterfaceIndices.size();//实时获取
                model_index++;
                auto &key = ai_ui->globalInterfaces[jk].keys;
                int len = key.size();
                for(int i2=0;i2< len;++i2)
                {
                    int index =  ai_ui->globalInterfaces[jk].key_index++;
                    index = index % len;
                    if(qxzd) return QString();
                    QString text =  Ai_post(ai_ui->globalInterfaces[jk].url,key[index].key,err);
                    if(text.isEmpty()) continue;
                    return text;
                }

            }
            return err;
        }
    }

    return "未找到："+model+" 模型 请重新打开本页面";
}

void AppWindow::onSendMessage(const QString &text)
{
    if(m_run)
    {
        qxzd = true;
        return ;
    }
    if(m_dir.isEmpty())
    {
        QMessageBox::warning(this, "", "请先打开一个文件夹，因为本 AI 对话会涉及操作文件");
        return;
    }
    m_run = true;
    sendBtn->setText("中断");
    // 2. 界面显示用户消息
    addMessage(text, true);


    if (sxw.contains("messages") && sxw["messages"].isArray()) {
        init_system(text);
    } else {
        // 如果 sxw 还没有 messages 数组，则直接创建一个新的
        QJsonArray msgs;

        // 1. 先添加系统提示词（必须用独立的 QJsonObject）
        if (!g_system.isEmpty()) {
            QJsonObject systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] =  subTextReplace(g_system,"{{文件目录}}",listDirectoryEntries(m_dir));
            msgs.append(systemMsg);
        }

        // 2. 再添加用户消息（用独立的 QJsonObject，避免覆盖）
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = text;
        msgs.append(userMsg);

        sxw["messages"] = msgs;
    }
    ai_ui->trimToolResponses(sxw, 5, 64); //限制工具长度
    ai_ui->trimContextByMessageCount(sxw, 128); //限制上下文
    QString model = modelCombo->currentText();
    sxw["tools"] = m_fun;
    m_execThread = QThread::create([this, model]() {
        QString aiReply = Ai_posts(model);
        sxw.remove("tools");
         addMessage(aiReply, false);
        QMetaObject::invokeMethod(this, [this, aiReply]() {

            W_file(m_dir + "/ai对话.json", QJsonDocument(sxw).toJson());
            m_run=false;
            qxzd=false;
            sendBtn->setText("发送");
            m_execThread->deleteLater();
            m_execThread = nullptr;
        }, Qt::QueuedConnection);
    });
    m_execThread->start();
}
void AppWindow::init_system(const QString &text)
{
    QJsonArray msgs = sxw["messages"].toArray();

    bool systemFound = false;

    for (int i = 0; i < msgs.size(); ++i) {
        QJsonObject msg = msgs[i].toObject();
        if (msg["role"].toString() == "system") {
            // 使用当前最新的目录列表更新 content
            msg["content"] = subTextReplace(g_system, "{{文件目录}}", listDirectoryEntries(m_dir));
            msgs[i] = msg;
            systemFound = true;
            break; // 通常只有一个 system，找到就停
        }
    }

    if (!systemFound && !g_system.isEmpty()) {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = subTextReplace(g_system, "{{文件目录}}", listDirectoryEntries(m_dir));
        msgs.insert(0, systemMsg); // 插在最前面
    }
    if(!text.isEmpty())
    {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = text;
        msgs.append(userMsg);
    }
    sxw["messages"] = msgs;
}

QString AppWindow::Ai_post(const QString &url, const QString &key,QString &err)
{
    for(int i=0; i<50; ++i) {
        QJsonObject obj;
        for(int i2=0; i2<3; ++i2) {
            //qDebug() << "上下文" << sxw;
            init_system("");//更新系统提示词
            QByteArray response = ai_ui->Ai_post(url, key, sxw, 1800000);
            if(response.isEmpty()) {
                err += "接口返回空\n";
                return QString();
            }

            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(response, &error);
            if (error.error != QJsonParseError::NoError) {
                err += "接口返回错误json:" + error.errorString()+"\n";
                if(err.contains(key)) err = subTextReplace(err, key, "...");
                return QString();
            }
            obj = doc.object();

            QJsonObject obj2 = obj["error"].toObject();
            QString error_mes = obj2["message"].toString();
            if(error_mes.contains("token")) {
                if (sxw.contains("messages") && sxw["messages"].isArray()) {
                    QJsonArray msgs = sxw["messages"].toArray();
                    if (msgs.size() > 1) {
                        msgs.removeAt(1);
                        sxw["messages"] = msgs;
                    }
                }
                continue;
            }
            if(!error_mes.isEmpty())
            {
                err += error_mes+"\n";
                return QString();
            }
            break;
        }

        QJsonArray arr = obj["choices"].toArray();
        if (arr.isEmpty()) {
            err += "返回的 choices 为空 请确认接口是否正确\n";
            return QString();
        }
        QJsonObject o2 = obj["usage"].toObject();
        int prompt = o2["prompt_tokens"].toInt();
        int completion = o2["completion_tokens"].toInt();
        QJsonObject obj2 = arr.at(0).toObject();
        QJsonObject obj3 = obj2["message"].toObject();
        QString text = obj3["content"].toString();
        const QJsonArray arr2 = obj3["tool_calls"].toArray();
        obj3.remove("reasoning_content");

        if (sxw.contains("messages") && sxw["messages"].isArray()) {
            QJsonArray msgs = sxw["messages"].toArray();
            msgs.append(obj3);
            sxw["messages"] = msgs;
        }
        bool ok = false;
        if (!arr2.isEmpty()) {


            for (const QJsonValue &value : arr2) {
                QJsonObject a = value.toObject();
                QJsonObject function = a["function"].toObject();
                QString tool_name = function["name"].toString();
                QString args = function["arguments"].toString();
                QString callID = a["id"].toString();
                QString data = tools_fun(tool_name,args,sxw["model"].toString());
                if(data.isEmpty())
                {
                    data = "调用函数完成 无返回值...";
                }
                if (!text.isEmpty()) {
                    addMessage(text +"\n调用工具："+tool_name +"\n\n参数："+args+"\n\n返回结果："+data+"\n\n输入:"+QString::number(prompt)+" 补全:"+QString::number(completion), false);
                    text = QString();
                }else
                    addMessage("调用工具："+tool_name +"\n\n参数："+args+"\n\n返回结果："+data+"\n\n输入:"+QString::number(prompt)+" 补全:"+QString::number(completion), false);
                if(!data.isEmpty())
                {
                    QJsonArray msgs = sxw["messages"].toArray();
                    QJsonObject toolMsg;
                    toolMsg["role"] = "tool";
                    toolMsg["content"] = data;
                    toolMsg["tool_call_id"] = callID;
                    toolMsg["name"] = tool_name;

                    msgs.append(toolMsg);
                    sxw["messages"] = msgs;
                    ok = true;
                }
            }
            if (!text.isEmpty()) {
                addMessage(text, false);
                text = QString();
            }
            if(qxzd) return QString();
            if (ok) continue;
        }

        return text;
    }
    return QString();
}


