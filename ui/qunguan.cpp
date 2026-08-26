#include "qunguan.h"
#include "global.h"
#include "ui_qunguan.h"
#include <QMessageBox>
int accinfo(int appid);
qunguan::qunguan(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::qunguan)
{
    ui->setupUi(this);


    connect(ui->listWidget, &QListWidget::itemClicked,
            this, &qunguan::onItemClicked);
}
QString getFieldValue(const std::shared_ptr<AccountInfo>& info, const QString& fieldName) {
    // 使用 if-else 或 switch 返回对应字段
    if (fieldName == "admin") return info->admin;
    if (fieldName == "caidan") return info->caidan;
    if (fieldName == "help") return info->help;
    if (fieldName == "emptyAt") return info->emptyAt;
    if (fieldName == "rqhy") return info->rqhy;
    if (fieldName == "tqhy") return info->tqhy;
    if (fieldName == "fallbackReply") return info->fallbackReply;
    if (fieldName == "welcomeMsg") return info->welcomeMsg;
    if (fieldName == "apply") return info->apply;
    if (fieldName == "jojnyz") return info->jojnhf;
    return QString();
}
void qunguan::refreshList() {
    ui->listWidget->clear();
    if (g_appid == 0) return;

    int idx = accinfo(g_appid);
    if (idx == -1) {

        return;
    }
    auto &info = m_accounts[idx];

    // 使用有序列表，顺序按你定义的顺序保持不变
    QList<QPair<QString, QString>> items = {
        {"机器人管理", "admin"},
        {"发送菜单", "caidan"},
        {"发送帮助", "help"},
        {"空艾特时", "emptyAt"},
        {"用户入群", "rqhy"},
        {"用户退群", "tqhy"},
        {"加群验证", "jojnyz"},
        {"未命中指令", "fallbackReply"},
        {"机器人入群", "welcomeMsg"},
        {"有人申请加群", "apply"}

    };

    for (const auto &pair : items) {
        QString displayText = pair.first;
        QString fieldName = pair.second;
        QListWidgetItem *item = new QListWidgetItem(displayText);

        QString content = getFieldValue(info, fieldName);
        item->setData(Qt::UserRole, content);
        item->setData(Qt::UserRole + 1, fieldName);

        ui->listWidget->addItem(item);
        if(m_currentField == fieldName)
        {
            ui->listWidget->setCurrentItem(item);
            ui->textEdit->setText(content); // 或者 item->data(Qt::UserRole).toString()
        }
    }
}
void setFieldValue(const std::shared_ptr<AccountInfo>& info, const QString& fieldName, const QString& value) {
    if (fieldName == "admin") info->admin = value;
    else if (fieldName == "caidan") info->caidan = value;
    else if (fieldName == "help") info->help = value;
    else if (fieldName == "emptyAt") info->emptyAt = value;
    else if (fieldName == "rqhy") info->rqhy = value;
    else if (fieldName == "tqhy") info->tqhy = value;
    else if (fieldName == "fallbackReply") info->fallbackReply = value;
    else if (fieldName == "welcomeMsg") info->welcomeMsg = value;
    else if (fieldName == "apply") info->apply = value;
    else if (fieldName == "jojnyz") info->jojnhf = value;
}


void qunguan::onItemClicked(QListWidgetItem *item) {
    if (!item) return;
    if (g_appid == 0) {
        QMessageBox::warning(this, "提示", "请先选择有效的机器人");
        return;
    }

    // 显示内容
    QString content = item->data(Qt::UserRole).toString();
    ui->textEdit->setText(content);
    QString fieldName = item->data(Qt::UserRole+1).toString();
    if(fieldName=="rqhy")
    {
        ui->textEdit->setPlaceholderText(R"(用户加群时#python开头为 py代码
python:
UserList = 用户ID列表
UserNameList = 用户昵称列表 长度和 UserList 一样
g_appid = 当前事件appid
api = 和写代码一样的api 有api使用
__result__ = "返回的内容"

非py代码时可用变量 {id} {艾特} {数量} {头像} {混合} {群名} {昵称}
当开始延迟时 统计 {混合} 排版? {混合x} 直接显示昵称 {混合}为艾特

)");
    }
    else if(fieldName=="apply")
    {
        ui->textEdit->setPlaceholderText(R"(用户申请加群时#python开头为 py代码(仅限全量群)
====
msg.groupid      : 群ID 发送消息无条件使用这个字段 私聊环境也可用传这个参数 包括 频道 和 频道私聊 因为可用让代码同时支持 各种事件来源(字符串)
msg.user         : 发送者标识 32字节hex(字符串)
msg.msg          : 消息内容 里面包含[image,name=xxx,url=xxx] 另外还有 语音[audio,name=xx,url=xx] 视频[video,name=xx,url=xx] 文件[file,name=xx,url=xx]等标签 与艾特标签'<@user>' 32字节hex 不需要区分是否艾特了你 取到的必定是其他用户 (字符串)
msg.appid        : 应用/机器人 ID(整数)
msg.type         : 事件类型（如群聊、私聊等）0群聊 1判断 2私聊 3判断私聊(整数)
msg.nickname     : 发送者昵称
msg.guildId      : 频道/服务器 ID（仅频道消息有效）(字符串)
msg.raw          : 原始数据（JSON 字符串） (字符串)
msg.callbackid   : 回调 ID（用于匹配异步回调） (字符串)
msg.groupname    : 群昵称
msg.user2        : 目前已知 用于群聊申请加群时 邀请人
===
msg = 事件结构体
api = 和写代码一样的api 有api使用
api.set_join_request(msg.appid,msg.groupid,msg.user,true,msg.callbackid，false,"这里是拒绝理由") <-同意某个人入群 true 是同意  callbackid，false 后面的是 是否拉黑名单
__result__ = "返回的内容"


非py代码时可用变量 {ID} {申请理由} {头像} {昵称} {群昵称} {ReqId}
没入群没法获取昵称 用的缓存前提框架有记录 以及部分全局变量

)");
    }
    else if(fieldName=="tqhy")
    {
        ui->textEdit->setPlaceholderText(R"(用户退群时#python开头为 py代码(仅限全量群)
python:
UserList = 用户ID列表
UserNameList = 用户昵称列表 长度和 UserList 一样
g_appid = 当前事件appid
api = 和写代码一样的api 有api使用
__result__ = "返回的内容"

非py代码时可用变量 {id} {数量} {头像} {混合} {混合x} {群名} {昵称}
当开始延迟时 统计 {混合} 排版? 退群后艾特无效 {混合x}里面有昵称 请注意违禁词
 {混合x} 直接显示昵称 {混合}为艾特 以及部分全局变量
)");
    }
    else if(fieldName=="jojnyz")
    {
        ui->textEdit->setPlaceholderText(R"(====
msg.groupid      : 群ID 发送消息无条件使用这个字段 私聊环境也可用传这个参数 包括 频道 和 频道私聊 因为可用让代码同时支持 各种事件来源(字符串)
msg.user         : 发送者标识 32字节hex(字符串)
msg.msg          : 消息内容 里面包含[image,name=xxx,url=xxx] 另外还有 语音[audio,name=xx,url=xx] 视频[video,name=xx,url=xx] 文件[file,name=xx,url=xx]等标签 与艾特标签'<@user>' 32字节hex 不需要区分是否艾特了你 取到的必定是其他用户 (字符串)
msg.appid        : 应用/机器人 ID(整数)
msg.type         : 事件类型（如群聊、私聊等）0群聊 1判断 2私聊 3判断私聊(整数)
msg.nickname     : 发送者昵称
msg.guildId      : 频道/服务器 ID（仅频道消息有效）(字符串)
msg.raw          : 原始数据（JSON 字符串） (字符串)
msg.callbackid   : 回调 ID（用于匹配异步回调） (字符串)
msg.groupname    : 群昵称
msg.user2        : 目前已知 用于群聊申请加群时 邀请人
===
msg = 事件结构体
api = 和写代码一样的api 有api使用
__result__ = "返回的内容"


非py代码时可用变量 {ID} {头像} {昵称} {群昵称} {日期}
没入群没法获取昵称 用的缓存前提框架有记录 以及部分全局变量
)");
    }

    else if(fieldName=="welcomeMsg")
    {
        ui->textEdit->setPlaceholderText(R"(机器人被邀请加群时#python开头为python代码 有msg事件)");
    }else
    {
        ui->textEdit->setPlaceholderText("无明显提示呢...");
    }
    m_currentField = item->data(Qt::UserRole + 1).toString();
}

qunguan::~qunguan()
{
    delete ui;
}

void qunguan::on_pushButton_clicked() {
    if (g_appid == 0) {
        QMessageBox::warning(this, "提示", "请先选择有效的机器人");
        return;
    }

    int index = accinfo(g_appid);
    if (index == -1) {
        QMessageBox::warning(this, "失败", "指定的机器人不在账号列表中，请重新选择");
        return;
    }
    auto &info = m_accounts[index];

    // ---- 1. 保存原本的独立控件（times, tiaoshu, 复选框等） ----
    info->times = ui->time_Edit->text().toInt();
    info->tiaoshu = ui->tiao_Edit->text().toInt();
    info->pbbot = ui->checkBox_2->isChecked();
    info->autoht = ui->zdht->isChecked();
    info->rq_ychf = ui->lineEdit->text().toInt();
    info->rq_lq = ui->lineEdit_2->text().toInt();
    info->tq_ychf = ui->lineEdit_3->text().toInt();
    info->tq_lq = ui->lineEdit_4->text().toInt();
    info->cbl = ui->checkBox->isChecked();
    // ---- 2. 保存当前编辑的文本内容（如果存在） ----
    if (!m_currentField.isEmpty()) {
        QString newContent = ui->textEdit->toPlainText(); // 假设是纯文本
        setFieldValue(info, m_currentField, newContent);

        // ---- 3. 更新列表中对应项的数据（让列表项显示新内容） ----
        QListWidgetItem *currentItem = ui->listWidget->currentItem();
        if (currentItem) {
            // 更新存储的内容数据
            currentItem->setData(Qt::UserRole, newContent);
            // 注意：不要修改显示文本（即 item 的 text），因为那是标题，不是内容
            // 如果想在列表项中显示部分内容，可以额外设置，这里不需要
        }
    } else {
        // 提示用户没有选中任何列表项，但保存依然进行（只保存独立控件）
        QMessageBox::information(this, "提示", "独立设置已保存，但未选中任何列表项，文本内容未保存");
    }

    // ---- 4. 持久化 ----
    accountPage->saveAccounts(info.get());

    QMessageBox::information(this, "成功", "保存成功");
}

void qunguan::列表行被单击()
{
    if (g_appid!=0) {
        int index=accinfo(g_appid);
        if(index==-1) return;
        auto &info = m_accounts [index];
        ui->time_Edit->setText(QString::number(info->times));
        ui->tiao_Edit->setText(QString::number(info->tiaoshu));
        ui->checkBox->setChecked(info->cbl);
        ui->checkBox_2->setChecked(info->pbbot);
        ui->textEdit->setText(info->admin);
        ui->zdht->setChecked(info->autoht);


        ui->lineEdit->setText(QString::number(info->rq_ychf));
        ui->lineEdit_2->setText(QString::number(info->rq_lq));
        ui->lineEdit_3->setText(QString::number(info->tq_ychf));
        ui->lineEdit_4->setText(QString::number(info->tq_lq));

        refreshList();

    }
}







