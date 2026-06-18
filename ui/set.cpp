#include "set.h"
#include "global.h"
#include <QSettings>
#include <QMessageBox>
#include <QTimer>
#include "accountinfo.h"

void stopImageServer();
bool startImageServer(const QString &ip,quint16 port);
void setUploadTokens(const QStringList &tokens);
QString ffmpegdiv;

set::set(QWidget *parent) : QWidget(parent), m_serverRunning(false)
{
    setupUI();
    loadConfig();           // 加载配置到 UI
    updateControlEnable();  // 根据模式启用/禁用控件

    // 如果需要自动启动（本地模式且 auto_start 为 true）
    bool isLocalMode = m_localRadio->isChecked();
    bool autoStart = g_config["auto_start_local_server"].toBool();
    if (isLocalMode && autoStart) {
        // 延迟一点启动，让界面先显示出来，避免阻塞
        QTimer::singleShot(0, this, [this]() {
            onStartStopClicked();  // 调用启动逻辑
        });
    }
}

set::~set()
{
    // 析构时如果服务器正在运行，可以选择停止或不管（根据需求）
}

void set::setupUI()
{
    QVBoxLayout *mainVLayout = new QVBoxLayout(this);
    mainVLayout->setContentsMargins(4, 4, 4, 4);
    mainVLayout->setSpacing(8);
    mainVLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // 模式行
    QHBoxLayout *remoteLayout1 = new QHBoxLayout;
    remoteLayout1->setAlignment(Qt::AlignLeft);
    QLabel *urlLabel1 = new QLabel(tr("ffmpeg路径"), this);
    m_ffmpegpath = new QLineEdit(this);
    m_ffmpegpath->setPlaceholderText("ffmpeg/");
    m_ffmpegpath->setMinimumWidth(250);

    QPushButton *bt = new QPushButton(tr("确认"), this);
    QLabel *urlLabel3 = new QLabel(tr("日志数量"), this);
    m_日志数量 = new QLineEdit(this);
    m_日志数量->setPlaceholderText("默认5w条 看你电脑配置来");
    m_日志数量->setText(QString::number(g_config["logs"].toInt(50000)));
    m_日志数量->setMinimumWidth(100);
    QPushButton *bt2 = new QPushButton(tr("确认"), this);

    Color_0 = g_config["log_Color0"].toInt(0);
    Color_1 = g_config["log_Color1"].toInt(0);


    QLabel *urlLabel4 = new QLabel(tr("日志颜色"), this);
    QPushButton *colorPreview = new QPushButton(this);
    colorPreview->setFixedSize(30, 30);
    colorPreview->setStyleSheet(QString("background-color: %1; border: 1px solid gray;").arg(QColor(Color_0).name()));
    colorPreview->setCursor(Qt::PointingHandCursor);  // 手型光标

    // 点击预览按钮 → 打开颜色选择器
    connect(colorPreview, &QPushButton::clicked, this, [=]() {
        QColor oldColor = QColor::fromRgb(Color_0);
        QColor newColor = QColorDialog::getColor(oldColor, this, tr("选择日志颜色"));
        if (newColor.isValid()) {
            Color_0 = newColor.rgb();
            colorPreview->setStyleSheet(QString("background-color: %1; border: 1px solid gray;")
                                            .arg(newColor.name()));
            g_config["log_Color0"] = Color_0;
            saveConfig();
        }
    });


    remoteLayout1->addWidget(urlLabel1);
    remoteLayout1->addWidget(m_ffmpegpath);
    remoteLayout1->addWidget(bt);

    remoteLayout1->addWidget(urlLabel3);
    remoteLayout1->addWidget(m_日志数量);
    remoteLayout1->addWidget(bt2);

    remoteLayout1->addWidget(urlLabel4);
    remoteLayout1->addWidget(colorPreview);


    QLabel *urlLabel5 = new QLabel(tr("未处理颜色"), this);
    QPushButton *colorPreview2 = new QPushButton(this);
    colorPreview2->setFixedSize(30, 30);
    colorPreview2->setStyleSheet(QString("background-color: %1; border: 1px solid gray;").arg(QColor(Color_1).name()));
    colorPreview2->setCursor(Qt::PointingHandCursor);  // 手型光标

    // 点击预览按钮 → 打开颜色选择器
    connect(colorPreview2, &QPushButton::clicked, this, [=]() {
        QColor oldColor = QColor::fromRgb(Color_1);
        QColor newColor = QColorDialog::getColor(oldColor, this, tr("选择日志颜色"));
        if (newColor.isValid()) {
            Color_1 = newColor.rgb();
            colorPreview2->setStyleSheet(QString("background-color: %1; border: 1px solid gray;")
                                            .arg(newColor.name()));
            g_config["log_Color1"] = Color_1;
            saveConfig();
        }
    });

    remoteLayout1->addWidget(urlLabel5);
    remoteLayout1->addWidget(colorPreview2);
    // 模式行
    QHBoxLayout *modeLayout = new QHBoxLayout;
    modeLayout->setAlignment(Qt::AlignLeft);
    QLabel *modeLabel = new QLabel(tr("模式："), this);
    m_remoteRadio = new QRadioButton(tr("使用别人的图床"), this);
    m_localRadio  = new QRadioButton(tr("自己启动图床【如果点击切换会停止图床】"), this);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(m_remoteRadio);
    modeLayout->addWidget(m_localRadio);
    modeLayout->addStretch();

    // 远程图床配置行
    QHBoxLayout *remoteLayout = new QHBoxLayout;
    remoteLayout->setAlignment(Qt::AlignLeft);
    QLabel *urlLabel = new QLabel(tr("使用别人图床："), this);
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(tr("https://example.com/api/upload"));
    m_urlEdit->setMinimumWidth(250);
    QLabel *urlLabel2 = new QLabel(tr("Token："), this);
    m_token = new QLineEdit(this);
    m_token->setPlaceholderText(tr("可空..."));


    m_confirmBtn = new QPushButton(tr("确认"), this);
    remoteLayout->addWidget(urlLabel);
    remoteLayout->addWidget(m_urlEdit);
    remoteLayout->addWidget(urlLabel2);
    remoteLayout->addWidget(m_token);

    remoteLayout->addWidget(m_confirmBtn);

    // 本地图床配置行
    QHBoxLayout *localLayout = new QHBoxLayout;
    localLayout->setAlignment(Qt::AlignLeft);
    QLabel *addrLabel = new QLabel(tr("启动本地图床："), this);
    m_addrEdit = new QLineEdit(this);
    m_addrEdit->setPlaceholderText(tr("这里只需要输入【公网ip】或者域名 "));
    m_addrEdit->setMinimumWidth(150);
    QLabel *portLabel = new QLabel(tr("端口："), this);
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_startStopBtn = new QPushButton(tr("启动"), this);
    localLayout->addWidget(addrLabel);
    localLayout->addWidget(m_addrEdit);
    localLayout->addWidget(portLabel);
    localLayout->addWidget(m_portSpin);
    localLayout->addWidget(m_startStopBtn);
    mainVLayout->addLayout(remoteLayout1);
    mainVLayout->addLayout(modeLayout);
    mainVLayout->addLayout(remoteLayout);
    mainVLayout->addLayout(localLayout);
    // ----- IP 白名单配置区域 -----
    QLabel *tableLabel = new QLabel(tr("Token 管理：启用/禁用、上传次数统计"), this);

    m_tokenTable = new QTableWidget(this);
    m_tokenTable->setColumnCount(3);
    m_tokenTable->setHorizontalHeaderLabels({tr("启用(备注)"), tr("Token"), tr("上传次数")});
    m_tokenTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tokenTable->setAlternatingRowColors(true);
    m_tokenTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tokenTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tokenTable->setMaximumHeight(200);

    QString style = R"(
    QTableWidget::item:selected {
        background-color: #E3B0B9;   /* 选中行背景色（深蓝色） */
        color: white;                 /* 选中行文字颜色 */
    }
    QTableWidget::item:hover {
        background-color: #e0e7ff;   /* 鼠标悬停行背景色（浅蓝色） */
        color: black;                /* 悬停行文字颜色 */
    }
    )";

    // 应用到表格（或整个应用）
    m_tokenTable->setStyleSheet(style);
    // 添加/删除行按钮
    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_addTokenBtn = new QPushButton(tr("添加 Token"), this);
    m_delTokenBtn = new QPushButton(tr("删除选中"), this);
    m_saveTokenBtn = new QPushButton(tr("保存 Token 设置"), this);
    btnLayout->addWidget(m_addTokenBtn);
    btnLayout->addWidget(m_delTokenBtn);

    btnLayout->addWidget(m_saveTokenBtn);

    mainVLayout->addWidget(tableLabel);
    mainVLayout->addWidget(m_tokenTable);
    mainVLayout->addLayout(btnLayout);
    mainVLayout->addStretch();

    // 信号槽连接（新增）
    connect(bt, &QPushButton::clicked, [this](){
        ffmpegdiv = m_ffmpegpath->text();
        g_config["ffmpeg"]= ffmpegdiv;
        saveConfig();
    });
    connect(bt2, &QPushButton::clicked, [this](){
        int configCapacity= m_日志数量->text().toInt();
        if(configCapacity<1000 && configCapacity>0)
        {
            m_日志数量->setText("1000");
            configCapacity=1000;
        }
        g_config["logs"]= configCapacity;
        saveConfig();
        if(configCapacity<=0) configCapacity=1000;
        for (int i = 0; i < 5; ++i) {
            m_logStore[i].setCapacity(configCapacity);
        }
        QMessageBox::warning(this,"修改日志数量","修改完成 0不记录日志 同时群聊会话不可用 日志最低记录数量1k防止异常 每次修改都会存在日志重新来 需要重启才能看见内存变化");
    });
    connect(m_addTokenBtn, &QPushButton::clicked, this, &set::onAddTokenRow);
    connect(m_delTokenBtn, &QPushButton::clicked, this, &set::onDeleteTokenRow);
    connect(m_saveTokenBtn, &QPushButton::clicked, this, &set::onWhitelistChanged);  // 复用原名槽

    connect(m_remoteRadio, &QRadioButton::toggled, this, &set::onModeToggled);
    connect(m_localRadio,  &QRadioButton::toggled, this, &set::onModeToggled);
    connect(m_confirmBtn, &QPushButton::clicked, [this](){ saveRemoteConfig();});
    connect(m_startStopBtn, &QPushButton::clicked, this, &set::onStartStopClicked);
    connect(m_addrEdit, &QLineEdit::textChanged, this, &set::onLocalIpChanged);
    connect(m_portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &set::onLocalPortChanged);

}

void set::onAddTokenRow()
{
    int row = m_tokenTable->rowCount();
    m_tokenTable->insertRow(row);

    QTableWidgetItem *checkItem = new QTableWidgetItem();
    checkItem->setCheckState(Qt::Unchecked);
    m_tokenTable->setItem(row, 0, checkItem);

    QTableWidgetItem *tokenItem = new QTableWidgetItem("新Token");
    tokenItem->setFlags(tokenItem->flags() | Qt::ItemIsEditable);   // 可编辑
    m_tokenTable->setItem(row, 1, tokenItem);

    QTableWidgetItem *countItem = new QTableWidgetItem("0");
    countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);  // 只读
    m_tokenTable->setItem(row, 2, countItem);

    m_tokenTable->scrollToBottom();
}

void set::onDeleteTokenRow()
{
    int curRow = m_tokenTable->currentRow();
    if (curRow >= 0) {
        m_tokenTable->removeRow(curRow);
    } else {
        QMessageBox::information(this, tr("提示"), tr("请先选中要删除的行"));
    }
}
void set::onTokenItemChanged(QTableWidgetItem *item)
{
    // 如果修改的是上传次数列（第3列），立即恢复原值并提示
    if (item->column() == 2) {
        // 获取原来的值
        int row = item->row();
        QTableWidgetItem *oldCountItem = m_tokenTable->item(row, 2);
        if (oldCountItem) {
            QString original = oldCountItem->text();
            item->setText(original);
        }
        QMessageBox::warning(this, tr("只读"), tr("上传次数不能手动修改，将由系统自动统计。"));
    }
}
void set::onWhitelistChanged()
{
    QJsonArray tokenArray;
    QStringList enabledTokens;

    int rows = m_tokenTable->rowCount();
    for (int i = 0; i < rows; ++i) {
        QTableWidgetItem *checkItem = m_tokenTable->item(i, 0);
        QTableWidgetItem *tokenItem = m_tokenTable->item(i, 1);
        QTableWidgetItem *countItem = m_tokenTable->item(i, 2);

        if (!tokenItem) continue;
        QString token = tokenItem->text().trimmed();
        if (token.isEmpty()) continue;

        bool enabled = (checkItem && checkItem->checkState() == Qt::Checked);
        int uploadCount = countItem ? countItem->text().toInt() : 0;

        QJsonObject obj;
        QString text;
        if(checkItem) text = checkItem->text();
        if(!text.isEmpty())
            obj["remark"] = text;

        QByteArray key = MachineKey::generateKey(text);
        obj["token"] = MachineKey::encrypt(token, key);

        obj["enabled"] = enabled;
        obj["uploadCount"] = uploadCount;
        tokenArray.append(obj);

        if (enabled) {
            enabledTokens.append(token);
        }
    }

    g_config["token_table_data"] = tokenArray;
    saveConfig();

    setUploadTokens(enabledTokens);

    QMessageBox::information(this, tr("保存成功"), tr("Token 白名单已更新"));
}


void set::loadConfig()
{
    // 读取模式
    bool useLocal = g_config["image_server_mode_local"].toBool();
    if (useLocal) {
        m_localRadio->setChecked(true);
    } else {
        m_remoteRadio->setChecked(true);
        远程服务器=true;
    }
    // 读取远程地址
    ffmpegdiv = g_config["ffmpeg"].toString();
    if(ffmpegdiv.isEmpty())
        ffmpegdiv="ffmpeg/";
    m_ffmpegpath->setText(ffmpegdiv);
    远程链接 = g_config["image_server_url"].toString();
    m_urlEdit->setText(远程链接);
    远程token=g_config["image_server_token"].toString();
    m_token->setText(远程token);
    QString localIp = g_config["local_server_ip"].toString();
    int localPort = g_config["local_server_port"].toInt();
    m_addrEdit->setText(localIp);
    m_portSpin->setValue(localPort);
    QJsonArray tokenArray = g_config["token_table_data"].toArray();
    m_tokenTable->setRowCount(0);
        QStringList enabledTokens;
    for (const QJsonValue &val : std::as_const(tokenArray)) {
        QJsonObject obj = val.toObject();
        QString remark = obj["remark"].toString();
        QString token = obj["token"].toString();
        QByteArray key = MachineKey::generateKey(remark);
        token = MachineKey::decrypt(token, key);
        bool enabled = obj["enabled"].toBool(false);
        if (enabled) enabledTokens.append(token);
        int uploadCount = obj["uploadCount"].toInt(0);
        int row = m_tokenTable->rowCount();
        m_tokenTable->insertRow(row);
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        checkItem->setText(remark);
        m_tokenTable->setItem(row, 0, checkItem);

        QTableWidgetItem *tokenItem = new QTableWidgetItem(token);
        tokenItem->setFlags(tokenItem->flags() | Qt::ItemIsEditable);
        m_tokenTable->setItem(row, 1, tokenItem);

        QTableWidgetItem *countItem = new QTableWidgetItem(QString::number(uploadCount));
        countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);
        m_tokenTable->setItem(row, 2, countItem);
    }
    setUploadTokens(enabledTokens);
}
void set::incrementTokenUsage(const QString &token)
{
    // 查找表格中匹配的 Token（忽略大小写敏感？按需）
    for (int i = 0; i < m_tokenTable->rowCount(); ++i) {
        QTableWidgetItem *tokenItem = m_tokenTable->item(i, 1);
        if (tokenItem && tokenItem->text() == token) {
            QTableWidgetItem *countItem = m_tokenTable->item(i, 2);
            if (countItem) {
                int newCount = countItem->text().toInt() + 1;
                countItem->setText(QString::number(newCount));
                // 立即保存到配置（可选，也可在退出或保存时统一保存）
                // 但为保证数据不丢失，可以调用保存函数
                // onWhitelistChanged();  // 会保存全部数据并更新白名单
            }
            break;
        }
    }
}
void set::saveModeConfig()
{
    bool useLocal = m_localRadio->isChecked();
    g_config["image_server_mode_local"]= useLocal;
    saveConfig();
}

void set::saveRemoteConfig()
{
    QString url = m_urlEdit->text().trimmed();
    if (!url.isEmpty()) {
        g_config["image_server_url"]= url;
        g_config["image_server_token"]=  m_token->text().trimmed();
        saveConfig();
    }
    // 可选：提示保存成功
}

void set::saveLocalConfig()
{
    g_config["local_server_ip"]= m_addrEdit->text().trimmed();
    g_config["local_server_port"]= m_portSpin->value();
    saveConfig();
}

void set::saveAutoStartFlag(bool autoStart)
{
    g_config ["auto_start_local_server"]= autoStart;
    saveConfig();
}
void set::updateControlEnable()
{
    bool remoteMode = m_remoteRadio->isChecked();
    m_urlEdit->setEnabled(remoteMode);
    m_token->setEnabled(remoteMode);
    m_confirmBtn->setEnabled(remoteMode);
    m_addrEdit->setEnabled(!remoteMode);
    m_portSpin->setEnabled(!remoteMode);
    m_startStopBtn->setEnabled(!remoteMode);
    m_tokenTable->setEnabled(!remoteMode);
    m_addTokenBtn->setEnabled(!remoteMode);
    m_delTokenBtn->setEnabled(!remoteMode);
    m_saveTokenBtn->setEnabled(!remoteMode);
}

void set::stopLocalServerIfRunning()
{
    if (m_serverRunning) {
        stopImageServer();
        m_serverRunning = false;
        m_startStopBtn->setText(tr("启动"));
    }
}

void set::onModeToggled(bool checked)
{
    if (!checked) return;  // 只处理被选中的那个
    saveModeConfig();
    远程服务器=false;
    if (m_remoteRadio->isChecked()) {
        stopLocalServerIfRunning();
        远程服务器=true;
        saveAutoStartFlag(false);  // 远程模式下不应该自动启动
    }
    updateControlEnable();
}


void set::onLocalIpChanged(const QString &ip)
{
    Q_UNUSED(ip)
    saveLocalConfig();
}

void set::onLocalPortChanged(int port)
{
    Q_UNUSED(port)
    saveLocalConfig();
}


void set::onStartStopClicked()
{
    if (!m_localRadio->isChecked())
        return;

    if (!m_serverRunning) {
        // 尝试启动
        QString ip = m_addrEdit->text().trimmed();
        quint16 port = m_portSpin->value();
        if (ip.isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("服务器地址不能为空"));
            return;
        }
        bool ok = startImageServer(ip, port);
        if (ok) {
            m_serverRunning = true;
            m_startStopBtn->setText(tr("停止"));
            saveAutoStartFlag(true);
        } else {
            AppendEventLog("图床服务器启动失败，请检查IP/端口是否可用。下次程序启动将自动重试。" ,0xff);
        }
    } else {
        // 停止服务器
        stopImageServer();
        m_serverRunning = false;
        m_startStopBtn->setText(tr("启动"));
        saveAutoStartFlag(false);
    }
}



