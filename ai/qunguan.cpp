#include "qunguan.h"
#include "global.h"
#include "ui_qunguan.h"
#include <QMessageBox>
#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <utility>

int accinfo(int appid);

namespace {

// 「全部机器人」模式下点「保存页面」弹出的批量应用窗口：勾选目标机器人。
//
// 故意不写 Q_OBJECT —— 它只用到 QDialog 自带的 accept()/reject() 和 lambda，
// 不需要新增信号槽，放在 .cpp 里就不用额外的 moc 产物（也不用动工程文件）。
class BatchTargetDialog : public QDialog
{
public:
    explicit BatchTargetDialog(const QString &pageName, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QString("批量保存 · %1").arg(pageName));
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        resize(460, 520);
        setMinimumSize(400, 380);

        // 只用带选择器的规则：不带选择器的声明块在 Qt 里等价于 * {...}，会传播给所有后代控件
        // （账号卡片按钮变透明的那个坑）。
        setStyleSheet(
            "#batchTitle { font-size: 14px; font-weight: 700; color: #26313F; background: transparent; }"
            "#batchHint  { font-size: 12px; color: #8A94A6; background: transparent; }"
            "#batchCount { font-size: 12px; color: #8A94A6; background: transparent; }"
            "#batchList  { background: #FFFFFF; border: 1px solid #F0E3D3; border-radius: 8px; }"
            "#batchList::item { padding: 5px 2px; }"
            "#batchList::item:selected { background: #FFF0DE; color: #26313F; }"
            "QPushButton#batchTool { padding: 3px 12px; font-size: 12px; }"
            );

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(14, 12, 14, 12);
        root->setSpacing(8);

        auto *title = new QLabel(QString("把当前「%1」的文本批量保存到：").arg(pageName), this);
        title->setObjectName("batchTitle");
        root->addWidget(title);

        auto *hint = new QLabel("只写左侧当前页面的文本内容。上方输入框和勾选项属于单个账号的设置，"
                                "批量保存不会改动它们。", this);
        hint->setObjectName("batchHint");
        hint->setWordWrap(true);
        root->addWidget(hint);

        auto *tools = new QHBoxLayout;
        tools->setSpacing(6);
        auto *btnAll  = new QPushButton("全选", this);
        auto *btnNone = new QPushButton("全不选", this);
        auto *btnInv  = new QPushButton("反选", this);
        for (QPushButton *b : { btnAll, btnNone, btnInv }) {
            b->setObjectName("batchTool");
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumHeight(26);
            tools->addWidget(b);
        }
        tools->addStretch();
        m_countLabel = new QLabel(this);
        m_countLabel->setObjectName("batchCount");
        tools->addWidget(m_countLabel);
        root->addLayout(tools);

        m_list = new QListWidget(this);
        m_list->setObjectName("batchList");
        m_list->setSpacing(1);
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        root->addWidget(m_list, 1);

        auto *bottom = new QHBoxLayout;
        bottom->addStretch();
        auto *cancelBtn = new QPushButton("取消", this);
        m_okBtn = new QPushButton("保存", this);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        m_okBtn->setCursor(Qt::PointingHandCursor);
        bottom->addWidget(cancelBtn);
        bottom->addWidget(m_okBtn);
        root->addLayout(bottom);

        connect(btnAll,  &QPushButton::clicked, this, [this]() { setAllState(Qt::Checked); });
        connect(btnNone, &QPushButton::clicked, this, [this]() { setAllState(Qt::Unchecked); });
        connect(btnInv,  &QPushButton::clicked, this, [this]() { invertAll(); });
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_okBtn,   &QPushButton::clicked, this, [this]() { accept(); });
        connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { updateCount(); });

        updateCount();
    }

    // 默认全部勾选
    void addTarget(int appid, const QString &text)
    {
        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, appid);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }

    QList<int> selectedAppids() const
    {
        QList<int> out;
        for (int i = 0; i < m_list->count(); ++i) {
            const QListWidgetItem *item = m_list->item(i);
            if (item && item->checkState() == Qt::Checked)
                out << item->data(Qt::UserRole).toInt();
        }
        return out;
    }

protected:
    void accept() override
    {
        if (selectedAppids().isEmpty()) {
            QMessageBox::warning(this, "批量保存", "请至少勾选一个机器人");
            return;
        }
        QDialog::accept();
    }

private:
    void setAllState(Qt::CheckState state)
    {
        const QSignalBlocker blocker(m_list);   // 批量改状态时不逐条刷新计数
        for (int i = 0; i < m_list->count(); ++i)
            m_list->item(i)->setCheckState(state);
        updateCount();
    }

    void invertAll()
    {
        const QSignalBlocker blocker(m_list);
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *item = m_list->item(i);
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
        updateCount();
    }

    void updateCount()
    {
        const int picked = selectedAppids().size();
        const int total  = m_list->count();
        if (m_countLabel)
            m_countLabel->setText(QString("已选 %1 / %2").arg(picked).arg(total));
        if (m_okBtn)
            m_okBtn->setText(QString("保存到 %1 个机器人").arg(picked));
    }

    QListWidget *m_list = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_okBtn = nullptr;
};

} // namespace
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
    if (fieldName == "welcomeMsg2") return info->welcomeMsg2;
    if (fieldName == "apply") return info->apply;
    if (fieldName == "jojnyz") return info->jojnhf;
    if (fieldName == "xxwb") return info->xxwb;
    if (fieldName == "ldsd") return info->ldsd;
    if (fieldName == "gxrz") return info->gxrz;
    if (fieldName == "qiandao") return info->qiandao;   // 空 = 静默签到，示例只放在 placeholder
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
        {"信息尾巴", "xxwb"},
        {"广告完成", "ldsd"},
        {"签到", "qiandao"},
        {"更新日志", "gxrz"},
        {"未命中指令", "fallbackReply"},
        {"机器人入群", "welcomeMsg"},
        {"被添加好友", "welcomeMsg2"},
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
        if (m_currentField == fieldName)
            ui->listWidget->setCurrentItem(item);
    }

    // 让文本框和「当前字段」保持一致：内容 + 灰色提示
    // （提示是 setPlaceholderText 设的，只有文本框为空时才会画出来）
    if (!m_currentField.isEmpty()) {
        for (int i = 0; i < ui->listWidget->count(); ++i) {
            QListWidgetItem *it = ui->listWidget->item(i);
            if (it->data(Qt::UserRole + 1).toString() == m_currentField) {
                onItemClicked(it);
                break;
            }
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
    else if (fieldName == "welcomeMsg2") info->welcomeMsg2 = value;
    else if (fieldName == "apply") info->apply = value;
    else if (fieldName == "jojnyz") info->jojnhf = value;
    else if (fieldName == "xxwb") info->xxwb = value;
    else if (fieldName == "ldsd") info->ldsd = value;
    else if (fieldName == "gxrz") info->gxrz = value;
    else if (fieldName == "qiandao") info->qiandao = value;
}


void qunguan::onItemClicked(QListWidgetItem *item) {
    if (!item) return;
    if (g_appid == 0) {
        QMessageBox::warning(this, "提示", "请先选择有效的机器人");
        return;
    }

    // 显示内容
    QString content = item->data(Qt::UserRole).toString();
    QString fieldName = item->data(Qt::UserRole+1).toString();
    // 签到是 py 代码：必须走 setPlainText，否则源码里的 `<` 会被 setText 当 HTML 标签吃掉
    if (fieldName == "qiandao") ui->textEdit->setPlainText(content);
    else                        ui->textEdit->setText(content);
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
msg.nickname2    : 发送者审核后昵称 可能是空
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
msg.nickname2    : 发送者审核后昵称 可能是空
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

    else if(fieldName=="qiandao")
    {
        // 和「默认值」共用同一份内容（core/accountinfo.cpp: defaultQiandaoTemplate）
        ui->textEdit->setPlaceholderText(defaultQiandaoTemplate());
    }
    else if(fieldName=="welcomeMsg2")
    {
        ui->textEdit->setPlaceholderText(R"(被添加好友时#python开头为 python 代码 有 msg 事件)");
    }
    else if(fieldName=="welcomeMsg")
    {
        ui->textEdit->setPlaceholderText(R"(机器人被邀请加群时#python开头为python代码 有msg事件)");
    }
    else if(fieldName=="apply")
    {
        ui->textEdit->setPlaceholderText(R"(有个变量 [匹配指令] 他是 当用户输入指令时 匹配相关指令)");
    }
    else
    {
        ui->textEdit->setPlaceholderText("无明显提示呢...");
    }
    // 占位符只在「文本框为空」时才会画：内容为空时先 clear + 强制重绘
    // （只写空格 / 空行的文档 Qt 也算非空，会把提示挡住）
    if (content.trimmed().isEmpty()) {
        ui->textEdit->clear();
        ui->textEdit->viewport()->update();
    }

    m_currentField = item->data(Qt::UserRole + 1).toString();
}

qunguan::~qunguan()
{
    delete ui;
}

void qunguan::on_pushButton_clicked() {
    // 标题栏选的是「全部机器人」时没有具体账号可写 → 转成批量保存（勾选目标机器人）
    if (m_currentBotIndex < 0) {
        batchSaveCurrentField();
        return;
    }

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
    info->ychfwcl = ui->checkBox_3->isChecked();
    info->action_button = ui->checkBox_4->isChecked();
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
    }

    // ---- 4. 持久化 ----
    accountPage->saveAccounts(info.get());

    QMessageBox::information(this, "成功", "保存成功");
}

QString qunguan::currentPageName() const
{
    if (QListWidgetItem *item = ui->listWidget->currentItem())
        return item->text();
    return m_currentField;
}

// 「全部机器人」模式下的保存：弹窗勾选目标机器人，把当前页面的文本批量写过去。
// 只动文本字段（m_currentField）—— 上方的 times / tiaoshu / 各复选框是每个账号各自的设置，
// 批量覆盖会把别的机器人调好的数值抹掉，所以这里一个字都不碰。
void qunguan::batchSaveCurrentField()
{
    if (m_accounts.isEmpty()) {
        QMessageBox::warning(this, "批量保存", "还没有任何机器人账号，无法批量保存");
        return;
    }
    if (m_currentField.isEmpty()) {
        QMessageBox::warning(this, "批量保存", "请先在左侧选中一个页面（比如「签到」），再点保存");
        return;
    }

    const QString pageName = currentPageName();
    const QString content  = ui->textEdit->toPlainText();

    BatchTargetDialog dlg(pageName, this);
    for (const auto &info : std::as_const(m_accounts)) {
        if (!info) continue;
        const QString name   = info->nickname.isEmpty() ? "未命名机器人" : info->nickname;
        const QString botId  = info->appid.isEmpty() ? "未配置 AppID" : info->appid;
        const QString status = info->online ? "● 在线" : "○ 离线";
        dlg.addTarget(info->appid_int, QString("%1   %2   %3").arg(name, botId, status));
    }

    if (dlg.exec() != QDialog::Accepted) return;

    const QList<int> targets = dlg.selectedAppids();
    int saved = 0;
    for (int appid : targets) {
        const int index = accinfo(appid);
        if (index == -1) continue;
        auto &info = m_accounts[index];
        if (!info) continue;

        setFieldValue(info, m_currentField, content);
        accountPage->saveAccounts(info.get());
        ++saved;
    }

    // 列表项里缓存的那份也同步一下，切回该页面时看到的是刚保存的内容
    if (QListWidgetItem *item = ui->listWidget->currentItem())
        item->setData(Qt::UserRole, content);

    QMessageBox::information(this, "批量保存",
                             QString("已把「%1」的文本保存到 %2 个机器人").arg(pageName).arg(saved));
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
        ui->checkBox_3->setChecked(info->ychfwcl);
        ui->checkBox_4->setChecked(info->action_button);

        ui->zdht->setChecked(info->autoht);


        ui->lineEdit->setText(QString::number(info->rq_ychf));
        ui->lineEdit_2->setText(QString::number(info->rq_lq));
        ui->lineEdit_3->setText(QString::number(info->tq_ychf));
        ui->lineEdit_4->setText(QString::number(info->tq_lq));

        refreshList();

        // 首次打开 / 还没选字段：默认选中第一项，
        // 否则文本框会空着、连灰色提示都没有
        if (m_currentField.isEmpty() && ui->listWidget->count() > 0)
            onItemClicked(ui->listWidget->item(0));
    }
}







