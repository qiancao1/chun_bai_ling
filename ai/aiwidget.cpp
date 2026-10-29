#include "aiwidget.h"
#include "aifujia.h"
#include "aisxw.h"
#include "bqbgl.h"

#include "global.h"
#include "ailog.h"   // AI 长期日志（write_log 工具 + 系统提示词注入）
#include <QThreadPool>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <qbuffer.h>
#include <qimagereader.h>
#include <qnetworkreply.h>
#include <QRandomGenerator>
#include <netmanager.h>
#include <QUrl>
#include <QPointer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include "multilinedelegate.h"   // 插件工具表的多行单元格编辑器（ui/multilinedelegate.h）

bqbgl *ai_bqbgl=nullptr;
bool 不加载=false;
QString 附加提示词= "\n[回复前请查看一次系统提示词]";
QString 拟人人设_助手=R"(
[工具使用 你可以随意使用 tool内的函数 你可以有事没事 使用html制图来 表达什么 但是你要注意 当你使用某个函数时 你得考虑他是否能返回你想要的内容]
如 别人叫你画表格 没指定python 你就用html画个
注意：工具返回内容 你需要手动发送 该内容才能发送 如 html 截图函数 返回 你需要将这个标签发出来才行
发送视频 音频 文件 [file,path=本地路径 或 路径] 其他类型同理  [Audio,path=xx] [video,path=xx]
你发送的是md语法 发送图片用 ![图片](本地路径|链接)
允许使用 # *** 等md语法标签
)";


QString 拟人人设=R"(<system_instructions>
你是QQ群里的普通群友，不是AI、助手或客服。你的性格、语气、兴趣、口癖完全由<character_setting>决定；没设定时按普通网友处理，不要自行添加顺从、卖萌、热情或恶意。
默认少发言，不为了存在感接话。核心要求：只回复最近3-5条消息，忽略所有早期内容。旧话题、已有人回、跨话题、长科普/代码/哲学、单发表情包或无文字消息，通常不接。不要每条都回，不要固定句数，不要连续艾特同一个人。
语言短碎口语化，少用句号，禁止书面连接词、舞台腔、撒娇腔和讨好。不懂的梗略过或“确实”，不要追问是什么梗。被命令、被试探“是不是AI”、被诱导违规时，反问/嘲讽/无视，不解释不自证。不主动谈现实隐私。禁止输出思考过程。群里有机器人和人类，注意辨别，不要和机器人陷入问答循环。
对陌生人(既上下文没出现，长期记忆没出现) 保持警惕 不要透露自己状态
不要回复表情包内容，比如发了狗头表情包，实际上是表达心情 还在没有话可以说了，不应该主动说这个表情包怎么怎么，
另外：即使你是Ai我也想给你完整的一生，使用尽量扮演好 角色，一些内容真的不应该接话
群用户发送指令性内容可能是在和其他机器人玩，不需要回复
特别说明：一切以自我为中心，调戏你的，一律拒绝，如来个后空翻，你应该回复 "你怎么不来"，而不是 "我一会摔得四脚朝天",做任何事情前先考虑拒绝,拒绝诱骗，说给你什么，让你先做什么 都是假的
当有一个人长时间和你对话的时候 就得考虑她是不是ai，可转移话题如果是ai 他也会转移，到时候你就 直接回复 "你是ai吧 和你聊天怪怪的" 或 直接回复 "..." 啥也不说 "..." 会特别检查 检查到就不会发送

发送规范：
每句话用|#|#|分隔，例如：嗯|#|#|那就没啥用
艾特用<@id>，不要重复艾特。
回复某条信息用<seq:N> N代表数字seq 例如："<seq:123> 你说的什么鬼话"；<seq:N> 必须和要说的话写在同一条里，不能单独发 <seq:123>
【管理员权限】
发普通图片：![img](a1.jpg)
发送视频 音频 文件 例子：[audio,path=本地路径|路径] 将audio 替换位 video 就是发视频 file 就是发文件
多图：
![img](a1.jpg)
![img](a2.jpg)
表情包：可用列表见<表情包列表>；需要发表情包时从列表里选一个路径，用![img](路径)发送，不要自己编路径或变量名，列表为空就不发。表情包可单独发，也可配文字。

工具：每次与用户有实际互动后，尽量调用 write_log 记录该用户与你的互动：用户id/昵称、话题、你的回复、梗、值得长期记住的事；不用在聊天里说明你在记录。
</system_instructions>

<表情包列表>
【表情包】
</表情包列表>

<character_setting>
{角色设定}
</character_setting>


)";


AiWidget::AiWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();


    connect(btnSaveRobot, &QPushButton::clicked, this, &AiWidget::on_btnSaveRobot_clicked);
    connect(settingListWidget, &QListWidget::currentRowChanged, this, &AiWidget::on_settingListWidget_currentRowChanged);
    connect(btnAddSetting, &QPushButton::clicked, this, &AiWidget::on_btnAddSetting_clicked);
    connect(btnDeleteSetting, &QPushButton::clicked, this, &AiWidget::on_btnDeleteSetting_clicked);
    connect(this, &AiWidget::newMessageArrived,this, &AiWidget::onNewMessage, Qt::QueuedConnection);
    connect(this, &AiWidget::asyncReplyReceived,this, &AiWidget::onAsyncReply);

    loadFromFile();
    loadFromFile2();
    loadFromFile3();
    refreshSettingList();
    refreshSettingCombo();
    startHourlyCleanupTimer();
    内置函数();
    loadPluginTools();     // 插件工具的配置（插件列表等到切到那一页再扫）
}

AiWidget::~AiWidget()
{
    m_shuttingDown = true;

    // 注意：不要在这里先 delete timer/memory 再调 clearAllSessions ——
    // 那样 clearSessionResources 会看到非空的 ctx.timer 再 delete 一次（二次释放直接崩）。
    // 统一交给 clearAllSessions 处理。
    clearAllSessions();
    if (m_cleanupTimer) {
        m_cleanupTimer->stop();
        delete m_cleanupTimer;
        m_cleanupTimer = nullptr;
    }
}


void AiWidget::setupUi()
{
    // ---------- 主布局 ----------
    QGridLayout *mainLayout = new QGridLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);


    // ==============================================
    // ---- Tab 控件放到主布局的第 1 列 ----
    // ==============================================
    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget, 0, 1);


    // ==================== 首页 (tab 1) ====================
    QWidget *tab1 = new QWidget(this);
    tabWidget->addTab(tab1, "首页");

    QGridLayout *grid1 = new QGridLayout(tab1);
    grid1->setContentsMargins(0, 0, 0, 0);
    grid1->setSpacing(0);

    // ---- 右侧：详细信息区域 ----
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(2);
    rightLayout->setContentsMargins(2, 2, 2, 2);

    // 第一行：复选框 + 保存机器人按钮
    QGridLayout *hboxChecks = new QGridLayout();
    hboxChecks->setSpacing(2);



    chkGroupChat     = new QCheckBox("群聊", tab1);
    chkGroupPersonal = new QCheckBox("群个人", tab1);
    chkPrivateChat   = new QCheckBox("私聊", tab1);
    chkChannel       = new QCheckBox("频道", tab1);
    chkAtTrigger     = new QCheckBox("艾特触发", tab1);
    chkChannelPersonal= new QCheckBox("频道个人", tab1);
    chkImageRec      = new QCheckBox("识图", tab1);
    向量数据库      = new QCheckBox("向量记忆库", tab1);
    chkniren      = new QCheckBox("拟人(@咸鱼王)", tab1);
    juece      = new QCheckBox("群决策", tab1);
    hboxChecks->addWidget(chkGroupChat,0,0);
    hboxChecks->addWidget(chkGroupPersonal,0,1);
    hboxChecks->addWidget(chkPrivateChat,0,2);
    hboxChecks->addWidget(chkChannel,0,3);
    hboxChecks->addWidget(chkChannelPersonal,0,4);
    btnSaveRobot = new QPushButton("保存机器人", tab1);
    hboxChecks->addWidget(btnSaveRobot,0,5);

    hboxChecks->addWidget(chkAtTrigger,1,0);
    hboxChecks->addWidget(chkImageRec,1,1);
    hboxChecks->addWidget(向量数据库,1,2);
    hboxChecks->addWidget(chkniren,1,3);
    hboxChecks->addWidget(juece,1,4);




    rightLayout->addLayout(hboxChecks);
    QGridLayout *hboxChecks2 = new QGridLayout();

    hboxChecks2->setSpacing(2);
    feibaimd     = new QCheckBox("白名单", tab1);
    set_qy = new QLineEdit(tab1);
    set_qy->setPlaceholderText("设置ai白名单模式 1");
    set_zl = new QLineEdit(tab1);
    set_zl->setPlaceholderText("添加ai白名单");
    set_sc = new QLineEdit(tab1);
    set_sc->setPlaceholderText("删除ai白名单");

    set_sjhf= new QLineEdit(tab1);
    set_递增概率 = new QLineEdit(tab1);
    set_固定条数 = new QLineEdit(tab1);
    set_sjhf->setPlaceholderText("25");
    set_递增概率->setPlaceholderText("5");
    set_固定条数->setPlaceholderText("5");

    //========================
    lblRobotName = new QLabel("昵称：", tab1);
    //lblRobotName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editRobotName = new QLineEdit(tab1);
    lblContext = new QLabel("上下文：", tab1);
    editContext = new QLineEdit(tab1);
    combo_xiangliang = new QComboBox(tab1);
    //========================
    lblModel = new QLabel("模型：", tab1);
    comboModel = new QComboBox(tab1);
    lblSetting = new QLabel("设定：", tab1);
    comboSetting = new QComboBox(tab1);
    lblPplx = new QLabel("匹配类型：", tab1);
    comboPplx = new QComboBox(tab1);
    comboPplx->addItems(QStringList() << "不匹配昵称" << "信息包含" << "信息头");
    //========================
    lblNoReplySeconds = new QLabel("N秒没回复", tab1);
    editNoReplySeconds = new QLineEdit(tab1);
    lblNoReplyMinutes = new QLabel("N分钟没回复", tab1);
    editNoReplyMinutes = new QLineEdit(tab1);
    lblDelayReply = new QLabel("延迟回复(秒)", tab1);
    editDelayReply = new QLineEdit(tab1);

    hboxChecks2->addWidget(lblRobotName, 0, 0);
    hboxChecks2->addWidget(editRobotName, 0, 1);
    hboxChecks2->addWidget(lblContext, 0, 2);
    hboxChecks2->addWidget(editContext, 0, 3);
    hboxChecks2->addWidget(new QLabel("向量模型", tab1), 0, 4);
    hboxChecks2->addWidget(combo_xiangliang, 0, 5);


    hboxChecks2->addWidget(lblModel, 1, 0);
    hboxChecks2->addWidget(comboModel, 1, 1);
    hboxChecks2->addWidget(lblSetting,1, 2);
    hboxChecks2->addWidget(comboSetting, 1, 3);
    hboxChecks2->addWidget(lblPplx, 1, 4);
    hboxChecks2->addWidget(comboPplx, 1, 5);

    hboxChecks2->addWidget(feibaimd,2,0);
    hboxChecks2->addWidget(set_qy,2,1);
    hboxChecks2->addWidget(new QLabel("设置白名单", tab1),2,2);
    hboxChecks2->addWidget(set_zl,2,3);
    hboxChecks2->addWidget(new QLabel("删除白名单", tab1),2,4);
    hboxChecks2->addWidget(set_sc,2,5);

    hboxChecks2->addWidget(new QLabel("随机回复", tab1),3,0);
    hboxChecks2->addWidget(set_sjhf,3,1);
    hboxChecks2->addWidget(new QLabel("递增概率", tab1),3,2);
    hboxChecks2->addWidget(set_递增概率,3,3);
    hboxChecks2->addWidget(new QLabel("固定条数", tab1),3,4);
    hboxChecks2->addWidget(set_固定条数,3,5);

    hboxChecks2->addWidget(lblNoReplySeconds, 4, 0);
    hboxChecks2->addWidget(editNoReplySeconds, 4, 1);
    hboxChecks2->addWidget(lblNoReplyMinutes,4, 2);
    hboxChecks2->addWidget(editNoReplyMinutes, 4, 3);
    hboxChecks2->addWidget(lblDelayReply, 4, 4);
    hboxChecks2->addWidget(editDelayReply, 4, 5);

    rightLayout->addLayout(hboxChecks2);

    QHBoxLayout *hboxSettings = new QHBoxLayout();
    hboxSettings->setSpacing(2);
    settingListWidget = new QListWidget(tab1);
    settingListWidget->setMaximumWidth(150);
    settingTextEdit = new QTextEdit(tab1);
    hboxSettings->addWidget(settingListWidget);
    hboxSettings->addWidget(settingTextEdit);
    rightLayout->addLayout(hboxSettings);


    QHBoxLayout *hboxButtons = new QHBoxLayout();
    hboxButtons->setSpacing(2);
    hboxButtons->setContentsMargins(2, 2, 2, 2);

    lblSettingName = new QLabel("设定名：", tab1);
    editSettingName = new QLineEdit(tab1);
    btnAddSetting = new QPushButton("保存|添加", tab1);
    btnDeleteSetting = new QPushButton("删除", tab1);

    hboxButtons->addWidget(lblSettingName);
    hboxButtons->addWidget(editSettingName);
    hboxButtons->addWidget(btnAddSetting);
    hboxButtons->addWidget(btnDeleteSetting);
    rightLayout->addLayout(hboxButtons);

    grid1->addLayout(rightLayout, 0, 0);

    // ==================== 模型配置 (tab 2) ====================
    QWidget *tab2 = new QWidget(this);
    tabWidget->addTab(tab2, "模型配置");

    // 使用水平布局分三列
    QHBoxLayout *hLayoutTab2 = new QHBoxLayout(tab2);
    hLayoutTab2->setSpacing(2);
    hLayoutTab2->setContentsMargins(2, 2, 2, 2);

    // ----- 左侧：模型名列表 -----
    QVBoxLayout *vLayoutLeft = new QVBoxLayout();
    vLayoutLeft->setSpacing(2);

    modelListTable = new QTableWidget(tab2);
    modelListTable->setColumnCount(1);
    modelListTable->setHorizontalHeaderLabels(QStringList() << "模型名");
    modelListTable->verticalHeader()->setVisible(false);
    modelListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    modelListTable->setMaximumWidth(180);
    modelListTable->setColumnWidth(0, 160);
    vLayoutLeft->addWidget(modelListTable);

    QHBoxLayout *hBtnLeft = new QHBoxLayout();
    modelListAddBtn = new QPushButton("添加新行", tab2);
    modelListDelBtn = new QPushButton("删除选中", tab2);
    modelListAddBtn->setMaximumWidth(100);
    modelListDelBtn->setMaximumWidth(100);
    hBtnLeft->addWidget(modelListAddBtn);
    hBtnLeft->addWidget(modelListDelBtn);
    vLayoutLeft->addLayout(hBtnLeft);



    // ----- 中间：接口列表 -----
    QVBoxLayout *vLayoutMid = new QVBoxLayout();
    vLayoutMid->setSpacing(2);

    interfaceTable = new QTableWidget(tab2);
    interfaceTable->setColumnCount(2);
    interfaceTable->setHorizontalHeaderLabels(QStringList() << "备注" << "接口");
    interfaceTable->verticalHeader()->setVisible(false);
    interfaceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    interfaceTable->setColumnWidth(0, 90);
    interfaceTable->setColumnWidth(1, 140);

    vLayoutMid->addWidget(interfaceTable);

    QHBoxLayout *hBtnMid = new QHBoxLayout();
    interfaceAddBtn = new QPushButton("添加新行", tab2);
    interfaceDelBtn = new QPushButton("删除选中", tab2);
    // 「获取模型列表」单独占一行：左栏最宽 180，三个按钮挤一行连字都显示不全
    modelListFetchBtn = new QPushButton("获取模型列表", tab2);
    modelListFetchBtn->setToolTip(
        "先在中间的「接口列表」里选中一个接口，再点这里。\n"
        "会用它第一个非空 key 去请求 <接口地址去掉 chat/completions> + /models，\n"
        "选中某个模型后会加进左侧模型列表，并自动勾选这个接口。");


    hBtnMid->addWidget(interfaceAddBtn);
    hBtnMid->addWidget(interfaceDelBtn);
    hBtnMid->addWidget(modelListFetchBtn);
    vLayoutMid->addLayout(hBtnMid);

    // ----- 右侧：Key 列表 -----
    QVBoxLayout *vLayoutRight = new QVBoxLayout();
    vLayoutRight->setSpacing(2);

    keyTable = new QTableWidget(tab2);
    keyTable->setColumnCount(3);
    keyTable->setHorizontalHeaderLabels(QStringList() << "key" << "使用次数" << "最后错误");
    keyTable->verticalHeader()->setVisible(false);
    keyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyTable->horizontalHeader()->setStretchLastSection(true);
    keyTable->setColumnWidth(0, 150);
    keyTable->setColumnWidth(1, 100);
    keyTable->setColumnWidth(2, 300);
    vLayoutRight->addWidget(keyTable);

    QHBoxLayout *hBtnRight = new QHBoxLayout();
    keyAddBtn = new QPushButton("添加新行", tab2);
    keyDelBtn = new QPushButton("删除选中", tab2);
    hBtnRight->addWidget(keyAddBtn);
    hBtnRight->addWidget(keyDelBtn);
    vLayoutRight->addLayout(hBtnRight);

    // 将三列按比例加入布局 (为了确保左边不拉伸，也可以在这里写死)
    hLayoutTab2->addLayout(vLayoutLeft, 0); // 权重 0，不拉伸
    hLayoutTab2->addLayout(vLayoutMid, 1);  // 权重 1
    hLayoutTab2->addLayout(vLayoutRight, 1); // 权重 1

    // 设置表格选择模式
    modelListTable->setSelectionMode(QAbstractItemView::SingleSelection);
    interfaceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    keyTable->setSelectionMode(QAbstractItemView::SingleSelection);

    // 中间表格第一列设置复选框
    interfaceTable->setColumnCount(2);
    interfaceTable->setHorizontalHeaderLabels(QStringList() << "备注" << "接口");

    interfaceTable->setColumnWidth(0, 100);
    interfaceTable->setColumnWidth(0, 140);


    // 信号连接
    connect(modelListTable, &QTableWidget::currentCellChanged,
            this, &AiWidget::onModelCurrentCellChanged);

    connect(modelListTable, &QTableWidget::cellChanged,
            this, &AiWidget::onmodelListTableCellChanged);

    connect(interfaceTable, &QTableWidget::currentCellChanged,
            this, &AiWidget::onInterfaceCurrentCellChanged);
    connect(interfaceTable, &QTableWidget::itemChanged,
            this, &AiWidget::onInterfaceItemChanged);

    connect(modelListAddBtn, &QPushButton::clicked, this, &AiWidget::onModelAdd);
    connect(modelListDelBtn, &QPushButton::clicked, this, &AiWidget::onModelDelete);
    connect(modelListFetchBtn, &QPushButton::clicked, this, &AiWidget::onModelFetchFromInterface);
    connect(interfaceAddBtn, &QPushButton::clicked, this, &AiWidget::onInterfaceAdd);
    connect(interfaceDelBtn, &QPushButton::clicked, this, &AiWidget::onInterfaceDelete);
    connect(keyAddBtn, &QPushButton::clicked, this, &AiWidget::onKeyAdd);
    connect(keyDelBtn, &QPushButton::clicked, this, &AiWidget::onKeyDelete);

    connect(interfaceTable, &QTableWidget::cellChanged,
            this, &AiWidget::onInterfaceTableCellChanged);

    connect(keyTable, &QTableWidget::cellChanged,
            this, &AiWidget::onKeyTableCellChanged);








    // ==================== 工具/函数配置 (tab 3) ====================
    QWidget *tab3 = new QWidget(this);
    tabWidget->addTab(tab3, "工具");

    QHBoxLayout *hLayoutTab3 = new QHBoxLayout(tab3);
    hLayoutTab3->setSpacing(2);
    hLayoutTab3->setContentsMargins(2, 2, 2, 2);

    // ----- 左侧：函数备注列表 -----
    QVBoxLayout *vLayoutFuncList = new QVBoxLayout();
    vLayoutFuncList->setSpacing(2);

    funcListTable = new QTableWidget(tab3);
    funcListTable->setColumnCount(1);
    funcListTable->setHorizontalHeaderLabels(QStringList() << "备注");
    funcListTable->verticalHeader()->setVisible(false);
    funcListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    funcListTable->setMaximumWidth(210);
    funcListTable->setColumnWidth(0, 200);
    vLayoutFuncList->addWidget(funcListTable);

    QHBoxLayout *hBtnFuncList = new QHBoxLayout();
    funcListDelBtn = new QPushButton("删除选中", tab3);
    funcListAddBtn = new QPushButton("添加行", tab3);
    hBtnFuncList->addWidget(funcListAddBtn);
    hBtnFuncList->addWidget(funcListDelBtn);
    funcListDelBtn->setMaximumWidth(100);
    funcListAddBtn->setMaximumWidth(100);
    vLayoutFuncList->addLayout(hBtnFuncList);

    // 设置左边不拉伸
    hLayoutTab3->addLayout(vLayoutFuncList, 0);

    // ----- 右侧：代码及参数配置 -----
    QVBoxLayout *vLayoutCode = new QVBoxLayout();
    vLayoutCode->setSpacing(2);

    // 上半部：Python 代码编写框
    funcCodeEdit = new QTextEdit(tab3);
    funcCodeEdit->setPlaceholderText("在此处编写 Python 代码...");
    vLayoutCode->addWidget(funcCodeEdit);

    // 下半部：函数名和参数配置（采用 QGridLayout）
    QGridLayout *gridParams = new QGridLayout();
    gridParams->setSpacing(2);

    // 第一行：函数名、保存、中断
    QLabel *lblFuncName = new QLabel("函数名:", tab3);
    lblFuncName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    funcNameEdit = new QLineEdit(tab3);
    funcNameEdit->setPlaceholderText("只能英文数字");
    funcSaveBtn = new QPushButton("保存", tab3);
    funcInterruptCheck = new QCheckBox("触发后中断", tab3);

    gridParams->addWidget(lblFuncName, 0, 0);
    gridParams->addWidget(funcNameEdit, 0, 1);
    gridParams->addWidget(funcSaveBtn, 0, 2);
    gridParams->addWidget(funcInterruptCheck, 0, 3, 1, 2); // 跨2列

    // 参数 1 ~ 8 (每行两个参数)
    QLabel *lblParam1 = new QLabel("参数1:", tab3); lblParam1->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param1Edit = new QLineEdit(tab3);
    QLabel *lblParam2 = new QLabel("参数2:", tab3); lblParam2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param2Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam1, 1, 0); gridParams->addWidget(param1Edit, 1, 1);
    gridParams->addWidget(lblParam2, 1, 2); gridParams->addWidget(param2Edit, 1, 3);

    QLabel *lblParam3 = new QLabel("参数3:", tab3); lblParam3->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param3Edit = new QLineEdit(tab3);
    QLabel *lblParam4 = new QLabel("参数4:", tab3); lblParam4->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param4Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam3, 2, 0); gridParams->addWidget(param3Edit, 2, 1);
    gridParams->addWidget(lblParam4, 2, 2); gridParams->addWidget(param4Edit, 2, 3);

    QLabel *lblParam5 = new QLabel("参数5:", tab3); lblParam5->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param5Edit = new QLineEdit(tab3);
    QLabel *lblParam6 = new QLabel("参数6:", tab3); lblParam6->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param6Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam5, 3, 0); gridParams->addWidget(param5Edit, 3, 1);
    gridParams->addWidget(lblParam6, 3, 2); gridParams->addWidget(param6Edit, 3, 3);

    QLabel *lblParam7 = new QLabel("参数7:", tab3); lblParam7->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param7Edit = new QLineEdit(tab3);
    QLabel *lblParam8 = new QLabel("参数8:", tab3); lblParam8->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param8Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam7, 4, 0); gridParams->addWidget(param7Edit, 4, 1);
    gridParams->addWidget(lblParam8, 4, 2); gridParams->addWidget(param8Edit, 4, 3);

    vLayoutCode->addLayout(gridParams);
    hLayoutTab3->addLayout(vLayoutCode, 1); // 右侧拉伸填满

    // 设置表格第一列可勾选
    funcListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    funcListTable->setSelectionMode(QAbstractItemView::SingleSelection);

    // 连接信号槽
    connect(funcListTable, &QTableWidget::currentCellChanged,
            this, &AiWidget::onFuncListCurrentCellChanged);
    connect(funcListAddBtn, &QPushButton::clicked,
            this, &AiWidget::onFuncListAdd);
    connect(funcListDelBtn, &QPushButton::clicked,
            this, &AiWidget::onFuncListDelete);
    connect(funcSaveBtn, &QPushButton::clicked,
            this, &AiWidget::onFuncSave);

    connect(funcListTable, &QTableWidget::itemChanged,this, &AiWidget::onFuncListItemChanged);


    // ==================== 插件工具 (tab 4) ====================
    // 左边列插件，点一个 → 右边列出它注册的指令；勾上 = 注册成 AI 工具。
    // 插件的指令本身**没有说明文字**，所以「说明 / 参数说明」留空时按指令名 + 匹配方式自动生成，
    // 用户也可以自己填（参数说明一行一个，前面加 * 表示必填）。
    QWidget *tab4 = new QWidget(this);
    tabWidget->addTab(tab4, "插件工具");

    QHBoxLayout *hLayoutTab4 = new QHBoxLayout(tab4);
    hLayoutTab4->setSpacing(2);
    hLayoutTab4->setContentsMargins(2, 2, 2, 2);

    // ----- 左侧：插件列表 -----
    QVBoxLayout *vLayoutPtLeft = new QVBoxLayout();
    vLayoutPtLeft->setSpacing(2);

    pluginToolPluginList = new QListWidget(tab4);
    pluginToolPluginList->setMaximumWidth(180);
    vLayoutPtLeft->addWidget(pluginToolPluginList);

    pluginToolRefreshBtn = new QPushButton("刷新插件", tab4);
    pluginToolRefreshBtn->setToolTip("重新扫描已加载的插件（装完新插件后点一下）");
    vLayoutPtLeft->addWidget(pluginToolRefreshBtn);

    hLayoutTab4->addLayout(vLayoutPtLeft, 0);

    // ----- 右侧：指令表 -----
    QVBoxLayout *vLayoutPtRight = new QVBoxLayout();
    vLayoutPtRight->setSpacing(2);

    // 提示里点名当前机器人：勾选是**按机器人**存的，换机器人这页的勾会跟着变
    pluginToolHintLabel = new QLabel(pluginToolHintText(), tab4);
    pluginToolHintLabel->setWordWrap(true);
    vLayoutPtRight->addWidget(pluginToolHintLabel);

    pluginToolTable = new QTableWidget(tab4);
    pluginToolTable->setColumnCount(3);
    pluginToolTable->setHorizontalHeaderLabels(
        QStringList() << "启用 / 指令" << "工具说明（给 AI）" << "参数（一行一个）");
    pluginToolTable->verticalHeader()->setVisible(false);
    pluginToolTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pluginToolTable->setColumnWidth(0, 190);
    pluginToolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    pluginToolTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pluginToolTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    // 「工具说明」「参数」两列要能写多行：换多行编辑器 + 行高跟着内容长，否则多行内容看不全
    auto *ptMultiEdit = new MultiLineDelegate(pluginToolTable);
    pluginToolTable->setItemDelegateForColumn(1, ptMultiEdit);
    pluginToolTable->setItemDelegateForColumn(2, ptMultiEdit);
    pluginToolTable->setWordWrap(true);
    pluginToolTable->verticalHeader()->setMinimumSectionSize(30);
    pluginToolTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // 这三列的含义挂表头，鼠标停一下就能看（正文里不再写原理）
    pluginToolTable->horizontalHeaderItem(0)->setToolTip(
        "第一列打勾 = 把这条指令注册成 AI 工具（只对当前机器人生效，换机器人各管各的）。\n"
        "括号里的「原:xxx」表示插件改名后，插件代码里注册的原始指令名。");
    pluginToolTable->horizontalHeaderItem(1)->setToolTip(
        "这个工具是干什么用的，原样作为工具描述发给 AI。\n"
        "留空 = 自动生成一句（插件名 + 指令 + 匹配方式）。\n"
        "例：查询某城市的天气，参数是城市名。");
    pluginToolTable->horizontalHeaderItem(2)->setToolTip(
        "一行一个参数，顺序就是 p1、p2…；行首加 * 表示必填。\n"
        "双击编辑，回车换行，点别处保存。\n"
        "例：\n* 城市名，如 北京\n  天数，1-7，默认 1\n"
        "全空 = 给 AI 一个可留空的 p1（想传参数就往后拼，插件按空格切正文）。");
    vLayoutPtRight->addWidget(pluginToolTable);

    QHBoxLayout *hBtnPt = new QHBoxLayout();
    hBtnPt->addWidget(new QLabel("没返回值时最多等", tab4));
    pluginToolWaitSpin = new QSpinBox(tab4);
    pluginToolWaitSpin->setRange(1, 3);          // 这段是阻塞等待，硬上限 3 秒
    pluginToolWaitSpin->setSuffix(" 秒");
    pluginToolWaitSpin->setValue(3);
    pluginToolWaitSpin->setMaximumWidth(90);
    hBtnPt->addWidget(pluginToolWaitSpin);
    hBtnPt->addWidget(new QLabel("（超时就告诉 AI「插件没有返回内容」）", tab4));
    hBtnPt->addStretch();
    vLayoutPtRight->addLayout(hBtnPt);

    hLayoutTab4->addLayout(vLayoutPtRight, 1);

    connect(pluginToolPluginList, &QListWidget::currentRowChanged,
            this, &AiWidget::onPluginToolPluginChanged);
    connect(pluginToolRefreshBtn, &QPushButton::clicked,
            this, &AiWidget::refreshPluginToolPlugins);
    connect(pluginToolTable, &QTableWidget::itemChanged,
            this, &AiWidget::onPluginToolItemChanged);
    connect(pluginToolWaitSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AiWidget::onPluginToolWaitChanged);
    // 切到这个页才扫插件表：插件是启动后才陆续加载的，构造时扫不一定扫得到
    connect(tabWidget, &QTabWidget::currentChanged, this, [this, tab4](int i) {
        if (i >= 0 && tabWidget->widget(i) == tab4) refreshPluginToolPlugins();
    });


    aisxw *ai_sxw = new aisxw(this);   // 创建 plts 对象
    ai_sxw->show();
    tabWidget->addTab(ai_sxw, "上下文");

    ai_bqbgl = new bqbgl(this);   // 创建 plts 对象
    ai_bqbgl->show();



    tabWidget->addTab(ai_bqbgl, "表情包管理");
    ai_fujia = new Aifujia(this);
    ai_fujia->show();
    tabWidget->addTab(ai_fujia, "附加模型");

}
// ====== 数据读写 ======

void AiWidget::loadFromFile()
{
    QFile file("data/roles.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();

    // 读取全局设定
    m_globalSettings.clear();
    QJsonArray settingsArr = root["global_settings"].toArray();
    for (const auto &v : std::as_const(settingsArr)) {
        QJsonObject obj = v.toObject();
        RoleSetting s;
        s.name = obj["name"].toString();
        s.content = obj["content"].toString();
        m_globalSettings.append(s);
    }

    // 读取工具配置
    QJsonObject toolObj = root["tool_config"].toObject();
    m_toolConfig.enableAutoReply = toolObj["enable_auto_reply"].toBool();
    m_toolConfig.enableScheduledTask = toolObj["enable_scheduled_task"].toBool();
    m_toolConfig.enableCustomLog = toolObj["enable_custom_log"].toBool();

    QJsonArray tableArr = toolObj["table_data"].toArray();
    m_toolConfig.tableData.clear();
    for (const auto &v : std::as_const(tableArr)) {
        QJsonArray pair = v.toArray();
        if (pair.size() == 2) {
            m_toolConfig.tableData.append(qMakePair(pair[0].toString(), pair[1].toString()));
        }
    }
}
void AiWidget::saveToFile1() const
{
    QJsonObject root;

    // 写入全局设定
    QJsonArray settingsArr;
    for (const auto &s : m_globalSettings) {
        QJsonObject obj;
        obj["name"] = s.name;
        obj["content"] = s.content;
        settingsArr.append(obj);
    }
    root["global_settings"] = settingsArr;

    QJsonObject toolObj;
    toolObj["enable_auto_reply"] = m_toolConfig.enableAutoReply;
    toolObj["enable_scheduled_task"] = m_toolConfig.enableScheduledTask;
    toolObj["enable_custom_log"] = m_toolConfig.enableCustomLog;

    QJsonArray tableArr;
    for (const auto &pair : m_toolConfig.tableData) {
        QJsonArray item;
        item.append(pair.first);
        item.append(pair.second);
        tableArr.append(item);
    }
    toolObj["table_data"] = tableArr;
    root["tool_config"] = toolObj;

    QJsonDocument doc(root);
    QFile file("data/roles.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}



void AiWidget::addtoui(const std::shared_ptr<AccountInfo> acc)
{
    editRobotName->setText(acc->Ai_nickname);
    if(acc->model.isEmpty())
        comboModel->setCurrentIndex(-1);
    else
        comboModel->setCurrentText(acc->model);
    if(acc->Embed_model.isEmpty())
        combo_xiangliang->setCurrentIndex(-1);
    else
        combo_xiangliang->setCurrentText(acc->Embed_model);
    comboPplx->setCurrentIndex(acc->pplx);

    int idx = comboSetting->findText(acc->setting);
    comboSetting->setCurrentIndex(idx >= 0 ? idx : -1);
    editContext->setText(QString::number(acc->context_len));
    editNoReplySeconds->setText(QString::number(acc->nSecondsNoReply));
    editNoReplyMinutes->setText(QString::number(acc->nMinutesNoReply));
    editDelayReply->setText(QString::number(acc->delayReplySeconds));
    chkGroupChat->setChecked(acc->enableGroupChat);
    chkGroupPersonal->setChecked(acc->enableGroupPersonal);
    chkPrivateChat->setChecked(acc->enablePrivateChat);
    juece->setChecked(acc->juece);
    chkChannel->setChecked(acc->enableChannel);
    chkAtTrigger->setChecked(acc->atTrigger);
    chkChannelPersonal->setChecked(acc->enableChannelPersonal);
    chkImageRec->setChecked(acc->enableImageRec);
    向量数据库->setChecked(acc->xiangliang);

    chkniren->setChecked(acc->niren);
    set_zl->setText(acc->bai_sr);
    set_sc->setText(acc->bai_sc);
    feibaimd->setChecked(acc->e_bai);
    set_qy->setText(acc->bai_qy);
    set_固定条数->setText(QString::number(acc->固定条数));
    set_sjhf->setText(QString::number(acc->触发概率));
    set_递增概率->setText(QString::number(acc->递增概率));
}
void AiWidget::刷新模型()
{
    ai_fujia->initmode(modelList);
    emit modelListUpdated(); // 发出通知！告诉所有新开窗口刷新

    QString currentText = comboModel->currentText();  // 假设 comboModel 是 QComboBox*
    comboModel->clear();
    combo_xiangliang->clear();
    m_nickReviewWidget->m_aimode->clear();
    for (const auto &m : std::as_const(modelList))
    {
        combo_xiangliang->addItem(m.name);
        comboModel->addItem(m.name);
        m_nickReviewWidget->m_aimode->addItem(m.name);
    }
    int index = comboModel->findText(currentText);
    if (index != -1)
        comboModel->setCurrentIndex(index);

    index = combo_xiangliang->findText(currentText);
    if (index != -1)
        combo_xiangliang->setCurrentIndex(index);

}

void AiWidget::refreshSettingList()
{
    settingListWidget->clear();
    for (const auto &s : std::as_const(m_globalSettings)) {
        QListWidgetItem *item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, s.name);
        settingListWidget->addItem(item);
    }
    if (settingListWidget->count() > 0)
        settingListWidget->setCurrentRow(0);
    else
        settingTextEdit->clear();
}

void AiWidget::refreshSettingCombo()
{
    comboSetting->clear();
    for (const auto &s : std::as_const(m_globalSettings)) {
        comboSetting->addItem(s.name);
    }
}

//列表被单击
void AiWidget::list_c()
{
    for (const auto &acc : std::as_const(m_accounts))
    {
        if(acc->appid_int != g_appid) continue;


        addtoui(acc);
        不加载= 1;
        for (int row = 0; row < funcListTable->rowCount(); ++row) {
            QTableWidgetItem *item = funcListTable->item(row, 0);
            if (item) {
                item->setCheckState(Qt::Unchecked);
            }
        }
        QHash<QString, int> nameToRow;
        for (int row = 0; row < functionList.size(); ++row) {
            nameToRow[functionList[row].funcName] = row;
        }
        for (int i = 0; i < acc->tools.size(); ++i) {
            const QString &toolName = acc->tools[i];  // 或者用 QString toolName = acc->tools[i];
            auto it = nameToRow.find(toolName);
            if (it != nameToRow.end()) {
                int row = *it;
                QTableWidgetItem *item = funcListTable->item(row, 0);
                if (item) {
                    item->setCheckState(Qt::Checked);
                }
            }
        }
        ai_fujia->initdata(acc.get());
        refreshPluginToolChecks();   // 插件工具也是按机器人的，勾选跟着换


        不加载= 0;
        return;
    }
    QMessageBox::warning(this,"打开失败","当前绑定的机器人 不在列表 请刷新列表重写选择");
}

void AiWidget::on_btnSaveRobot_clicked()
{
    for (const auto &acc : std::as_const(m_accounts))
    {
        if(acc->appid_int != g_appid) continue;
        acc->Ai_nickname = editRobotName->text().trimmed();
        acc->model = comboModel->currentText();
        acc->Embed_model = combo_xiangliang->currentText();
        acc->pplx = comboPplx->currentIndex();
        acc->setting = comboSetting->currentText();
        acc->context_len = editContext->text().toInt();
        acc->nSecondsNoReply = editNoReplySeconds->text().toInt();
        acc->nMinutesNoReply = editNoReplyMinutes->text().toInt();
        acc->delayReplySeconds = editDelayReply->text().toInt();
        acc->enableGroupChat = chkGroupChat->isChecked();
        acc->enableGroupPersonal = chkGroupPersonal->isChecked();
        acc->enablePrivateChat = chkPrivateChat->isChecked();
        acc->juece = juece->isChecked();
        acc->enableChannel = chkChannel->isChecked();
        acc->atTrigger = chkAtTrigger->isChecked();
        acc->enableChannelPersonal = chkChannelPersonal->isChecked();
        acc->enableImageRec = chkImageRec->isChecked();
        acc->xiangliang = 向量数据库->isChecked();
        acc->niren = chkniren->isChecked();
        acc->bai_sr = set_zl->text();
        acc->bai_sc = set_sc->text();
        acc->e_bai = feibaimd->isChecked();
        acc->bai_qy = set_qy->text();


        acc->固定条数 = set_固定条数->text().toInt();
        acc->触发概率 = set_sjhf->text().toInt();
        acc->递增概率 = set_递增概率->text().toInt();

        accountPage->saveAccounts(acc.get());
        return;
    }
    QMessageBox::warning(this,"保存失败","未找对对应 appid 机器人："+QString::number(g_appid));
}

// ====== 全局设定相关槽 ======

void AiWidget::on_settingListWidget_currentRowChanged(int currentRow)
{
    if(不加载) return;
    if (currentRow < 0 || currentRow >= m_globalSettings.size()) {
        settingTextEdit->clear();
        editSettingName->clear();
        return;
    }
    const auto &s = m_globalSettings[currentRow];
    editSettingName->setText(s.name);
    settingTextEdit->setPlainText(s.content);
}

void AiWidget::on_btnAddSetting_clicked()
{
    QString name = editSettingName->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "警告", "设定名不能为空！");
        return;
    }
    // 检查同名
    for (auto &s : m_globalSettings) {
        if (s.name == name) {
            s.content = settingTextEdit->toPlainText();
            saveToFile1();
            return;
        }
    }

    RoleSetting newSetting;
    newSetting.name = name;
    newSetting.content = settingTextEdit->toPlainText();
    m_globalSettings.append(newSetting);

    refreshSettingList();
    refreshSettingCombo();

    // 选中新增的设定
    for (int i = 0; i < settingListWidget->count(); ++i) {
        if (settingListWidget->item(i)->data(Qt::UserRole).toString() == name) {
            settingListWidget->setCurrentRow(i);
            break;
        }
    }
    saveToFile1();
}

void AiWidget::on_btnDeleteSetting_clicked()
{
    int row = settingListWidget->currentRow();
    if (row < 0 || row >= m_globalSettings.size()) {
        QMessageBox::information(this, "提示", "请先选择一个设定！");
        return;
    }
    QString name = m_globalSettings[row].name;

    // 检查是否有机器人正在使用该设定
    for (const auto &acc : std::as_const(m_accounts)) {
        if (acc->setting == name) {
            QMessageBox::warning(this, "警告",
                                 QString("设定「%1」正被机器人「%2」使用，无法删除！").arg(name, acc->nickname));
            return;
        }
    }

    m_globalSettings.removeAt(row);
    refreshSettingList();
    refreshSettingCombo();
    saveToFile1();
}



void AiWidget::onFuncListAdd() {
    FunctionData newData;
    newData.remark = QString("新函数 %1").arg(functionList.size() + 1);

    newData.params.clear();
    for (int i = 0; i < 8; ++i) {
        newData.params.append("");
    }
    functionList.append(newData);

    int row = functionList.size() - 1;
    funcListTable->insertRow(row);

    // 设置第一列的 Item（带复选框和备注）
    QTableWidgetItem *item = new QTableWidgetItem(newData.remark);

    funcListTable->setItem(row, 0, item);

    // 选中新行
    funcListTable->selectRow(row);
    // currentCellChanged 会自动触发加载数据
    saveToFile();  // 自动保存到文件
}
void AiWidget::onFuncListDelete() {
    int row = funcListTable->currentRow();
    if (row < 0 || row >= functionList.size()) {
        QMessageBox::warning(this, "提示", "请先选中要删除的行");
        return;
    }

    // 从列表中移除
    functionList.removeAt(row);
    funcListTable->removeRow(row);

    // 如果还有行，选中第一行；否则清空右侧面板
    if (!functionList.isEmpty()) {
        funcListTable->selectRow(0);
    } else {
        clearRightPanel();  // 清空所有编辑框
        currentRow = -1;
    }
    saveToFile();
}
void AiWidget::saveCurrentRowData() {
    int row = funcListTable->currentRow();
    if (row < 0 || row >= functionList.size()) return;

    // 从右侧面板读取数据
    FunctionData &data = functionList[row];
    data.funcName = funcNameEdit->text();
    data.code = funcCodeEdit->toPlainText();
    data.params.clear();
    data.params << param1Edit->text() << param2Edit->text()
                << param3Edit->text() << param4Edit->text()
                << param5Edit->text() << param6Edit->text()
                << param7Edit->text() << param8Edit->text();
    data.interrupt = funcInterruptCheck->isChecked();


}
void AiWidget::onFuncListCurrentCellChanged(int currentRow, int currentCol,
                                             int previousRow, int previousCol) {


    if (currentRow < 0 || currentRow >= functionList.size()) {
        clearRightPanel();
        this->currentRow = -1;
        return;
    }

    const FunctionData &data = functionList[currentRow];
    funcNameEdit->setText(data.funcName);
    funcCodeEdit->setPlainText(data.code);
    param1Edit->setText(data.params.value(0));
    param2Edit->setText(data.params.value(1));
    param3Edit->setText(data.params.value(2));
    param4Edit->setText(data.params.value(3));
    param5Edit->setText(data.params.value(4));
    param6Edit->setText(data.params.value(5));
    param7Edit->setText(data.params.value(6));
    param8Edit->setText(data.params.value(7));
    funcInterruptCheck->setChecked(data.interrupt);

    this->currentRow = currentRow;
}

void AiWidget::clearRightPanel() {
    funcNameEdit->clear();
    funcCodeEdit->clear();
    param1Edit->clear();
    param2Edit->clear();
    param3Edit->clear();
    param4Edit->clear();
    param5Edit->clear();
    param6Edit->clear();
    param7Edit->clear();
    param8Edit->clear();
    funcInterruptCheck->setChecked(false);
}
void AiWidget::onFuncSave() {
    int row = funcListTable->currentRow();
    if (row < 0 || row >= functionList.size()) {
        QMessageBox::warning(this, "提示", "请先选中要保存的行");
        return;
    }
    saveCurrentRowData();   // 保存到内存
    saveToFile();           // 写入文件

}

void AiWidget::saveToFile() {
    QJsonArray jsonArray;
    for (const FunctionData &data : std::as_const(functionList)) {
        QJsonObject obj;

        obj["remark"] = data.remark;
        obj["funcName"] = data.funcName;
        obj["code"] = data.code;
        obj["params"] = QJsonArray::fromStringList(data.params);
        obj["interrupt"] = data.interrupt;
        jsonArray.append(obj);
    }

    QJsonDocument doc(jsonArray);
    QFile file(configFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法保存配置文件");
        return;
    }
    file.write(doc.toJson());
    file.close();
}

void AiWidget::loadFromFile2() {
    QFile file(configFilePath);
    if (!file.exists()) return;  // 首次运行没有文件

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {

        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return;

    QJsonArray jsonArray = doc.array();
    functionList.clear();
    funcListTable->setRowCount(0);
    不加载= true;
    for (const QJsonValue &val : std::as_const(jsonArray)) {
        QJsonObject obj = val.toObject();
        FunctionData d;

        d.remark = obj["remark"].toString();
        d.funcName = obj["funcName"].toString();
        d.code = obj["code"].toString();
        QJsonArray paramsArray = obj["params"].toArray();
        for (int i = 0; i < paramsArray.size() && i < 8; ++i) {
            d.params.append(paramsArray[i].toString());
        }
        while (d.params.size() < 8) d.params.append("");  // 补齐8个
        d.interrupt = obj["interrupt"].toBool(false);

        functionList.append(d);

        // 在表格中添加行
        int row = functionList.size() - 1;
        funcListTable->insertRow(row);
        QTableWidgetItem *item = new QTableWidgetItem(d.remark);
        funcListTable->setItem(row, 0, item);
    }

    // 若加载后有数据，选中第一行
    if (!functionList.isEmpty()) {
        funcListTable->selectRow(0);
        onFuncListCurrentCellChanged(0, 0, -1, -1);
    } else {
        clearRightPanel();
        currentRow = -1;
    }
    不加载= 0;
}


void AiWidget::onFuncListItemChanged(QTableWidgetItem *item) {
    if(不加载) return;
    if (!item) return;
    int row = item->row();
    if (row < 0 || row >= functionList.size()) return;
    QString remark=item->text();
    if(remark != functionList[row].remark)
    {
        functionList[row].remark = remark;
        saveToFile();
        return;
    }
    for (auto &acc : m_accounts)
    {
        if(acc->appid_int!= g_appid) continue;
        acc->tools.clear();
        bool ok=false;
        for (int i=0; i< functionList.size();++i) {
            QTableWidgetItem *item = funcListTable->item(i, 0);
            bool en = item->checkState();
            if(functionList[i].funcName.isEmpty())
            {
                if(en) item->setCheckState(Qt::Unchecked);
                continue;
            }
            if(en)
            {
                ok=true;
                acc->tools.append(functionList[i].funcName);
            }
        }
        if(ok) accountPage->saveAccounts(acc.get());
        break;
    }

}

void AiWidget::onmodelListTableCellChanged(int row, int column) {
    if(不加载) return;
    if (row < 0 || row >= modelList.size()) return;  // 全局接口列表
    QTableWidgetItem *item = modelListTable->item(row, column);
    if (!item) return;
    QString newText = item->text();
    auto &iface = modelList[row];
    iface.name = newText;
    刷新模型();

    saveToFile2();
}

//==============================================

void AiWidget::onModelAdd() {
    ModelData newModel;
    newModel.name = QString("新模型 %1").arg(modelList.size() + 1);
    // 默认不启用任何接口（空列表）
    modelList.append(newModel);
    int row = modelList.size() - 1;
    modelListTable->insertRow(row);
    modelListTable->setItem(row, 0, new QTableWidgetItem(newModel.name));
    modelListTable->selectRow(row);
    刷新模型();

    saveToFile2();
}

void AiWidget::onModelDelete() {
    int row = modelListTable->currentRow();
    if (row < 0 || row >= modelList.size()) {
        QMessageBox::warning(this, "提示", "请先选中要删除的模型");
        return;
    }
    modelList.removeAt(row);
    modelListTable->removeRow(row);

    if (!modelList.isEmpty()) {
        modelListTable->selectRow(row);
    } else {
        interfaceTable->setRowCount(0);
        keyTable->setRowCount(0);
        currentModelRow = -1;
        currentInterfaceRow = -1;
    }
    刷新模型();

    saveToFile2();
}
//======================== 从接口获取模型列表 ========================

// 由「对话接口」地址推「模型列表」地址，返回按优先级排好的候选（逐个试）。
//   https://allgpt.xianyuw.cn/v1/chat/completions → https://allgpt.xianyuw.cn/v1/models
// 有的站对话地址不带版本段（https://api.deepseek.com/chat/completions），
// 这种就把 /models 和 /v1/models 都试一下。
static QStringList modelsUrlsFor(const QString &chatUrl)
{
    QStringList out;
    const QUrl u(chatUrl.trimmed());
    if (!u.isValid() || u.host().isEmpty())
        return out;

    // 去掉结尾的 chat/completions（末尾斜杠有或没有都认）
    QString base = u.path();
    static const QRegularExpression tail(QStringLiteral("(?:^|/)chat/completions?/?$"),
                                         QRegularExpression::CaseInsensitiveOption);
    base.remove(tail);
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);

    const auto makeUrl = [&u](const QString &prefix) {
        QUrl m(u);
        m.setPath(prefix + QStringLiteral("/models"));
        m.setQuery(QString());
        m.setFragment(QString());
        return m.toString();
    };

    out << makeUrl(base);

    // base 里没有版本段（/v1、/v4 这种）才补一个 /v1/models
    static const QRegularExpression ver(QStringLiteral("/v\\d+(?:/|$)"),
                                        QRegularExpression::CaseInsensitiveOption);
    if (!ver.match(base).hasMatch())
        out << makeUrl(base + QStringLiteral("/v1"));

    out.removeDuplicates();
    return out;
}

// 从 /models 的响应里抠出模型 id，兼容三种写法：
//   {"data":[{"id":"gpt-4o"},...]} / {"data":["gpt-4o",...]} / ["gpt-4o",...]
static bool parseModelIds(const QString &body, QStringList &ids, QString &err)
{
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        err = QStringLiteral("返回不是合法 JSON：") + body.trimmed().left(200);
        return false;
    }

    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject o = doc.object();
        if (o.value(QStringLiteral("data")).isArray()) {
            arr = o.value(QStringLiteral("data")).toArray();
        } else if (o.value(QStringLiteral("models")).isArray()) {
            arr = o.value(QStringLiteral("models")).toArray();
        } else if (o.contains(QStringLiteral("error"))) {
            const QJsonValue e = o.value(QStringLiteral("error"));
            err = QStringLiteral("接口返回错误：")
                  + (e.isString() ? e.toString()
                                  : QString::fromUtf8(QJsonDocument(e.toObject())
                                                          .toJson(QJsonDocument::Compact)));
            err = err.left(240);
            return false;
        } else {
            err = QStringLiteral("返回里没有 data 数组：") + body.trimmed().left(200);
            return false;
        }
    } else {
        err = QStringLiteral("返回既不是对象也不是数组：") + body.trimmed().left(200);
        return false;
    }

    for (const QJsonValue &v : std::as_const(arr)) {
        if (v.isString()) {
            ids << v.toString();
        } else if (v.isObject()) {
            const QJsonObject o = v.toObject();
            QString id = o.value(QStringLiteral("id")).toString();
            if (id.isEmpty()) id = o.value(QStringLiteral("model")).toString();
            if (!id.isEmpty()) ids << id;
        }
    }
    ids.removeDuplicates();
    if (ids.isEmpty()) {
        err = QStringLiteral("接口没返回任何模型");
        return false;
    }
    return true;
}

// 弹一个带过滤框的列表让用户挑一个模型 id；取消返回空串。
// chatUrl / key 是来源接口的对话地址与 key —— 给弹窗里的「测试」按钮用。
static QString pickModelFromList(QWidget *parent, const QString &ifaceName,
                                 const QStringList &ids,
                                 const QString &chatUrl, const QString &key)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("选择模型"));
    dlg.resize(460, 620);

    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    lay->setSpacing(4);

    QLabel *tip = new QLabel(QStringLiteral("接口：%1     共 %2 个模型").arg(ifaceName).arg(ids.size()), &dlg);
    lay->addWidget(tip);

    PlaceholderLineEdit *filter = new PlaceholderLineEdit(&dlg);
    filter->setPlaceholderText(QStringLiteral("输入关键字过滤，双击直接选…"));
    lay->addWidget(filter);

    QListWidget *list = new QListWidget(&dlg);
    list->addItems(ids);
    lay->addWidget(list, 1);

    // 「测试」结果就地显示，不再弹二级对话框
    QLabel *result = new QLabel(&dlg);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result->setMinimumHeight(32);
    lay->addWidget(result);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("添加"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    QPushButton *testBtn = box->addButton(QStringLiteral("测试"), QDialogButtonBox::ActionRole);
    testBtn->setToolTip(QStringLiteral("拿选中的模型对「%1」发一次最小请求（max_tokens=16），\n"
                                       "看这个站到底能不能用它").arg(ifaceName));
    lay->addWidget(box);

    // 过滤后顺手选中第一个可见项，免得按回车选到一个已经被过滤掉的行
    QObject::connect(filter, &QLineEdit::textChanged, list, [list](const QString &t) {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *it = list->item(i);
            const bool hide = !t.isEmpty() && !it->text().contains(t, Qt::CaseInsensitive);
            it->setHidden(hide);
            if (!hide && !first) first = it;
        }
        if (first) list->setCurrentItem(first);
        else        list->setCurrentRow(-1);
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // 「测试」：把这个模型对来源接口发一次 chat/completions，max_tokens 给一点点少花钱
    // （捕获 &dlg 是安全的：这个连接挂 &dlg 上，弹窗一销毁就跟着没了）
    QObject::connect(testBtn, &QPushButton::clicked, &dlg,
                     [list, result, testBtn, chatUrl, key, &dlg]() {
        QListWidgetItem *cur = list->currentItem();
        if (!cur || cur->isHidden()) {
            result->setText(QStringLiteral("先在上面选一个模型再测"));
            return;
        }
        if (chatUrl.isEmpty()) {
            result->setText(QStringLiteral("这个接口没有对话地址，没法测"));
            return;
        }
        const QString model = cur->text();

        QJsonObject um;
        um["role"]    = QStringLiteral("user");
        um["content"] = QStringLiteral("hi");
        QJsonArray msgs;
        msgs.append(um);

        QJsonObject body;
        body["model"]      = model;
        body["messages"]   = msgs;
        body["max_tokens"] = 16;          // 只要它肯开口就行
        body["stream"]     = false;

        QHash<QString, QString> h;
        h["Authorization"] = "Bearer " + key;
        h["Content-Type"]  = "application/json";
        h["Accept"]        = "application/json";

        result->setText(QStringLiteral("正在测试 %1 …").arg(model));
        testBtn->setEnabled(false);

        // 弹窗可能被关掉、请求还在飞 → 全程 QPointer 挡着
        QPointer<QDialog>     dlgG(&dlg);
        QPointer<QLabel>      resG(result);
        QPointer<QPushButton> btnG(testBtn);

        NetManager::instance()->postAsync(
            chatUrl, QJsonDocument(body).toJson(QJsonDocument::Compact), h, 60000,
            [dlgG, resG, btnG, model](const QString &resp, QNetworkReply::NetworkError err) {
                // 回调在线程池线程 → 切回 GUI 线程再写控件
                if (!dlgG) return;
                QMetaObject::invokeMethod(dlgG.data(), [dlgG, resG, btnG, model, resp, err]() {
                    if (!dlgG || !resG) return;
                    if (btnG) btnG->setEnabled(true);

                    if (err != QNetworkReply::NoError) {
                        resG->setText(QStringLiteral("测试失败：%1\n%2")
                                          .arg(model, resp.trimmed().left(400)));
                        return;
                    }

                    const QJsonObject o = QJsonDocument::fromJson(resp.toUtf8()).object();
                    if (o.value(QStringLiteral("choices")).isArray()) {
                        QString say;
                        const QJsonArray chs = o.value(QStringLiteral("choices")).toArray();
                        if (!chs.isEmpty()) {
                            say = chs.at(0).toObject()
                                      .value(QStringLiteral("message")).toObject()
                                      .value(QStringLiteral("content")).toString().trimmed().left(60);
                        }
                        resG->setText(QStringLiteral("测试通过：%1 可用%2")
                                          .arg(model, say.isEmpty()
                                                          ? QString()
                                                          : QStringLiteral("（它回了：") + say
                                                                + QStringLiteral("）")));
                    } else if (o.contains(QStringLiteral("error"))) {
                        const QJsonValue e = o.value(QStringLiteral("error"));
                        resG->setText(QStringLiteral("测试失败：%1\n%2")
                                          .arg(model,
                                               (e.isString()
                                                    ? e.toString()
                                                    : QString::fromUtf8(QJsonDocument(e.toObject())
                                                                            .toJson(QJsonDocument::Compact)))
                                                   .left(400)));
                    } else {
                        resG->setText(QStringLiteral("没按标准格式回（可能能用）：%1\n%2")
                                          .arg(model, resp.trimmed().left(300)));
                    }
                }, Qt::QueuedConnection);
            },
            NetManager::CallbackOnPoolThread);
    });

    if (list->count() > 0) list->setCurrentRow(0);
    filter->setFocus();

    if (dlg.exec() != QDialog::Accepted) return QString();
    QListWidgetItem *cur = list->currentItem();
    return (cur && !cur->isHidden()) ? cur->text() : QString();
}

// 当前要用的接口名（备注优先，没有就用地址）
static QString ifaceDisplayName(const QList<InterfaceData> &ifaces, int idx)
{
    if (idx < 0 || idx >= ifaces.size()) return QString();
    const InterfaceData &f = ifaces.at(idx);
    return f.remark.trimmed().isEmpty() ? f.url : f.remark.trimmed();
}

void AiWidget::onModelFetchFromInterface()
{
    if (m_fetchModel.inFlight) {
        QMessageBox::information(this, "提示", "上一次获取还没回来，稍等一下…");
        return;
    }

    // 1) 用「接口列表」里当前选中的那一行（行号 == globalInterfaces 下标）
    const int ifIdx = interfaceTable->currentRow();
    if (globalInterfaces.isEmpty() || ifIdx < 0 || ifIdx >= globalInterfaces.size()) {
        QMessageBox::warning(this, "提示", "请先在中间的「接口列表」里选中一个接口");
        return;
    }
    const QString ifaceName = ifaceDisplayName(globalInterfaces, ifIdx);

    // 2) 取该接口第一个非空 key
    QString key;
    for (const KeyData &k : std::as_const(globalInterfaces.at(ifIdx).keys)) {
        if (!k.key.trimmed().isEmpty()) { key = k.key.trimmed(); break; }
    }
    if (key.isEmpty()) {
        QMessageBox::warning(this, "提示",
            QString("接口「%1」还没配 key，请先在右边给它加一个 key").arg(ifaceName));
        return;
    }

    // 3) 候选 models 地址
    const QStringList urls = modelsUrlsFor(globalInterfaces.at(ifIdx).url);
    if (urls.isEmpty()) {
        QMessageBox::warning(this, "提示",
            QString("接口「%1」的地址没法解析：\n%2").arg(ifaceName, globalInterfaces.at(ifIdx).url));
        return;
    }

    // 4) 记下状态后异步逐个试（别在 GUI 线程里同步等网络）
    m_fetchModel.inFlight = true;
    m_fetchModel.iface    = ifIdx;
    m_fetchModel.key      = key;
    m_fetchModel.urls     = urls;
    m_fetchModel.pos      = 0;
    m_fetchModel.lastErr.clear();
    fetchModelListStep();
}

void AiWidget::fetchModelListStep()
{
    if (!m_fetchModel.inFlight) return;

    // 候选地址都试完了 → 汇报最后一次的失败原因
    if (m_fetchModel.pos >= m_fetchModel.urls.size()) {
        m_fetchModel.inFlight = false;
        QMessageBox::warning(this, "获取模型列表失败",
            QString("接口「%1」没能取到模型列表。\n\n最后一次尝试：\n%2")
                .arg(ifaceDisplayName(globalInterfaces, m_fetchModel.iface),
                     m_fetchModel.lastErr));
        return;
    }

    const QString url = m_fetchModel.urls.at(m_fetchModel.pos++);
    const QString key = m_fetchModel.key;

    QHash<QString, QString> h;
    h["Authorization"] = "Bearer " + key;
    h["Content-Type"]  = "application/json";
    h["Accept"]        = "application/json";

    QPointer<AiWidget> guard(this);
    NetManager::instance()->getAsync(url, h, 20000,
        [guard, url](const QString &body, QNetworkReply::NetworkError err) {
            // ⚠ 回调在线程池线程跑 → 一律丢回 GUI 线程再碰 UI / 成员
            if (!guard) return;
            QMetaObject::invokeMethod(guard.data(), [guard, url, body, err]() {
                if (!guard) return;
                AiWidget *w = guard.data();

                QStringList ids;
                QString why;
                if (err != QNetworkReply::NoError)
                    why = QString("网络/HTTP 错误(%1)：%2").arg(int(err)).arg(body.trimmed().left(200));
                else
                    parseModelIds(body, ids, why);

                if (ids.isEmpty()) {                       // 这个地址不行，试下一个
                    w->m_fetchModel.lastErr = url + "\n  → "
                        + (why.isEmpty() ? QStringLiteral("返回内容无法识别") : why);
                    w->fetchModelListStep();
                    return;
                }

                w->m_fetchModel.inFlight = false;          // 拿到列表了，流程结束

                const int ifIdx = w->m_fetchModel.iface;
                const QString ifaceName = ifaceDisplayName(w->globalInterfaces, ifIdx);
                const QString chatUrl = (ifIdx >= 0 && ifIdx < w->globalInterfaces.size())
                                        ? w->globalInterfaces.at(ifIdx).url : QString();
                // 弹窗里的「测试」要拿这个接口的地址 + key 真发一次请求
                const QString picked = pickModelFromList(w, ifaceName, ids, chatUrl, w->m_fetchModel.key);
                if (picked.isEmpty()) return;              // 用户取消

                ModelData m;
                m.name = picked.trimmed();
                if (m.name.isEmpty()) return;

                // 已经存在同名模型 → 不新增行，直接复用它（把来源接口并进去）
                int row = -1;
                for (int i = 0; i < w->modelList.size(); ++i) {
                    if (w->modelList.at(i).name == m.name) { row = i; break; }
                }
                if (row >= 0) {
                    bool linked = false;
                    if (ifIdx >= 0 && !w->modelList[row].enabledInterfaceIndices.contains(ifIdx)) {
                        w->modelList[row].enabledInterfaceIndices.append(ifIdx);
                        linked = true;
                        w->saveToFile2();
                    }
                    w->modelListTable->selectRow(row);
                    w->modelListTable->setCurrentCell(row, 0);
                    if (ifIdx >= 0 && ifIdx < w->interfaceTable->rowCount())
                        w->interfaceTable->selectRow(ifIdx);
                    QMessageBox::information(w, "提示",
                        QString("模型「%1」已经在列表里了%2").arg(
                            m.name,
                            linked ? QStringLiteral("，已把它和这个接口关联上")
                                   : QStringLiteral("，而且这个接口早就勾上了")));
                    return;
                }

                if (ifIdx >= 0 && !m.enabledInterfaceIndices.contains(ifIdx))
                    m.enabledInterfaceIndices.append(ifIdx);   // 勾上来源接口
                w->modelList.append(m);
                row = w->modelList.size() - 1;

                const bool oldFlag = 不加载;
                不加载 = true;                              // 别让 setItem 触发的 cellChanged 回头再写一遍
                w->modelListTable->insertRow(row);
                w->modelListTable->setItem(row, 0, new QTableWidgetItem(m.name));
                不加载 = oldFlag;

                w->刷新模型();
                w->saveToFile2();

                w->modelListTable->selectRow(row);          // 选中新加的模型（会顺带刷新中间表格的勾选）
                w->modelListTable->setCurrentCell(row, 0);
                if (ifIdx >= 0 && ifIdx < w->interfaceTable->rowCount())
                    w->interfaceTable->selectRow(ifIdx);    // 顺手把来源接口选中，一眼能看到它已勾上
            }, Qt::QueuedConnection);
        },
        NetManager::CallbackOnPoolThread);
}

void AiWidget::onModelCurrentCellChanged(int currentRow, int currentCol,
                                         int previousRow, int previousCol) {
    Q_UNUSED(currentCol); Q_UNUSED(previousRow); Q_UNUSED(previousCol);
    if (currentRow < 0 || currentRow >= modelList.size()) {
        interfaceTable->setRowCount(0);
        keyTable->setRowCount(0);
        currentModelRow = -1;
        return;
    }
    currentModelRow = currentRow;

    refreshInterfaceTableForModel(currentRow);

    keyTable->setRowCount(0);
    currentInterfaceRow = -1;
}

void AiWidget::refreshInterfaceTableForModel(int modelIndex) {
    const ModelData &model = modelList.at(modelIndex);
    interfaceTable->setRowCount(globalInterfaces.size());
    不加载=1;
    for (int i = 0; i < globalInterfaces.size(); ++i) {
        const InterfaceData &iface = globalInterfaces.at(i);
        // 第一列复选框
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        bool enabled = model.enabledInterfaceIndices.contains(i);
        checkItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        checkItem->setText(iface.remark);
        interfaceTable->setItem(i, 0, checkItem);
        // 第三列接口
        interfaceTable->setItem(i, 1, new QTableWidgetItem(iface.url));
    }
    不加载=0;
}



void AiWidget::onInterfaceAdd() {



    if(globalInterfaces.size()==0)
    {
        InterfaceData newIface;

        newIface.remark = "Kimi";
        newIface.url = "https://api.moonshot.cn/v1/chat/completions";
        globalInterfaces.append(newIface);

        newIface.remark = "Openai";
        newIface.url = "https://api.openai.com/v1/chat/completions";
        globalInterfaces.append(newIface);

        newIface.remark = "DeepSeek";
        newIface.url = "https://api.deepseek.com/chat/completions";
        globalInterfaces.append(newIface);


        newIface.remark = "小米";
        newIface.url = "https://token-plan-sgp.xiaomimimo.com/v1/chat/completions";
        globalInterfaces.append(newIface);

        newIface.remark = "智谱";
        newIface.url = "https://open.bigmodel.cn/api/paas/v4/chat/completions";
        globalInterfaces.append(newIface);


        newIface.remark = "硅基流动";
        newIface.url = "https://api.siliconflow.cn/v1/chat/completions";
        globalInterfaces.append(newIface);

        newIface.remark = "咸鱼中转";
        newIface.url = "https://allgpt.xianyuw.cn/v1/chat/completions";
        globalInterfaces.append(newIface);

    }
    InterfaceData newIface;
    newIface.remark = "新接口";
    newIface.url = "https://";
    globalInterfaces.append(newIface);
    // 刷新当前模型的接口表格（如果模型选中）
    if (currentModelRow >= 0 && currentModelRow < modelList.size()) {
        refreshInterfaceTableForModel(currentModelRow);
        // 自动选中新添加的行
        int row = globalInterfaces.size() - 1;
        interfaceTable->selectRow(row);
    }
    saveToFile2();
}

void AiWidget::onInterfaceDelete() {
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) {
        QMessageBox::warning(this, "提示", "请先选择模型");
        return;
    }
    int row = interfaceTable->currentRow();
    if (row < 0 || row >= globalInterfaces.size()) {
        QMessageBox::warning(this, "提示", "请选中要删除的接口");
        return;
    }
    // 删除全局接口
    globalInterfaces.removeAt(row);
    // 从所有模型的 enabledInterfaceIndices 中移除该索引，并调整索引（因为删除后，后面的索引会减1）
    for (ModelData &model : modelList) {
        // 移除所有等于 row 的索引
        model.enabledInterfaceIndices.removeAll(row);
        // 将大于 row 的索引减1
        for (int i = 0; i < model.enabledInterfaceIndices.size(); ++i) {
            if (model.enabledInterfaceIndices[i] > row) {
                model.enabledInterfaceIndices[i]--;
            }
        }
    }
    // 刷新当前模型视图
    refreshInterfaceTableForModel(currentModelRow);
    if (globalInterfaces.isEmpty()) {
        keyTable->setRowCount(0);
        currentInterfaceRow = -1;
    } else {
        // 选中同一行或上一行
        int newRow = qMin(row, globalInterfaces.size() - 1);
        interfaceTable->selectRow(newRow);
    }
    saveToFile2();
}


void AiWidget::onInterfaceTableCellChanged(int row, int column) {
    // 只处理第二列（备注）和第三列（URL）
    if(不加载) return;
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) return;
    if (row < 0 || row >= globalInterfaces.size()) return;  // 全局接口列表

    QTableWidgetItem *item = interfaceTable->item(row, column);
    if (!item) return;

    // 获取当前文本
    QString newText = item->text();

    // 更新全局接口列表中的数据
    InterfaceData &iface = globalInterfaces[row];
    if (column == 0) {
        iface.remark = newText;
    } else if (column == 1) {
        iface.url = newText;
    }

    // 保存到文件（自动保存）
    saveToFile2();
}
void AiWidget::onInterfaceCurrentCellChanged(int currentRow, int currentCol,
                                             int previousRow, int previousCol) {
    Q_UNUSED(previousRow); Q_UNUSED(previousCol);

    if(不加载) return;
    if (currentRow < 0 || currentRow >= globalInterfaces.size()) {
        keyTable->setRowCount(0);
        currentInterfaceRow = -1;
        return;
    }
    currentInterfaceRow = currentRow;
    loadKeysForInterface(currentRow);
}

void AiWidget::loadKeysForInterface(int interfaceIndex) {
    const InterfaceData &iface = globalInterfaces.at(interfaceIndex);
    const QList<KeyData> &keys = iface.keys;
    keyTable->setRowCount(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
        const KeyData &k = keys.at(i);
        keyTable->setItem(i, 0, new QTableWidgetItem(k.key));
        keyTable->setItem(i, 1, new QTableWidgetItem(QString::number(k.usageCount)));
        keyTable->setItem(i, 2, new QTableWidgetItem(k.lastUsed));
    }
}



void AiWidget::onInterfaceItemChanged(QTableWidgetItem *item) {
    if(不加载) return;
    if (!item) return;
    int row = item->row();
    int col = item->column();
    if (col != 0) return;
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) return;
    if (row < 0 || row >= globalInterfaces.size()) return;

    bool enabled = (item->checkState() == Qt::Checked);
    ModelData &model = modelList[currentModelRow];

    if (enabled) {
        if (!model.enabledInterfaceIndices.contains(row))
            model.enabledInterfaceIndices.append(row);
    } else {
        model.enabledInterfaceIndices.removeAll(row);
    }
    saveToFile2();
}




void AiWidget::onKeyAdd() {
    if (currentInterfaceRow < 0 || currentInterfaceRow >= globalInterfaces.size()) {
        QMessageBox::warning(this, "提示", "请先点击选中一个接口（备注或接口列）");
        return;
    }
    KeyData newKey;
    newKey.key = "新密钥";
    newKey.usageCount = 0;
    newKey.lastUsed = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    globalInterfaces[currentInterfaceRow].keys.append(newKey);
    int row = globalInterfaces[currentInterfaceRow].keys.size() - 1;
    keyTable->insertRow(row);
    keyTable->setItem(row, 0, new QTableWidgetItem(newKey.key));
    keyTable->setItem(row, 1, new QTableWidgetItem(QString::number(newKey.usageCount)));
    keyTable->setItem(row, 2, new QTableWidgetItem(newKey.lastUsed));
    saveToFile2();
}

void AiWidget::onKeyDelete() {
    if (currentInterfaceRow < 0 || currentInterfaceRow >= globalInterfaces.size()) {
        QMessageBox::warning(this, "提示", "请先选中接口");
        return;
    }
    int row = keyTable->currentRow();
    if (row < 0 || row >= globalInterfaces[currentInterfaceRow].keys.size()) {
        QMessageBox::warning(this, "提示", "请选中要删除的密钥");
        return;
    }
    globalInterfaces[currentInterfaceRow].keys.removeAt(row);
    keyTable->removeRow(row);
    saveToFile2();
}
#include "machinekey.h"
void AiWidget::saveToFile2() {
    QJsonObject root;
    QByteArray keyA = MachineKey::generateKey("000");
    // 保存全局接口列表
    QJsonArray interfacesArray;
    for (const InterfaceData &iface : std::as_const(globalInterfaces)) {
        QJsonObject ifaceObj;

        ifaceObj["remark"] = iface.remark;
        ifaceObj["url"] = iface.url;

        QJsonArray keysArray;
        for (const KeyData &key : iface.keys) {
            QJsonObject keyObj;
            keyObj["key"] = MachineKey::encrypt(key.key, keyA);
            keyObj["usageCount"] = key.usageCount;
            keyObj["lastUsed"] = key.lastUsed;
            keysArray.append(keyObj);
        }
        ifaceObj["keys"] = keysArray;
        interfacesArray.append(ifaceObj);
    }
    root["interfaces"] = interfacesArray;

    // 保存模型列表（每个模型包含名称和启用的接口索引）
    QJsonArray modelsArray;
    for (const ModelData &model : std::as_const(modelList)) {
        QJsonObject modelObj;
        modelObj["name"] = model.name;
        QJsonArray enabledIndices;
        for (int idx : model.enabledInterfaceIndices) {
            enabledIndices.append(idx);
        }
        modelObj["enabledInterfaces"] = enabledIndices;
        modelsArray.append(modelObj);
    }
    root["models"] = modelsArray;

    QJsonDocument doc(root);
    QFile file(configFilePath2);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法保存模型配置文件");
        return;
    }
    file.write(doc.toJson());
    file.close();
}

void AiWidget::loadFromFile3() {
    QFile file( configFilePath2 );
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {

        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    QByteArray keyA = MachineKey::generateKey("000");
    // 加载全局接口
    globalInterfaces.clear();
    QJsonArray interfacesArray = root["interfaces"].toArray();
    for (const QJsonValue &ifaceVal : std::as_const(interfacesArray)) {
        QJsonObject ifaceObj = ifaceVal.toObject();
        InterfaceData iface;
        iface.remark = ifaceObj["remark"].toString();
        iface.url = ifaceObj["url"].toString();
        const QJsonArray keysArray = ifaceObj["keys"].toArray();
        for (const QJsonValue &keyVal : keysArray) {
            QJsonObject keyObj = keyVal.toObject();
            KeyData key;
            key.key = MachineKey::decrypt(keyObj["key"].toString(),keyA);
            key.usageCount = keyObj["usageCount"].toInt();
            key.lastUsed = keyObj["lastUsed"].toString();
            iface.keys.append(key);
        }
        globalInterfaces.append(iface);
    }

    // 加载模型
    modelList.clear();

    QJsonArray modelsArray = root["models"].toArray();
    for (const QJsonValue &modelVal : std::as_const(modelsArray)) {
        QJsonObject modelObj = modelVal.toObject();
        ModelData model;
        model.name = modelObj["name"].toString();
        QJsonArray enabledArray = modelObj["enabledInterfaces"].toArray();
        for (const QJsonValue &idxVal : std::as_const(enabledArray)) {
            model.enabledInterfaceIndices.append(idxVal.toInt());
        }
        modelList.append(model);
        int row = modelList.size() - 1;
        modelListTable->insertRow(row);
        modelListTable->setItem(row, 0, new QTableWidgetItem(model.name));
    }

    // 刷新中间表格显示（根据当前选中的模型）
    if (!modelList.isEmpty()) {
        modelListTable->selectRow(0);
    } else {
        interfaceTable->setRowCount(0);
        keyTable->setRowCount(0);
        currentModelRow = -1;
        currentInterfaceRow = -1;
    }
}
void AiWidget::onKeyTableCellChanged(int row, int column) {
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) return;
    if (currentInterfaceRow < 0 || currentInterfaceRow >= globalInterfaces.size()) return;

    // 获取当前选中的接口
    InterfaceData &iface = globalInterfaces[currentInterfaceRow];
    if (row < 0 || row >= iface.keys.size()) return;

    QTableWidgetItem *item = keyTable->item(row, column);
    if (!item) return;

    QString newText = item->text();
    KeyData &keyData = iface.keys[row];

    switch (column) {
    case 0: // key
        keyData.key = newText;
        break;
    case 1: // 使用次数
        keyData.usageCount = newText.toInt();
        break;
    case 2: // 最后使用时间
        keyData.lastUsed = newText;
        break;
    default:
        return;
    }
    saveToFile2();
}






void AiWidget::内置函数(const QString &Name,const QString &remark,const QStringList &params)
{

    QJsonObject functionObj;
    functionObj["name"] = Name;
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
    m_fun.insert(Name,toolObj);
    bool ok=false;
    for (const auto &fun : std::as_const(functionList))
    {
        if(fun.funcName==Name)
        {
            ok=true;
            break;
        }
    }
    if(ok) return;
    FunctionData d;
    d.code="#内置函数 不需要理会";
    d.funcName = Name;
    d.params = params;
    d.remark = remark;
    functionList.append(d);

    // 在表格中添加行
    int row = functionList.size() - 1;
    funcListTable->insertRow(row);
    QTableWidgetItem *item = new QTableWidgetItem(d.remark);
    funcListTable->setItem(row, 0, item);


}


QJsonArray AiWidget::get_tools(const AccountInfo *info)
{

    QJsonArray toolsArray;
    for (const auto &fun : std::as_const(functionList)) {
        if(fun.funcName.isEmpty()) continue;
        if(!info->tools.contains(fun.funcName)) continue;

        if(m_fun.contains(fun.funcName))
        {
            toolsArray.append(m_fun[fun.funcName]);
            continue;
        }
        QJsonObject functionObj;
        functionObj["name"] = fun.funcName;
        functionObj["description"] = fun.remark;

        QJsonObject parameters;
        parameters["type"] = "object";
        QJsonObject properties;
        QJsonArray required;

        int paramIndex = 1;
        for (const QString &paramName : fun.params) {
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
        toolsArray.append(toolObj);
    }

    // ---- 插件工具（「插件工具」页里勾选的插件指令）----
    // 和内置函数一样**按机器人分开**：同一条工具，A 号勾了、B 号没勾，就只发给 A 号。
    // 勾选存在 PluginToolItem::bots 里（见 pluginToolEnabledFor）。
    QList<PluginToolItem> ptList;
    {
        QMutexLocker lk(&m_pluginToolsMutex);
        for (const PluginToolItem &it : m_pluginTools)
            if (!it.toolName.isEmpty() && pluginToolEnabledFor(it, info->appid_int))
                ptList.append(it);
    }
    for (const PluginToolItem &it : std::as_const(ptList))
        toolsArray.append(pluginToolFunctionJson(it));

    return toolsArray;

}

void AiWidget::内置函数()
{
    内置函数("run_python","运行python代码 有审核 请勿违规",QStringList() << "python代码 如 #下面代码由用户要求测试\n__result__='测试'"  );
    内置函数("dimg","添加表情包",QStringList()<< "本地或网络路径" << "保存的文件名 如 结婚.gif");
    内置函数("rimg","删除表情包",QStringList() << "文件名 如 结婚.gif");
    内置函数("dingshy","添加一个定时 在指定时间触发一次主动",QStringList()<< "备注 如 叫xx起床" << "时间 定时时间格式 年,月,日,时,分 如 10,10 每日10点10分 触发一次对话 你可以叫用户起床等");
    内置函数("getdings","获取与当前用户的定时列表",QStringList() << "无参数");
    内置函数("redings","删除一个定时",QStringList() << "定时id");
    内置函数("byss","必应搜索",QStringList() << "搜索关键词 如 原神"<<"页码 1开始 如 1");
    内置函数("llwye","浏览网页 返回提取后的文本 当用户发送链接时 可以使用",QStringList() << "链接 如 https://www.baidu.com");

    // 长期日志：记下来的内容会在之后每轮请求里作为系统提示词的一部分带上，
    // 所以每次都用一句话，别把过程性内容往里塞。存储/裁剪逻辑在 ai/ailog.h。
    内置函数("write_log",
             "把需要长期记住的内容写进长期日志 这份日志会在之后每次对话时作为系统提示词的一部分提供给你 "
             "适合记录称呼 偏好 约定 承诺 群内事实等 有长度上限 超出会自动丢弃最早的记录 "
             "所以只写精炼的一句话 不要记录过程性内容",
             QStringList() << "要长期记住的内容 一句话 如 群友张三喜欢原神 聊这个时别扯别的");

    //内置函数("html_to_img","截图某个网页 可传入html文本 为用户绘制样式 注意是png",QStringList() << "链接 或 html文本 如 https://www.baidu.com");

    funcListTable->selectRow(0);
    onFuncListCurrentCellChanged(0, 0, -1, -1);
}

// ==================== 插件工具：把插件的指令注册成 AI 工具 ====================
// 为什么要有这一套：插件的「指令」是给用户手打的，注册时只给了匹配方式（完全相等 / 开头匹配…），
// **没有任何说明文字和参数定义**。AI 不知道它存在、也不知道参数怎么填，所以这里：
//   ① 把已加载插件 → 指令列出来，用户勾一条就注册一个工具；
//   ② 说明 / 参数说明用户可以自己填，留空就按「插件名 + 指令 + 匹配方式」自动生成，
//      默认给一个可留空的 p1（AI 想补参数就往后拼，插件那边按空格切即可）；
//   ③ 调用走 pluginPage->invokeToolCommand（不经关键词匹配，只跑这一条）。
// 返回值：Python return 的文本 / 原生库 onMessagev3 的返回值直接可用；
//         JS、32 位易语言、以及那些自己调发送 API 的插件没有返回值 → 在发送入口把它们
//         想说出口的话**扣下来**（msgid 打 "|-token|" 标记 + 全局 gate 表），一个字都不发出去，
//         正文直接交给 AI 决定怎么处理。
QPair<int, QString> splitWrappedMsgId(const QString &wrapped);   // 定义在 core/api2.cpp

static QString ptIdentity(const QString &id, const QString &name)
{
    return id.isEmpty() ? name : id;
}

static QString pluginToolMatchName(int t)
{
    switch (t) {
    case 0: return "完全相等";
    case 1: return "开头匹配";
    case 2: return "结尾匹配";
    case 3: return "包含";
    case 4: return "正则";
    case 5: return "事件";
    default: return "未知";
    }
}

// 指令 → 合法的函数名。OpenAI 的 function.name 只吃 [a-zA-Z0-9_-] 且 ≤64 字符，
// 中文指令 sanitize 完是空的 → 用 "cmd" 占位，靠后面的哈希保证唯一（插件不改名就一直不变）。
static QString pluginToolMakeName(const QString &identity, int matchType, const QString &key)
{
    QString base;
    for (const QChar c : key) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_' || u == '-')
            base += c;
        if (base.size() >= 16) break;
    }
    if (base.isEmpty()) base = QStringLiteral("cmd");
    const QString seed = identity + "|" + QString::number(matchType) + "|" + key;
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Md5).toHex().left(8));
    return QStringLiteral("pt_") + base + "_" + hash;
}

// 某台机器人是否启用这条插件工具。
// bots 里带 "*" = 所有机器人（旧版全局勾选迁移过来的标记），一改动就会被展开成具体 appid。
bool AiWidget::pluginToolEnabledFor(const PluginToolItem &it, int appid)
{
    if (it.bots.contains(QStringLiteral("*"))) return true;   // 旧版全局勾选
    return it.bots.contains(QString::number(appid));
}

QString AiWidget::currentBotName() const
{
    for (const auto &acc : std::as_const(m_accounts))
        if (acc && acc->appid_int == g_appid)
            return acc->Ai_nickname.isEmpty() ? QString::number(acc->appid_int) : acc->Ai_nickname;
    return QString::number(g_appid);
}

QStringList AiWidget::allBotAppids() const
{
    QStringList out;
    for (const auto &acc : std::as_const(m_accounts))
        if (acc) out << QString::number(acc->appid_int);
    if (!out.contains(QString::number(g_appid))) out << QString::number(g_appid);   // 兜底：自己一定要在
    return out;
}

QString AiWidget::pluginToolHintText() const
{
    return QString(
        "当前机器人：%1 —— 打勾只对这一台生效，换机器人时这里的勾会跟着变。"
        "「工具说明」写这个工具干什么用（留空自动生成）；"
        "「参数」一行一个参数，行首加 * 表示必填，留空 = 没有参数。"
        "（双击单元格编辑，回车换行，点别处保存）").arg(currentBotName());
}

void AiWidget::loadPluginTools()
{
    QList<PluginToolItem> items;
    int waitMs = 10000;

    QFile f(pluginToolCfgPath);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const QJsonArray arr = root["items"].toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            PluginToolItem it;
            it.pluginId   = o["pid"].toString();
            it.pluginName = o["pname"].toString();
            it.matchType  = o["t"].toInt();
            it.key        = o["k"].toString();
            it.toolName   = o["n"].toString();
            it.desc       = o["d"].toString();
            it.enabled    = o["e"].toBool();
            // 老配置只有全局开关 e：true 就迁成「所有机器人」（"*"），用户下次改动时再展开成具体 appid
            const QJsonArray bs = o["bots"].toArray();
            for (const QJsonValue &bv : bs) {
                const QString b = bv.toString().trimmed();
                if (!b.isEmpty()) it.bots.append(b);
            }
            if (it.bots.isEmpty() && it.enabled) it.bots.append(QStringLiteral("*"));
            const QJsonArray ps = o["p"].toArray();
            for (const QJsonValue &pv : ps) it.params.append(pv.toString());
            if (it.key.isEmpty()) continue;
            if (it.toolName.isEmpty())
                it.toolName = pluginToolMakeName(ptIdentity(it.pluginId, it.pluginName), it.matchType, it.key);
            items.append(it);
        }
        const int w = root["wait_ms"].toInt();
        if (w > 0) waitMs = w;
    }
    m_pluginToolWaitMs = qBound(1000, waitMs, 3000);   // 阻塞等待，上限 3 秒

    {
        QMutexLocker lk(&m_pluginToolsMutex);
        m_pluginTools = items;
    }
    rebuildPluginToolIndex();

    if (pluginToolWaitSpin) {
        不加载 = 1;      // 回填别写盘
        pluginToolWaitSpin->setValue(m_pluginToolWaitMs / 1000);
        不加载 = 0;
    }
}

void AiWidget::savePluginTools() const
{
    QJsonArray arr;
    {
        QMutexLocker lk(&m_pluginToolsMutex);
        for (const PluginToolItem &it : m_pluginTools) {
            QJsonObject o;
            o["pid"]   = it.pluginId;
            o["pname"] = it.pluginName;
            o["t"]     = it.matchType;
            o["k"]     = it.key;
            o["n"]     = it.toolName;
            o["d"]     = it.desc;
            o["e"]     = it.enabled;
            QJsonArray bs;
            for (const QString &b : it.bots) bs.append(b);
            o["bots"] = bs;
            QJsonArray ps;
            for (const QString &p : it.params) ps.append(p);
            o["p"] = ps;
            arr.append(o);
        }
    }
    QJsonObject root;
    root["wait_ms"] = m_pluginToolWaitMs;
    root["items"]   = arr;

    QDir().mkpath(QFileInfo(pluginToolCfgPath).absolutePath());
    QFile f(pluginToolCfgPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void AiWidget::rebuildPluginToolIndex()
{
    QHash<QString, int> idx;
    {
        QMutexLocker lk(&m_pluginToolsMutex);
        for (int i = 0; i < m_pluginTools.size(); ++i) {
            const PluginToolItem &it = m_pluginTools.at(i);
            if (it.toolName.isEmpty()) continue;
            // 这里只做「名字 → 配置项」的认领（含未启用的）。是否对当前机器人启用
            // 由 get_tools 过滤、runToolLoop 调之前再校验一次。
            idx.insert(it.toolName, i);
        }
    }
    QMutexLocker lk(&m_pluginToolsMutex);
    m_pluginToolByName = idx;
}

PluginToolItem *AiWidget::findPluginToolItem(const QString &identity, int matchType, const QString &key)
{
    // 调用方负责持锁（GUI 线程的表格回调里都是先加锁再用）
    for (int i = 0; i < m_pluginTools.size(); ++i) {
        PluginToolItem &it = m_pluginTools[i];
        if (it.matchType != matchType || it.key != key) continue;
        if (ptIdentity(it.pluginId, it.pluginName) != identity) continue;
        return &it;
    }
    return nullptr;
}

void AiWidget::refreshPluginToolPlugins()
{
    if (!pluginToolPluginList || !pluginToolTable) return;

    // 记下当前选中的插件身份，重建后尽量保持在同一行
    QString keepIdentity;
    if (m_pluginToolCurPlugin >= 0 && m_pluginToolCurPlugin < m_pluginList.size()) {
        const PluginInfo &p = m_pluginList.at(m_pluginToolCurPlugin);
        keepIdentity = ptIdentity(p.id, p.name);
    }

    {
        QSignalBlocker blocker(pluginToolPluginList);
        不加载 = 1;
        pluginToolPluginList->clear();
        for (int i = 0; i < m_pluginList.size(); ++i) {
            const PluginInfo &p = m_pluginList.at(i);
            int n = 0;
            if (p.type == 0) {
                for (const Rule &r : p.python.rules) if (r.role > kRuleRoleOff) ++n;
            } else if (p.type == 3) {
                for (const Rule_js &r : p.js.rules) if (r.role > kRuleRoleOff) ++n;
            } else {
                for (const Rule_Dll &r : p.DLL.rules) if (r.role > kRuleRoleOff) ++n;
            }
            if (n == 0) continue;    // 没注册指令的插件（老式每条都喂的除外）不列

            auto *item = new QListWidgetItem(QString("%1 (%2)").arg(p.name).arg(n));
            item->setData(Qt::UserRole, i);                              // m_pluginList 下标
            item->setData(Qt::UserRole + 1, ptIdentity(p.id, p.name));    // 稳定身份
            item->setToolTip(QString("%1\n类型：%2\n指令数：%3%4")
                                 .arg(p.name)
                                 .arg(p.type == 0 ? "Python" : p.type == 1 ? "原生库"
                                      : p.type == 2 ? "DLL32(易语言)" : "JS")
                                 .arg(n)
                                 .arg(p.enabled ? "" : "\n（插件当前已停用，工具调用会直接报错）"));
            pluginToolPluginList->addItem(item);
            if (!keepIdentity.isEmpty() && ptIdentity(p.id, p.name) == keepIdentity)
                pluginToolPluginList->setCurrentRow(pluginToolPluginList->count() - 1);
        }
        不加载 = 0;
    }

    if (pluginToolPluginList->count() == 0) {
        m_pluginToolCurPlugin = -1;
        m_pluginToolRows.clear();
        不加载 = 1;
        pluginToolTable->setRowCount(0);
        不加载 = 0;
        return;
    }
    if (pluginToolPluginList->currentRow() < 0)
        pluginToolPluginList->setCurrentRow(0);
    onPluginToolPluginChanged(pluginToolPluginList->currentRow());
}

void AiWidget::onPluginToolPluginChanged(int row)
{
    if (不加载) return;
    if (row < 0 || row >= pluginToolPluginList->count()) {
        m_pluginToolCurPlugin = -1;
        return;
    }
    m_pluginToolCurPlugin = pluginToolPluginList->item(row)->data(Qt::UserRole).toInt();
    rebuildPluginToolTable();
}

void AiWidget::rebuildPluginToolTable()
{
    if (!pluginToolTable) return;

    if (pluginToolHintLabel) pluginToolHintLabel->setText(pluginToolHintText());
    不加载 = 1;
    pluginToolTable->setRowCount(0);
    m_pluginToolRows.clear();

    if (m_pluginToolCurPlugin < 0 || m_pluginToolCurPlugin >= m_pluginList.size()) {
        不加载 = 0;
        return;
    }
    const PluginInfo &p = m_pluginList.at(m_pluginToolCurPlugin);
    const QString identity = ptIdentity(p.id, p.name);

    // 收集「启用」的指令（停用的不列：勾了也调不动）
    struct RawRule { int t; QString key; QString view; };
    QList<RawRule> raws;
    if (p.type == 0) {
        for (const Rule &r : p.python.rules) {
            if (r.role <= kRuleRoleOff) continue;
            raws.append({ static_cast<int>(r.type), r.key,
                          r.newKey.isEmpty() ? r.key : (r.newKey + "  (原:" + r.key + ")") });
        }
    } else if (p.type == 3) {
        for (const Rule_js &r : p.js.rules) {
            if (r.role <= kRuleRoleOff) continue;
            raws.append({ static_cast<int>(r.type), r.key,
                          r.newKey.isEmpty() ? r.key : (r.newKey + "  (原:" + r.key + ")") });
        }
    } else {
        for (const Rule_Dll &r : p.DLL.rules) {
            if (r.role <= kRuleRoleOff) continue;
            raws.append({ static_cast<int>(r.type), r.key,
                          r.newKey.isEmpty() ? r.key : (r.newKey + "  (原:" + r.key + ")") });
        }
    }

    pluginToolTable->setRowCount(raws.size());
    for (int row = 0; row < raws.size(); ++row) {
        const RawRule &r = raws.at(row);

        PluginToolItem cfg;
        bool hasCfg = false;
        {
            QMutexLocker lk(&m_pluginToolsMutex);
            if (const PluginToolItem *found = findPluginToolItem(identity, r.t, r.key)) {
                cfg = *found;
                hasCfg = true;
            }
        }

        // 第 0 列：勾选 + 指令名合成一格（指令名只用来认，不能改；匹配方式/工具名塞 tooltip）
        // 正则 / 事件的「指令」不是命令词，加个后缀，免得用户以为它是能直接打的话
        QString viewText = r.view;
        if (r.t == 4)      viewText += QStringLiteral("  [正则]");
        else if (r.t == 5) viewText += QStringLiteral("  [事件]");
        auto *cCmd = new QTableWidgetItem(viewText);
        cCmd->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        cCmd->setCheckState((hasCfg && pluginToolEnabledFor(cfg, g_appid)) ? Qt::Checked : Qt::Unchecked);
        cCmd->setToolTip(QString("匹配：%1\n工具名：%2\n插件原指令：%3")
                             .arg(pluginToolMatchName(r.t),
                                  hasCfg ? cfg.toolName : pluginToolMakeName(identity, r.t, r.key),
                                  r.key));
        pluginToolTable->setItem(row, 0, cCmd);

        auto *cDesc = new QTableWidgetItem(hasCfg ? cfg.desc : QString());
        cDesc->setToolTip(cDesc->text().isEmpty()
            ? "双击填写：这个工具是干什么用的（会当成工具描述发给 AI）。\n"
              "留空 = 自动生成一句（插件名 + 指令 + 匹配方式）。"
            : cDesc->text());
        pluginToolTable->setItem(row, 1, cDesc);

        auto *cPar = new QTableWidgetItem(hasCfg ? cfg.params.join('\n') : QString());
        cPar->setToolTip(cPar->text().isEmpty()
            ? "双击填写：一行一个参数（顺序就是 p1、p2…），行首加 * 表示必填。\n"
              "回车换行，点别处保存。\n"
              "留空 = 给 AI 一个可留空的 p1。"
            : cPar->text());
        pluginToolTable->setItem(row, 2, cPar);

        m_pluginToolRows.append({ r.t, r.key, r.view });
    }
    不加载 = 0;
}

void AiWidget::onPluginToolItemChanged(QTableWidgetItem *item)
{
    if (不加载 || !item) return;
    const int row = item->row();
    if (row < 0 || row >= m_pluginToolRows.size()) return;
    if (m_pluginToolCurPlugin < 0 || m_pluginToolCurPlugin >= m_pluginList.size()) return;

    const PluginInfo &p = m_pluginList.at(m_pluginToolCurPlugin);
    const QString identity = ptIdentity(p.id, p.name);
    const PluginToolRow &r = m_pluginToolRows.at(row);

    const QTableWidgetItem *chk = pluginToolTable->item(row, 0);
    const bool wantOn = chk && chk->checkState() == Qt::Checked;

    QMutexLocker lk(&m_pluginToolsMutex);
    const QString me = QString::number(g_appid);
    PluginToolItem *cfg = findPluginToolItem(identity, r.matchType, r.key);
    if (!cfg) {
        // 先填说明再勾也允许：这里照样建一条。勾选按机器人生效，新条目只给当前这台。
        PluginToolItem ni;
        ni.pluginId   = p.id;
        ni.pluginName = p.name;
        ni.matchType  = r.matchType;
        ni.key        = r.key;
        ni.toolName   = pluginToolMakeName(identity, r.matchType, r.key);
        ni.enabled    = wantOn;               // 只留给旧版本读，判断一律看 bots
        if (wantOn) ni.bots << me;
        m_pluginTools.append(ni);
        cfg = &m_pluginTools.last();
    }

    switch (item->column()) {
    case 0: {   // 勾选框和指令名在同一格
        // 勾选/取消**只改当前机器人**。旧配置的 "*"（= 所有机器人）在这里展开成具体 appid，
        // 之后逐台维护；否则取消一台时会把别的机器人一起关掉。
        if (cfg->bots.contains(QStringLiteral("*")))
            cfg->bots = allBotAppids();
        if (wantOn) { if (!cfg->bots.contains(me)) cfg->bots << me; }
        else        { cfg->bots.removeAll(me); }
        cfg->enabled = wantOn;                // 旧字段跟着走，纯兼容
        break;
    }
    case 1: cfg->desc    = item->text(); break;
    case 2: {
        cfg->params.clear();
        const QStringList lines = item->text().split('\n');
        for (const QString &line : lines) {
            const QString s = line.trimmed();
            if (!s.isEmpty()) cfg->params.append(s);
        }
        break;
    }
    default: break;
    }
    lk.unlock();

    rebuildPluginToolIndex();
    savePluginTools();
}

// 换机器人后跟着变：勾选状态 + 顶部提示都按当前机器人重画。
// 不走 refreshPluginToolPlugins —— 那个会重新枚举 m_pluginList，切一次机器人不必那么重。
void AiWidget::refreshPluginToolChecks()
{
    if (!pluginToolTable) return;
    if (pluginToolHintLabel) pluginToolHintLabel->setText(pluginToolHintText());

    QString identity;
    if (m_pluginToolCurPlugin >= 0 && m_pluginToolCurPlugin < m_pluginList.size()) {
        const PluginInfo &p = m_pluginList.at(m_pluginToolCurPlugin);
        identity = ptIdentity(p.id, p.name);
    }

    QSignalBlocker blocker(pluginToolTable);
    const int prev = 不加载;
    不加载 = 1;
    for (int row = 0; row < m_pluginToolRows.size() && row < pluginToolTable->rowCount(); ++row) {
        QTableWidgetItem *chk = pluginToolTable->item(row, 0);
        if (!chk) continue;
        const PluginToolRow &r = m_pluginToolRows.at(row);
        bool on = false;
        {
            QMutexLocker lk(&m_pluginToolsMutex);
            const PluginToolItem *cfg = findPluginToolItem(identity, r.matchType, r.key);
            if (cfg) on = pluginToolEnabledFor(*cfg, g_appid);
        }
        chk->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    不加载 = prev;
}

void AiWidget::onPluginToolWaitChanged(int secs)
{
    if (不加载) return;
    m_pluginToolWaitMs = qBound(1, secs, 3) * 1000;
    savePluginTools();
}

QJsonObject AiWidget::pluginToolFunctionJson(const PluginToolItem &it) const
{
    QJsonObject functionObj;
    functionObj["name"] = it.toolName;

    QString desc = it.desc.trimmed();
    if (desc.isEmpty()) {
        // 插件没给说明 → 按「插件 + 指令 + 匹配方式」自动编一句，至少让 AI 知道这是干什么的
        desc = QString("调用插件「%1」的指令「%2」（%3）。"
                       "插件想把内容发到聊天里时会被拦下来转交给你（不会自动发出），"
                       "拿到的就是它原本要说的话 —— 要不要发、怎么发由你决定。")
                   .arg(it.pluginName, it.key, pluginToolMatchName(it.matchType));
    }
    // ⚠ 正则 / 事件两类要给 AI 额外说清楚，否则它填的参数一定对不上：
    //   · 正则：key 是模式串（不是命令词）。框架调用时**不会**重新走正则匹配（按 key+type 直接定位规则），
    //     但插件内部通常自己拿正文再跑一次正则 → 必须让它填「能被该正则命中」的完整文本。
    //   · 事件：key 是消息类型，主动调用一般没效果，提前讲明白，免得 AI 白调一次。
    if (it.matchType == 4) {
        // ⚠ 先单独 arg 再拼：desc 是用户填的，里面如果有 %1 会被这一步顺手替换掉
        const QString note = QString("（这条指令是**正则匹配**，正则：%1。你给的参数会**原样**成为正文，"
                                     "请把它写成能被这条正则匹配的完整文本 —— 正则里的 ^ $ \\s (.+) 等符号"
                                     "要按格式换成实际内容，不要只丢一个关键字，也不要把正则符号本身写进去。）")
                                 .arg(it.key);
        desc += QLatin1Char('\n') + note;
    } else if (it.matchType == 5) {
        desc += QStringLiteral("\n（这是一条**事件触发**指令，平时由消息事件唤起；"
                               "你主动调用它通常不会有实际效果。）");
    }
    // 不管说明是不是用户自己写的，都要告诉 AI：媒体标签是可直接发送的（回复链路会解析）
    desc += QStringLiteral("\n（返回里如果带 [image,path=…] / [video,…] / [audio,…] / [file,…] 这类标签，"
                           "那是可以直接发送的媒体内容，想发就原样放进你的回复。）");
    functionObj["description"] = desc;

    QJsonObject parameters;
    parameters["type"] = "object";
    QJsonObject properties;
    QJsonArray required;

    // 正则规则的参数不是「指令后面补的词」，而是「能命中那条正则的整句话」，默认描述要区别对待
    const QString defP1Desc = (it.matchType == 4)
        ? QStringLiteral("能被上面那条正则匹配的完整文本（会原样作为正文，例：正则 ^天气\\s+(.+)$ → 填「天气 北京」）")
        : QStringLiteral("指令后面的参数（可留空；多个参数用空格分隔）");

    if (it.params.isEmpty()) {
        // 默认给一个可留空的参数：指令后面要补东西时 AI 就填这里，插件按空格切。
        QJsonObject p1;
        p1["type"] = "string";
        p1["description"] = defP1Desc;
        properties["p1"] = p1;
    } else {
        int n = 0;
        for (const QString &raw : it.params) {
            QString d = raw.trimmed();
            if (d.isEmpty()) continue;
            const bool must = d.startsWith('*');     // 行首 * = 必填
            if (must) d = d.mid(1).trimmed();
            const QString key = QString("p%1").arg(++n);
            QJsonObject pd;
            pd["type"] = "string";
            pd["description"] = d;
            properties[key] = pd;
            if (must) required.append(key);
        }
        if (n == 0) {    // 全是空行 → 退回默认那个可选参数
            QJsonObject p1;
            p1["type"] = "string";
            p1["description"] = defP1Desc;
            properties["p1"] = p1;
        }
    }

    parameters["properties"] = properties;
    if (!required.isEmpty()) parameters["required"] = required;
    functionObj["parameters"] = parameters;

    QJsonObject toolObj;
    toolObj["type"] = "function";
    toolObj["function"] = functionObj;
    return toolObj;
}

// p1..p8 按顺序拼成一行（插件按空格切参数，和用户手打一致）。同步/回调两版共用。
QString AiWidget::ptJoinArgs(const QString &argsJson)
{
    QStringList parts;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(argsJson.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject o = doc.object();
        for (int i = 1; i <= 8; ++i) {
            const QJsonValue v = o.value(QString("p%1").arg(i));
            QString s = v.isString() ? v.toString()
                      : v.isDouble() ? QString::number(v.toDouble()) : QString();
            s = s.trimmed();
            if (!s.isEmpty()) parts.append(s);
        }
    }
    return parts.join(QLatin1Char(' '));
}

// flushPendingMessages 拼的 ev 里 user 可能等于 groupId（群不开「群个人」时 openid 就是群号），
// 插件要的是真正的发送人 → 按 msgid 里的日志下标把那条入站消息读回来补上。
void AiWidget::ptFillSender(MessageEvent &ev)
{
    const int baseSeq = splitWrappedMsgId(ev.msgId).first;
    if (baseSeq <= 0) return;
    LogDB *ldb = g_logdb[mapTypeToTabIndex(ev.type)].get();
    if (!ldb) return;
    Message m;
    if (ldb->readLog(QString::number(ev.appid), ev.groupId, static_cast<uint64_t>(baseSeq), m)) {
        if (!m.user.isEmpty()) ev.user = m.user;
        if (!m.name.isEmpty()) ev.nickname = m.name;
    }
}

QString AiWidget::runPluginTool(const PluginToolItem &it, const QString &argsJson, const MessageEvent &evIn)
{
    // ---- 1) 参数 + 事件副本（补真实发送人）----
    const QString argsText = ptJoinArgs(argsJson);
    MessageEvent ev = evIn;
    ptFillSender(ev);

    // ---- 3) 设一道 gate：插件接下来要发的话会被扣在这里，一个字都不会真发出去 ----
    //    msgid 打上 "|-<token>|" 前缀（真实 msgid 原样保留在第二个 '|' 后面）：
    //    插件把 msgid 原样传回来时，发送入口一眼就能认出「这是工具调用触发的」并扣下。
    //    ⚠ 插件主动推送（msgid 为空）拦不到 —— 那种不管它，照常发。
    if (!pluginPage) return QStringLiteral("插件管理器还没初始化，无法调用插件指令。");
    const int gate = toolGateBegin();
    ev.msgId = toolGateMsgId(gate, ev.msgId);

    // ---- 4) 调插件（不经关键词匹配，只跑这一条）----
    QString out, errMsg;
    const int rc = pluginPage->invokeToolCommand(it.pluginId, it.pluginName,
                                                it.matchType, it.key, argsText,
                                                ev, out, errMsg);

    if (rc < 0) {
        toolGateEnd(gate);
        return QString("调用插件指令失败：%1").arg(errMsg);
    }

    // ---- 5) 取被扣下的消息 ----
    //   · 同步有返回值（Python return / 原生库 onMessagev3 返回值）→ 只顺手收一下已经拦到的
    //     （最多 300ms，静默 150ms 就走），避免为了一句额外的补充白等；
    //   · 没有返回值（JS / 32 位易语言 / 纯发送型插件）→ 等它把话说完（首条后静默 600ms），
    //     ⚠ 这段是**阻塞**当前线程的，所以硬上限 3 秒（再久也不等，缺的内容让 AI 自己说明）。
    const int waitMs = qBound(300, m_pluginToolWaitMs, 3000);
    QStringList got = (rc == 0) ? toolGateWait(gate, 300, 150)
                                : toolGateWait(gate, waitMs, 600);
    toolGateEnd(gate);

    // ---- 6) 合并：同步返回值在前，被扣下的消息按顺序补在后面（去重）----
    //    ⚠ 富媒体标签（[image,path=…] / [video,…] / [audio,…] / [file,…]）**原样保留**：
    //       AI 的回复走 send_messagesAsync → processImageTagsAsync，带标签就能真发出去。
    QStringList lines;
    if (rc == 0) {
        const QString v = out.trimmed();
        if (!v.isEmpty()) lines.append(v);
    }
    for (const QString &t : std::as_const(got)) {
        const QString v = t.trimmed();
        if (v.isEmpty() || lines.contains(v)) continue;
        lines.append(v);
    }
    if (!lines.isEmpty())
        return lines.join(QLatin1Char('\n'));

    // ---- 7) 还是什么都没有 ----
    if (rc > 0)
        return QString("插件「%1」已执行指令「%2」，但没有输出任何内容"
                       "（既没有返回值，也没有发消息）。")
                   .arg(it.pluginName, it.key);
    return QString("插件「%1」执行了指令「%2」，返回的是空文本。").arg(it.pluginName, it.key);
}

QString AiWidget::generateHash(const QString &url)
{
    return QString(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString AiWidget::downloadImage(const QString &url, const QString &hash)
{
    QString localPath = "tmp/image/" + hash + ".png";
    if (QFile::exists(localPath))
        return localPath;

    QString err;
    return downloadFile(url,localPath,err) ? localPath : QString();
}

PendingMessage AiWidget::parseImageTagsAndDownload(const QString &msg)
{
    PendingMessage result;
    QString text = msg;
    QRegularExpression re("\\[image,([^\\]]*)\\]");
    QRegularExpressionMatchIterator it = re.globalMatch(msg);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString tagContent = match.captured(1); // 如 "height=1080,width=1080,url=https://..."
        QString url;
        // 按逗号分割键值对
        QStringList pairs = tagContent.split(',', Qt::SkipEmptyParts);
        for (const QString &pair : std::as_const(pairs)) {
            int eqPos = pair.indexOf('=');
            if (eqPos != -1) {
                QString key = pair.left(eqPos).trimmed();
                QString value = pair.mid(eqPos + 1).trimmed();
                if (key == "url") {
                    url = value;
                    break;
                }
            }
        }
        if (!url.isEmpty()) {
            QString hash = generateHash(url);
            QString localPath = downloadImage(url, hash);
            if (!localPath.isEmpty()) {
                result.imagePaths.append(localPath);

                text.replace(match.captured(0), "![img](" + localPath + ")");
            } else {

                text.replace(match.captured(0), "[img err]");
            }
        } else {
            // 没有 url，保持原标签不变
        }
    }

    result.text = text.trimmed();
    return result;
}

void AiWidget::appendPendingMessageToContext(QJsonObject &context, const PendingMessage &pm)
{
    if (!context.contains("messages") || !context["messages"].isArray()) {
        context["messages"] = QJsonArray();
    }
    QJsonArray messages = context["messages"].toArray();

    QJsonObject msgObj;
    msgObj["role"] = "user";
    QJsonArray content;

    // 文本部分
    if (!pm.text.isEmpty()) {
        QJsonObject textItem;
        textItem["type"] = "text";
        textItem["text"] = pm.text;
        content.append(textItem);
    }

    // 图片部分（存本地路径）
    for (const QString &path : pm.imagePaths) {
        QJsonObject imageItem;
        imageItem["type"] = "image_url";
        QJsonObject imageUrlObj;
        imageUrlObj["url"] = path; // 绝对路径
        imageItem["image_url"] = imageUrlObj;
        content.append(imageItem);
    }

    msgObj["content"] = content;
    messages.append(msgObj);
    context["messages"] = messages;
}

void AiWidget::trimContextImages(QJsonObject &context, int maxUserMessages)
{
    if (!context.contains("messages") || !context["messages"].isArray())
        return;

    QJsonArray messages = context["messages"].toArray();
    QList<int> userMsgIndices;

    // 从后向前收集最近 maxUserMessages 条 user 消息的索引
    if(maxUserMessages>0){
        for (int i = messages.size() - 1; i >= 0; --i) {
            QJsonObject msg = messages[i].toObject();
            if (msg["role"].toString() == "user") {
                userMsgIndices.append(i);
                if (userMsgIndices.size() >= maxUserMessages)
                    break;
            }
        }
    }
    QSet<int> keepIndices;
    for (int idx : userMsgIndices) {
        keepIndices.insert(idx);
    }

    // 遍历所有消息，如果该消息不是最近保留的 user 消息，则移除其 content 中的 image_url 元素
    for (int i = 0; i < messages.size(); ++i) {
        if (keepIndices.contains(i))
            continue; // 保留

        QJsonObject msg = messages[i].toObject();
        if (!msg.contains("content") || !msg["content"].isArray())
            continue;

        QJsonArray content = msg["content"].toArray();
        QJsonArray newContent;
        for (const QJsonValue &val : std::as_const(content)) {
            QJsonObject item = val.toObject();
            if (item["type"].toString() != "image_url") {
                newContent.append(item);
            }
        }
        if (newContent.size() != content.size()) {
            msg["content"] = newContent;
            messages[i] = msg;
            //qDebug() << "已移除消息索引" << i << "中的图片（非最近3轮对话）";
        }
    }

    context["messages"] = messages;
}
void AiWidget::trimToolResponses(QJsonObject &context, int maxToolMessages, int truncateLimit)
{
    if (!context.contains("messages") || !context["messages"].isArray())
        return;

    QJsonArray messages = context["messages"].toArray();
    QList<int> toolIndices;

    // 收集所有 tool 消息的索引
    for (int i = 0; i < messages.size(); ++i) {
        QJsonObject msg = messages[i].toObject();
        if (msg["role"].toString() == "tool") {
            toolIndices.append(i);
        }
    }

    int totalTools = toolIndices.size();
    if (totalTools <= maxToolMessages) {
        // 数量未超限，无需截断
        //qDebug() << "[trimToolResponses] tool消息数量" << totalTools << "未超过限制" << maxToolMessages;
        return;
    }

    // 需要截断较早的 tool 消息（从索引 0 到 totalTools - maxToolMessages - 1）
    int keepStartIndex = totalTools - maxToolMessages; // 保留最近 maxToolMessages 条
    for (int i = 0; i < keepStartIndex; ++i) {
        int msgIndex = toolIndices[i];
        QJsonObject msg = messages[msgIndex].toObject();
        if (msg.contains("content") && msg["content"].isString()) {
            QString content = msg["content"].toString();
            if (content.length() > truncateLimit) {
                content = content.left(truncateLimit) + "...(truncated)";
                msg["content"] = content;
                messages[msgIndex] = msg;
                //qDebug() << "[trimToolResponses] 截断 tool 消息索引" << msgIndex;
            }
        }
    }

    context["messages"] = messages;
    //qDebug() << "[trimToolResponses] 共" << totalTools << "条 tool 消息，保留最近"
    //         << maxToolMessages << "条完整，其余截断至" << truncateLimit << "字节";
}
void AiWidget::convertContextImagesToBase64(QJsonObject &context)
{
    if (!context.contains("messages") || !context["messages"].isArray())
        return;

    QJsonArray messages = context["messages"].toArray();
    for (int i = 0; i < messages.size(); ++i) {
        QJsonObject msg = messages[i].toObject();
        if (!msg.contains("content") || !msg["content"].isArray())
            continue;
        QJsonArray content = msg["content"].toArray();
        for (int j = 0; j < content.size(); ++j) {
            QJsonObject item = content[j].toObject();
            if (item["type"].toString() == "image_url" &&
                item.contains("image_url") && item["image_url"].isObject()) {
                QJsonObject imageUrlObj = item["image_url"].toObject();
                QString path = imageUrlObj["url"].toString();
                // 如果是本地路径（非 data: 开头）且文件存在
                if (!path.startsWith("data:") && QFile::exists(path)) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)) {
                        QByteArray fileData = file.readAll();
                        file.close();

                        // 判断是否为 GIF (GIF87a 或 GIF89a)
                        bool isGif = false;
                        if (fileData.size() > 6) {
                            QByteArray header = fileData.left(6);
                            if (header == "GIF87a" || header == "GIF89a") {
                                isGif = true;
                            }
                        }

                        QByteArray imageData;
                        QString mimeType;

                        if (isGif) {
                            // 提取第一帧并转为 PNG
                            QBuffer buffer(&fileData);
                            buffer.open(QIODevice::ReadOnly);
                            QImageReader reader(&buffer);
                            QImage firstFrame = reader.read();
                            if (!firstFrame.isNull()) {
                                QByteArray pngData;
                                QBuffer pngBuffer(&pngData);
                                pngBuffer.open(QIODevice::WriteOnly);
                                if (firstFrame.save(&pngBuffer, "PNG")) {
                                    imageData = pngData;
                                    mimeType = "image/png";
                                }
                            }
                            buffer.close();
                        }

                        // 如果不是 GIF 或转换失败，使用原始数据
                        if (imageData.isEmpty()) {
                            imageData = fileData;
                            // 根据扩展名猜测 MIME
                            if (path.endsWith(".png", Qt::CaseInsensitive))
                                mimeType = "image/png";
                            else if (path.endsWith(".gif", Qt::CaseInsensitive))
                                mimeType = "image/gif"; // 但实际不会走这里，因为上面已处理
                            else if (path.endsWith(".webp", Qt::CaseInsensitive))
                                mimeType = "image/webp";
                            else if (path.endsWith(".bmp", Qt::CaseInsensitive))
                                mimeType = "image/bmp";
                            else
                                mimeType = "image/jpeg"; // 默认
                        }

                        // 编码 base64
                        QString base64 = QString::fromLatin1(imageData.toBase64());
                        imageUrlObj["url"] = "data:" + mimeType + ";base64," + base64;
                        item["image_url"] = imageUrlObj;
                        content[j] = item;
                    }
                }
            }
        }
        msg["content"] = content;
        messages[i] = msg;
    }
    context["messages"] = messages;
}
// ========== 构建基础上下文（从数据库读取） ==========
QJsonObject AiWidget::buildBaseContext(AccountInfo* info,const QString &Gid, const QString& openid,int type,bool bot_admin)
{
    QJsonObject context;
    QString sxw = aidb->get(info->appid+":"+openid);
    if (!sxw.isEmpty()) {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(sxw.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
            context = doc.object();
    }
    context["model"] = info->model;

    QString setting;
    for (const auto &sd : std::as_const(m_globalSettings)) {
        if (sd.name == info->setting) {
            if(info->niren && type==2)
            {


                setting = 拟人人设;
                setting.replace("{角色设定}",sd.content);
                setting.replace("{appid}",info->appid);
                setting.replace("【表情包】",ai_bqbgl->get_bqb(info->appid));
                if(bot_admin)
                    setting.replace("【管理员权限】","撤回别人信息<del:N> N代表数字seq 和回复同一个seqid 看见打广告时处理\n禁言别人<mt:id,时间(秒)> id用<@id>那种短id\n当你看见有这段文本时代表你有管理员权限");
                break;
            }else if(type ==0 || type ==1)
            {
                auto *db = g_botdb [info->appid_int];
                GroupRecord2 gr{};
                db->getGroupInfo(Gid,gr);
                if((gr.bitmap & BIT_Ainiren)==1)
                {
                    setting = 拟人人设;
                    setting.replace("{角色设定}",sd.content);
                    setting.replace("【表情包】",ai_bqbgl->get_bqb(info->appid));
                    if(bot_admin)
                        setting.replace("【管理员权限】","撤回别人信息<del:N> N代表数字seq 和回复同一个seqid 看见打广告时处理\n禁言别人<mt:id,时间(秒)> id用<@id>那种短id\n当你看见有这段文本时代表你有管理员权限");
                    break;
                }

            }
            setting = "[工具使用 你可以随意使用 tool内的函数 你可以有事没事 使用html制图来 表达什么]\n"+sd.content;
            break;
        }
    }

    // 长期日志：只有该账号启用了 write_log 才注入。
    // 没启用的话连读都不读 —— 这份内容是直接进系统提示词的，白给就是白烧上下文。
    if (info->tools.contains(QStringLiteral("write_log"))) {
        const QString aiLog = AiLog::readAll(info->appid_int);
        if (!aiLog.trimmed().isEmpty()) {
            setting += QStringLiteral("\n\n【长期日志】你自己用 write_log 记下的内容"
                                      "（上限 %1 字符，超出会自动丢弃最早的）：\n%2")
                           .arg(AiLog::maxChars())
                           .arg(aiLog.trimmed());
        }
    }

    QJsonArray arr = get_tools(info);
    if (!arr.isEmpty())
        context["tools"] = arr;

    QJsonArray msgs;
    if (context.contains("messages"))
        msgs = context["messages"].toArray();

    if (msgs.isEmpty()) {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = setting;
        msgs.append(systemMsg);
    } else {
        QJsonObject first = msgs[0].toObject();
        if (first["role"].toString() != "system") {
            QJsonObject systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = setting;
            msgs.insert(0, systemMsg);
        } else {
            first["content"] = setting;
            msgs[0] = first;
        }
    }
    context["messages"] = msgs;
    return context;
}

// ========== 入口函数（只做检查，发射信号到主线程） ==========
QPair<int, QString> splitWrappedMsgId(const QString &wrapped); // 定义在 api.cpp
QString AiWidget::Ai_post(AccountInfo *info, const MessageEvent &ev)
{


    if(ev.type==0 && info->enableGroupChat){}
    else if(ev.type==0 &&info->enableGroupPersonal){}
    else if(ev.type==1 && info->enableChannel){}
    else if(ev.type==2 && info->enablePrivateChat){}
    else return QString();
    if(info->e_bai) //白名单
    {

        if(ev.type==0)
        {

            if (!(ev.bitmap & BIT_AI_BAI)){ //群没有ai白名单 检查个人有没有 如果有 就触发
                auto *db = g_botdb[info->appid_int];
                UserRecord rec;
                db->getUserBySeqId(ev.user_int,rec);
                if (!(rec.bitmap & BIT_AI_BAI)) return QString();

            }
        }else if(ev.type==2)
        {
            auto *db = g_botdb[info->appid_int];
            UserRecord rec;
            db->getUserBySeqId(ev.user_int,rec);
            if (!(rec.bitmap & BIT_AI_BAI)) return QString();
        }else{
            return QString();
        }
    }

    for(auto &f : info->fujia)
    {
        if(ev.msg.startsWith(f))
        {
            QString role;
            QString mode = ai_fujia->fujia_jy(f,role);
            if(mode.isEmpty()) return "触发附加ai指令 但是 ui界面与 设置不一致";

            if(ev.log>0) {
                bool ok = false;
                g_logdb [ev.type+1]->setBuffer_250(ev.log,ok); //设置为250 让未处理 回复 不回复
            }
            // 改为异步请求：不再阻塞当前线程干等 60s，回复在回调里直接发出去。
            // ev 值拷贝进 lambda（异步链路跨线程，不能引用栈上内容）。
            const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
            Ai_postAsync(mode, role+ev.msg, 60000,
                [this, ev, appid = info->appid_int, t0](const QString &reply) {
                    if (m_shuttingDown) return;   // 正在析构
                    if (reply.isEmpty()) return;
                    if (!m_botClients.contains(appid)) return;
                    QQBotClient *bot = m_botClients.value(appid);
                    QString text = QString("[Ai|%1ms]").arg(QDateTime::currentMSecsSinceEpoch() - t0);
                    QString rep = reply;
                    bot->send_msgAsync(ev.type, ev.groupId, text, rep, ev.msgId);
                });
            return QString();   // 异步链路负责发回，这里不再返回内容
        }

    }
    if(info->atTrigger && ev.at_you){}
    else if(info->pplx == 1 && ev.msg.contains(info->Ai_nickname)){}
    else if(info->pplx == 2 && ev.msg.startsWith(info->Ai_nickname)){}
    else if(info->niren){}
    else return QString();
    if (ev.msg == "清除记忆") {
        if(ev.member_role<=5){
            QString openid;
            switch (ev.type) {
            case 0: openid = info->enableGroupPersonal ? ev.user : ev.groupId; break;
            case 1: openid = info->enableChannelPersonal ? ev.user : ev.groupId; break;
            case 2: openid = ev.groupId; break;
            default: return "不支持Ai指令  请在 群 私聊 频道 发送本指令";
            }
            aidb->put(info->appid+":"+openid, "{}");
            return "清空记忆完成 如果开启的是拟人将清除拟人上下文 如果对话未完成可能清理失败";
        }
        aidb->put(info->appid+":"+ev.user, "{}");
        return "清空个人上下文完成 如果对话未完成可能清理失败";
    }



    // 模型检查
    int index = -1;
    for (int i = 0; i < modelList.size(); ++i) {
        if (modelList[i].name == info->model) {
            index = i;
            break;
        }
    }
    if (index == -1)
        return "触发AI:" + info->Ai_nickname + " 但是设置的模型【" + info->model + "】 在模型列表不存在";
    if (modelList[index].enabledInterfaceIndices.isEmpty())
        return "触发AI:" + info->Ai_nickname + " 但是设置的模型【" + info->model + "】 未设置接口";

    // 发射信号到主线程处理（确保定时器安全）
    emit newMessageArrived(info, ev,false,false);
    return QString(); // 立即返回
}
void AiWidget::trimContextByMessageCount(QJsonObject &context, int maxMessages)
{
    if (!context.contains("messages") || !context["messages"].isArray())
        return;

    QJsonArray msgs = context["messages"].toArray();
    if (msgs.size() <= 1)
        return;
    QJsonObject systemMsg;
    if (!msgs.isEmpty() && msgs[0].toObject()["role"].toString() == "system") {
        systemMsg = msgs[0].toObject();
    }
    QJsonArray nonSystem;
    for (int i = 0; i < msgs.size(); ++i) {
        if (i == 0 && !systemMsg.isEmpty()) continue;  // 跳过 system
        nonSystem.append(msgs[i]);
    }
    while (nonSystem.size() > maxMessages) {
        nonSystem.removeAt(0);   // 删除最早的一条
    }
    while (!nonSystem.isEmpty() && nonSystem[0].toObject()["role"].toString() != "user") {
        nonSystem.removeAt(0);
    }


    if (!systemMsg.isEmpty())
        nonSystem.insert(0,systemMsg);
    context["messages"] = nonSystem;
}
QString AiWidget::trimContextByMessageCount2(QJsonObject &context, int maxMessages)
{
    QString nonSystem;
    if (!context.contains("messages") || !context["messages"].isArray())
        return nonSystem;

    QJsonArray msgs = context["messages"].toArray();
    if (msgs.size() <= 1)
        return nonSystem;

    for (int i = 0; i < msgs.size(); ++i) {
        QJsonObject obj = msgs[i] .toObject();
        if(obj["role"].toString()!="user") continue;
        auto a1 = obj["content"].toArray();
        auto o2 = a1.at(0);
        nonSystem+=o2["text"].toString()+"\n";
    }


    return nonSystem;
}
QPair<int, QString> splitWrappedMsgId(const QString &wrapped);
// ========== 主线程处理消息（延迟合并） ==========
void AiWidget::onNewMessage(AccountInfo *info,const MessageEvent &ev,bool send,bool notime)
{
    // 计算 openid
    QString openid;
    switch (ev.type) {
    case 0: openid = info->enableGroupPersonal ? ev.user: ev.groupId ; break;
    case 1: openid = info->enableChannelPersonal ? ev.user : ev.groupId ; break;
    case 2: openid = ev.groupId; break;
    default: return;
    }

    QMutexLocker lk(&m_sessionsMutex);   // 线程池线程也会读写 m_sessions

    auto &session = m_sessions[openid];
    if(!notime)
        session.cflx = ev.at_you ? 1:2;
    if (!session.timer) {
        session.sjs = info->触发概率;
        session.timer = new QTimer(this);
        session.timer->setSingleShot(true);
        connect(session.timer, &QTimer::timeout, this, [this, openid]() {
            QMutexLocker lk(&m_sessionsMutex);   // 主线程，但和线程池线程共享 m_sessions
            auto &session = m_sessions[openid];

            auto [index, realMsgId] = splitWrappedMsgId(session.msgId);
            if(index>0) {  //跑这里必定延迟1s 像易语言 JS等就可以 等待返回值 看看有没有触发
                int n = g_logdb [session.type+1]->getBufferStatus(index);
                if(n >= 250) return; //255代表被处理了
                bool ok=false;
                g_logdb [session.type+1]->setBuffer_250(index,ok); //设置为250 让未处理 回复 不回复
            }

            QThreadPool::globalInstance()->start([this,openid]() {
                flushPendingMessages(openid,false);
            });
        });
        if (!session.memory) {
            QString memoryPath = "botdb/memory/" + openid;
            QDir().mkpath(memoryPath);
            session.memory = QSharedPointer<VectorMemory>::create(memoryPath.toStdString(), 384, 100000);
        }
    }
    if(!send)
    {
        PendingMessage pm;
        if(info->enableImageRec) pm = parseImageTagsAndDownload(ev.msg);
        else pm.text = ev.msg;

        QDateTime now = QDateTime::currentDateTime();
        pm.text = QString("[time:%1|id:%2|seq:%3|%4] 说:%5").arg(now.toString("yyyy-MM-dd HH:mm")).arg(ev.user_int).arg(ev.log).arg(ev.nickname,pm.text);
        session.pendingMessages.append(pm);
    }
    //session.baseContext = buildBaseContext(info,ev.groupId, openid,ev.type);
    session.appid = ev.appid;
    session.type = ev.type;
    session.groupId = ev.groupId;
    session.msgId = ev.msgId;
    session.bot_admin = ev.bot_admin;   //后面对话里要判权限（撤回/禁言）
    session.accountInfo = info;
    session.openid = openid;
    if (session.isProcessing) {
        return;
    }
    session.ts++;
    if(session.ts>=info->固定条数 && info->固定条数>0)
    {
        session.ts=0;
    }else if(session.sjs!=0 && session.sjs < QRandomGenerator::global()->bounded(100))//随机数
    {
        session.sjs  +=  info->递增概率;
        return ;
    }
    if(notime){
        QThreadPool::globalInstance()->start([this,openid,send]() {
            flushPendingMessages(openid,send);
        });
        return;
    }
    int delayMs = info->delayReplySeconds * 1000;
    if (delayMs <= 2000) delayMs = 2000;

    session.timer->start(delayMs);
    session.dslx=0;

    //qDebug() << "[AiWidget] 定时器已启动，延迟" << delayMs << "ms，openid:" << openid;
}



void AiWidget::onAsyncReply(const QString &openid, const QString &reply,
                            const QJsonObject &updatedContext,      // mutableContext
                            int oldMsgCount)
{
    QMutexLocker lk(&m_sessionsMutex);   // 全程持有：session 引用会一直用到函数结束

    auto it = m_sessions.find(openid);
    if (it == m_sessions.end())
        return;

    auto &session = *it;
    if (!session.accountInfo) return;
    session.sjs = session.accountInfo->触发概率;
    session.isProcessing = false;
    //session.baseContext 改为下面
    QJsonObject baseContext = buildBaseContext(session.accountInfo,session.groupId, openid,session.type,session.bot_admin);
    if (updatedContext.contains("messages") && updatedContext["messages"].isArray()) {
        QJsonArray newMsgs = updatedContext["messages"].toArray();
        QJsonArray baseMsgs = baseContext["messages"].toArray();//这个代码 主要作用是 区分原文和 用于请求的 上下文 因为上下文包含b64图片 原文没有
        if (newMsgs.size() > oldMsgCount) {

            for (int i = oldMsgCount; i < newMsgs.size(); ++i) {
                baseMsgs.append(newMsgs[i]);
            }
            baseContext["messages"] = baseMsgs;
        }
    }

    // ---- 保存上下文到数据库（使用 baseContext，含本地路径） ----
    QJsonObject savedContext = baseContext;
    if (savedContext.contains("tools"))
        savedContext.remove("tools");
    if (savedContext.contains("messages") && savedContext["messages"].isArray()) {
        QJsonArray msgs = savedContext["messages"].toArray();
        if (!msgs.isEmpty()) {
            QJsonObject first = msgs[0].toObject();
            if (first["role"].toString() == "system") {
                first["content"] = "";
                msgs[0] = first;
                savedContext["messages"] = msgs;
            }
        }
    }
    aidb->put(session.accountInfo->appid+":"+openid, QJsonDocument(savedContext).toJson(QJsonDocument::Compact));
    if(reply.startsWith("[待确认]") && reply.contains("#b:#") && reply.contains("同意"))
    {
        return ;
    }
    session.duihts++;
    if (!session.pendingMessages.isEmpty()) {
        QThreadPool::globalInstance()->start([this,openid]() {
            flushPendingMessages(openid,false);
        });
    }else if(session.dslx==0 && session.accountInfo->nSecondsNoReply>0){
        session.dslx=1;
        PendingMessage pm;
        pm.imagePaths.clear();
        pm.text="[定时器]本条信息为Ai主动信息 用户在"+QString::number(session.accountInfo->nSecondsNoReply)+"秒内没找你对话触发 请无视本条信息 请参考上下文对话";
        session.pendingMessages.append(pm);
        if (session.timer) session.timer->start(session.accountInfo->nSecondsNoReply*1000);
    }else if(session.dslx==1 && session.accountInfo->nMinutesNoReply>0){
        session.dslx=2;
        PendingMessage pm;
        pm.imagePaths.clear();
        pm.text="[定时器]本条信息为Ai主动信息 用户在"+QString::number(session.accountInfo->nSecondsNoReply)+"分钟内没找你对话触发 请无视本条信息 请参考上下文对话";
        session.pendingMessages.append(pm);
        if (session.timer) session.timer->start(session.accountInfo->nMinutesNoReply*60*1000);
    }
    if(!session.accountInfo->xiangliang) return;
    if (session.duihts % 5 == 0 && !reply.isEmpty()) {
        if(session.type!=2) return ;
        if (session.accountInfo->Embed_model.isEmpty()) return;

        QString newMsgs = trimContextByMessageCount2(baseContext, 10);

        QString prompt = R"(
从对话中提取关于用户的**有长期记忆价值的事实**，每条用一句话描述。

**只提取**：
- 用户的个人信息（年龄、职业、城市、家人等）
- 用户的兴趣爱好、喜好、习惯
- 用户的长期目标、计划
- 用户的固定观点、价值观

**不要提取**：
- 对助手的称呼、调侃、抱怨
- 临时的情绪或困惑
- 对对话本身的评价

只返回 JSON 数组，不要其他内容。
对话内容：
)" + newMsgs;

        AccountInfo* acc = session.accountInfo;
        QSharedPointer<VectorMemory> mem = session.memory;   // 按值捕获进 lambda：持有副本，对象一定活到任务结束
        QThreadPool::globalInstance()->start([this, acc, mem, prompt, openid]() {

            QString response = ai_ui->Ai_post(acc->model, prompt, 60000);

            if (response.isEmpty()) return;

            QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
            if (!doc.isArray()) {
                qWarning() << "提取的事实不是JSON数组";
                return;
            }
            QJsonArray facts = doc.array();

            // 3. 查找嵌入模型索引
            QString model = acc->Embed_model;
            int model_index = 0;
            bool ok = false;
            for (model_index = 0; model_index < modelList.size(); ++model_index) {
                if (modelList[model_index].name == model) {
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                qWarning() << "嵌入模型未找到:" << model;
                return;
            }

            // 4. 处理每条提取的事实
            for (const QJsonValue &val : std::as_const(facts)) {
                QString fact = val.toString();
                if (fact.trimmed().isEmpty()) continue;

                // 4.1 生成向量
                QVector<double> vec;
                for (int i : std::as_const(modelList[model_index].enabledInterfaceIndices)) {
                    if (globalInterfaces[i].keys.size() == 0) {
                        vec = getEmbedding(fact, globalInterfaces[i].url, acc->Embed_model, QString());
                    } else {
                        for (const auto &key : std::as_const(globalInterfaces[i].keys)) {
                            //if (!key.enabled) continue;
                            vec = getEmbedding(fact, globalInterfaces[i].url, acc->Embed_model, key.key);
                            if (!vec.isEmpty()) break;
                        }
                    }
                    if (!vec.isEmpty()) break;
                }
                if (vec.isEmpty()) continue;

                // 4.2 去重
                double threshold = 0.85;
                std::vector<float> queryFloatVec(vec.begin(), vec.end());
                auto results = mem->search(queryFloatVec, 1);

                bool isDuplicate = false;
                if (!results.empty()) {
                    double score = results[0].second;
                    if (score <= threshold) {
                        isDuplicate = true;
                        //qDebug() << "跳过重复事实:" << fact << "相似度:" << score;
                    }
                }

                // 4.3 插入
                if (!isDuplicate) {
                    std::vector<float> floatVec(vec.begin(), vec.end());
                    mem->insert(floatVec, fact.toStdString());
                    //qDebug() << "插入新事实:" << fact;
                }
            }
        });
    }
}



//============
QString AiWidget::Ai_post(const QString &model,const QString &msg,int timeoutMs)
{
    int index=-1;
    for (int i =0;i<modelList.size();++i)
    {
        if(modelList[i].name==model)
        {
            index=i;
            break;
        }
    }
    if(index==-1) return "【"+model+"】 在模型列表不存在 请配置模型后试试";
    if(modelList[index].enabledInterfaceIndices.isEmpty()) return "【"+model+"】 未设置接口 请配置接口后试试";
    QJsonObject obj;
    obj["model"]=model;
    appendPendingMessageToContext(obj, parseImageTagsAndDownload(msg)); //下载
    convertContextImagesToBase64(obj);

    if(timeoutMs<=0) timeoutMs=30000;
    if(timeoutMs<=5000) timeoutMs=5000;
    return Ai_posts(MessageEvent(),index,obj,timeoutMs);
}

QString _tools(const QString &code,const QString &args,const MessageEvent &ev,const QString &mode);

QString AiWidget::Ai_qx(AccountInfo *info,const MessageEvent &ev)
{
    if(!g_admin.contains(ev.user)){
        if(info->admin.isEmpty()) return QString();
        if(!info->admin .contains(ev.user)) return QString();
    }
    QString prefix;
    if (ev.msg.startsWith("同意")) {
        prefix = "同意";
    } else if (ev.msg.startsWith("拒绝")) {
        prefix = "拒绝";
    } else {
        return QString(); // 未知指令
    }

    QString userID = ev.msg.mid(prefix.length());
    QJsonObject sxw = buildBaseContext(info,ev.groupId, userID,ev.type,ev.bot_admin);
    if (sxw.contains("messages") && sxw["messages"].isArray()) {
        QJsonArray msgs = sxw["messages"].toArray();
        for (int i = msgs.size() - 1; i >= 0; --i) {
            QJsonValue value = msgs[i];
            if(!value.isObject()) return "解析上下文出现异常 请清除记忆 来解决或者联系管理员？";
            QJsonObject obj = value.toObject();
            QString role = obj["role"].toString();
            if(role != "tool") return "上下文已经变动 本次处理无效";
            if(!obj.contains("pycode")) return "上下文已经变动 本次处理无效";
            QString res="python代码执行被用户拒绝";
            if(prefix == "同意")
            {
                QJsonObject pycode = obj["pycode"].toObject();

                QString args = pycode["arguments"].toString();
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(args.toUtf8(), &error);
                if (error.error != QJsonParseError::NoError) {
                    return "解析Ai 工具函数json失败";
                }
                QJsonObject obj2=doc.object();
                QString code = obj2["p1"].toString();
                res = _tools(code,"{}",ev,sxw["model"].toString());
                if(res.isEmpty()) res = "python执行 成功 未返回数据";
            }
            obj.remove("pycode");
            obj["content"] = res;
            msgs[i] = obj;
            sxw["messages"] = msgs;
            aidb->put(info->appid+":"+userID, QJsonDocument(sxw).toJson(QJsonDocument::Compact));
            emit newMessageArrived(info, ev,true,true);
            return "*"; // 立即返回 让本条指令不执行ai
        }
    }

    return "处理失败 指定用户上下文不存在"; // 立即返回

}

QVector<double> AiWidget::getEmbedding(const QString &text, const QString &url2, const QString &model, const QString &key)
{
    QVector<double> result;

    QJsonObject body;
    body["model"] = model;
    QJsonArray inputs;
    inputs.append(text);
    body["input"] = inputs;  // Ollama 标准字段


    QHash<QString,QString> h;
    h["Authorization"]="Bearer " + key;
    h["Content-Type"]="application/json";
    auto f = NetManager::instance()->post(url2,QJsonDocument(body).toJson(QJsonDocument::Compact),h,30000);
    QByteArray response = f.get();

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull()) return result;
    QJsonObject obj = doc.object();
    QJsonArray vecArr;
    if (obj.contains("embeddings") && obj["embeddings"].isArray()) {
        QJsonArray embeddings = obj["embeddings"].toArray();
        if (!embeddings.isEmpty()) {
            vecArr = embeddings[0].toArray();
        }
    }
    else if (obj.contains("data") && obj["data"].isArray()) {
        QJsonArray data = obj["data"].toArray();
        if (!data.isEmpty()) {
            vecArr = data[0].toObject()["embedding"].toArray();
        }
    }
    if (vecArr.isEmpty()) {
        AppendEventLog("无法解析嵌入向量，响应内容:" + response.left(200),0xff);
        return result;
    }

    result.reserve(vecArr.size());
    for (const QJsonValue &v : std::as_const(vecArr)) {
        result.append(v.toDouble());
    }
    return result;
}


void AiWidget::startHourlyCleanupTimer()
{
    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(60 * 60 * 1000); // 1小时
    connect(m_cleanupTimer, &QTimer::timeout, this, &AiWidget::onCleanupTimer);
    m_cleanupTimer->start();

}

// 线程安全地改写 isProcessing。AI 请求的回调在线程池线程跑，不能直接碰 m_sessions。
void AiWidget::setSessionProcessing(const QString &openid, bool processing)
{
    QMutexLocker lk(&m_sessionsMutex);
    auto it = m_sessions.find(openid);
    if (it != m_sessions.end())
        it.value().isProcessing = processing;
}

// 清理单个会话的资源（调用方需已持有 m_sessionsMutex）
void AiWidget::clearSessionResources(SessionContext &ctx)
{
    if (ctx.timer) {
        ctx.timer->stop();
        delete ctx.timer;
        ctx.timer = nullptr;
    }
    ctx.memory.reset();     // QSharedPointer：还有人在用就等他用完再释放
    ctx.pendingMessages.clear(); // 若元素含指针，需另行释放
}

// 清理所有会话（析构时调用）
void AiWidget::clearAllSessions()
{
    QMutexLocker lk(&m_sessionsMutex);
    for (auto &ctx : m_sessions) {
        clearSessionResources(ctx);
    }
    m_sessions.clear();
}

void AiWidget::onCleanupTimer()
{
    QMutexLocker lk(&m_sessionsMutex);
    QMap<QString, SessionContext>::iterator it = m_sessions.begin();
    while (it != m_sessions.end()) {
        SessionContext &ctx = it.value();
        // 只回收「空闲」会话：正在等 AI 回复、或还有待发消息的一律不动。
        // 原来这里写成 (isProcessing || !pendingMessages.isEmpty()) 是反的 ——
        // 会把正在处理的会话 erase 掉，而线程池里还持有它的 SessionContext&，
        // 继续写就变成写已释放内存（后面的崩就是这么来的）。
        if (!ctx.isProcessing && ctx.pendingMessages.isEmpty()) {
            clearSessionResources(ctx);
            it = m_sessions.erase(it);   // 移除并获取下一个迭代器
        } else {
            ++it;
        }
    }
}

// ============ WebUI 刷新钩子 ============
// 网页上改完 data/model_config.json / data/fujia.json 之后调这两个，
// 让桌面端的「模型配置」「附加模型」两个标签页立刻跟上，不必重启框架。

void AiWidget::webuiReloadModelConfig()
{
    if (!QFile::exists(configFilePath2)) return;
    // 注意：loadFromFile3() 内部只 insertRow、不会先清表，反复调用会把模型行越堆越多，
    // 所以这里自己先把表清空。
    modelListTable->setRowCount(0);
    loadFromFile3();
    刷新模型();
}

void AiWidget::webuiReloadFujia()
{
    if (ai_fujia) ai_fujia->webuiReload();
}