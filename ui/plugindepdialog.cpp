#include "PluginDepDialog.h"
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProcess>
#include <QDebug>
#include <QCoreApplication>
#include <QStandardPaths>   // 新增：用于查找可执行文件

PluginDepDialog::PluginDepDialog(const QStringList& requires, const QString& pluginName, QWidget *parent)
    : QDialog(parent), m_process(nullptr), m_pluginName(pluginName)
{
    setWindowTitle(QString("安装依赖库 - %1").arg(pluginName));
    resize(600, 500);

    // 查找 python3.14t 的完整路径（避免 QProcess 的 PATH 问题）
    QString pythonCmd = QStandardPaths::findExecutable("python3.14t");
    if (pythonCmd.isEmpty()) {
        QMessageBox::warning(this,"缺少python主程序","没有找到 python3.14t路径 不能下载");
        return ;
    } else {
        m_pythonCmd = pythonCmd;
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *infoLabel = new QLabel(QString("插件“%1”需要以下 Python 库：").arg(pluginName));
    mainLayout->addWidget(infoLabel);

    m_checkList = new QListWidget;
    m_checkList->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString &lib : requires) {
        QListWidgetItem *item = new QListWidgetItem(lib);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        m_checkList->addItem(item);
    }
    mainLayout->addWidget(m_checkList);

    m_outputBrowser = new QTextBrowser;
    m_outputBrowser->setMinimumHeight(200);
    mainLayout->addWidget(m_outputBrowser);

    m_statusLabel = new QLabel("就绪");
    mainLayout->addWidget(m_statusLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_installBtn = new QPushButton("安装选中库");
    m_cancelBtn = new QPushButton("取消");
    btnLayout->addWidget(m_installBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_installBtn, &QPushButton::clicked, this, &PluginDepDialog::onInstallClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void PluginDepDialog::appendOutput(const QString &text)
{
    m_outputBrowser->append(text);
    QCoreApplication::processEvents();
}

void PluginDepDialog::onInstallClicked()
{
    // 收集选中的库
    QStringList selectedLibs;
    for (int i = 0; i < m_checkList->count(); ++i) {
        QListWidgetItem *item = m_checkList->item(i);
        if (item->checkState() == Qt::Checked) {
            selectedLibs << item->text();
        }
    }

    if (selectedLibs.isEmpty()) {
        QMessageBox::information(this, "提示", "没有选中任何库。");
        return;
    }

    m_pendingLibs = selectedLibs;
    m_currentLib.clear();
    m_outputBrowser->clear();
    appendOutput(QString("准备安装 %1 个依赖库...").arg(m_pendingLibs.size()));
    appendOutput(QString("使用 Python 命令: %1").arg(m_pythonCmd));

    m_installBtn->setEnabled(false);
    m_checkList->setEnabled(false);

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PluginDepDialog::onProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &PluginDepDialog::onProcessOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PluginDepDialog::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &PluginDepDialog::onProcessError);

    startNextInstall();
}

void PluginDepDialog::startNextInstall()
{
    if (m_pendingLibs.isEmpty()) {
        appendOutput("\n所有依赖库安装完成！");
        m_statusLabel->setText("安装完成");
        m_installBtn->setEnabled(false);
        m_cancelBtn->setText("关闭");
        m_process->deleteLater();
        m_process = nullptr;
        return;
    }

    m_currentLib = m_pendingLibs.takeFirst();
    appendOutput(QString("\n[开始安装] %1 ...").arg(m_currentLib));
    m_statusLabel->setText(QString("正在安装: %1 (剩余 %2 个)").arg(m_currentLib).arg(m_pendingLibs.size()));

    QStringList args;
    args << "-m" << "pip" << "install" << m_currentLib;
    m_process->start(m_pythonCmd, args);
}

void PluginDepDialog::onProcessOutput()
{
    QString out = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    QString err = QString::fromLocal8Bit(m_process->readAllStandardError());
    if (!out.isEmpty()) appendOutput(out.trimmed());
    if (!err.isEmpty()) appendOutput(err.trimmed());
}

void PluginDepDialog::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        appendOutput(QString("[安装失败] %1 (退出码 %2)").arg(m_currentLib).arg(exitCode));
        m_statusLabel->setText(QString("安装 %1 失败，继续下一个...").arg(m_currentLib));
    } else {
        appendOutput(QString("[安装成功] %1").arg(m_currentLib));
    }
    startNextInstall();
}

void PluginDepDialog::onProcessError()
{
    appendOutput(QString("[错误] %1").arg(m_process->errorString()));
    startNextInstall();
}