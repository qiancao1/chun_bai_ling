// forbiddenwordpage.cpp


#include "forbiddenwordpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>


// ---------- 默认存储路径 ----------
QString ForbiddenWordPage::defaultFilePath() {

    return  "data/forbidden_words.txt";
}

void ForbiddenWordPage::loadFromDefaultFile() {
    QString path = defaultFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {


        refreshTable();
        buildAutomaton();
        saveToDefaultFile();   // 保存示例到文件
        return;
    }
    QTextStream in(&file);
    QStringList words;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty())
            words << line;
    }
    file.close();
    if (!words.isEmpty())
        m_forbiddenWords = words;


    refreshTable();
    buildAutomaton();
}

void ForbiddenWordPage::saveToDefaultFile() {
    QStringList nonEmpty;
    for (const QString &w : std::as_const(m_forbiddenWords)) {
        if (!w.trimmed().isEmpty())
            nonEmpty << w.trimmed();
    }
    QString path = defaultFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {

        return;
    }
    QTextStream out(&file);
    for (const QString &w : std::as_const(nonEmpty))
        out << w << "\n";
    file.close();
}

// ---------- 构造与UI ----------
ForbiddenWordPage::ForbiddenWordPage(QWidget *parent)
    : QWidget(parent), m_ac(nullptr), m_updating(false) {
    setupUI();
    loadFromDefaultFile();   // 运行时加载已有数据
}

ForbiddenWordPage::~ForbiddenWordPage() {
    delete m_ac;
}

void ForbiddenWordPage::setupUI() {
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(COLUMN_COUNT);
    QStringList headers;
    for (int i = 0; i < COLUMN_COUNT; ++i)
        headers << QString("违禁词%1").arg(i+1);
    m_tableWidget->setHorizontalHeaderLabels(headers);
    m_tableWidget->horizontalHeader()->setStretchLastSection(true);
    m_tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectItems);

    m_addBtn = new QPushButton("添加行", this);
    m_deleteBtn = new QPushButton("删除行", this);
    m_saveBtn = new QPushButton("导出到文件", this);
    m_importBtn = new QPushButton("批量导入", this);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_deleteBtn);
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_importBtn);
    btnLayout->addStretch();

    QGroupBox *testGroup = new QGroupBox("过滤测试", this);
    QLabel *inputLabel = new QLabel("输入文本:", testGroup);
    m_testInputEdit = new QTextEdit(testGroup);
    m_testInputEdit->setMaximumHeight(100);
    QPushButton *testBtn = new QPushButton("过滤", testGroup);
    QLabel *resultLabel = new QLabel("过滤结果:", testGroup);
    m_testResultLabel = new QLabel(testGroup);
    m_testResultLabel->setWordWrap(true);
    m_testResultLabel->setFrameStyle(QFrame::Box | QFrame::Sunken);
    QVBoxLayout *testLayout = new QVBoxLayout(testGroup);
    testLayout->addWidget(inputLabel);
    testLayout->addWidget(m_testInputEdit);
    testLayout->addWidget(testBtn);
    testLayout->addWidget(resultLabel);
    testLayout->addWidget(m_testResultLabel);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_tableWidget);
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(testGroup);

    connect(m_addBtn, &QPushButton::clicked, this, &ForbiddenWordPage::onAddRow);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ForbiddenWordPage::onDeleteRow);
    connect(m_saveBtn, &QPushButton::clicked, this, &ForbiddenWordPage::onSave);
    connect(m_importBtn, &QPushButton::clicked, this, &ForbiddenWordPage::onBatchImport);
    connect(testBtn, &QPushButton::clicked, this, &ForbiddenWordPage::onTestFilter);
    connect(m_tableWidget, &QTableWidget::cellChanged, this, &ForbiddenWordPage::onTableDataChanged);
}

void ForbiddenWordPage::refreshTable() {
    if (m_updating) return;
    m_updating = true;
    m_tableWidget->blockSignals(true);

    int wordCount = m_forbiddenWords.size();
    int rowCount = (wordCount + COLUMN_COUNT - 1) / COLUMN_COUNT;
    if (rowCount == 0) rowCount = 1;
    m_tableWidget->setRowCount(rowCount);

    for (int row = 0; row < rowCount; ++row) {
        for (int col = 0; col < COLUMN_COUNT; ++col) {
            int idx = row * COLUMN_COUNT + col;
            QString text = (idx < wordCount) ? m_forbiddenWords[idx] : QString();
            QTableWidgetItem *item = m_tableWidget->item(row, col);
            if (!item) {
                item = new QTableWidgetItem(text);
                m_tableWidget->setItem(row, col, item);
            } else {
                item->setText(text);
            }
        }
    }

    m_tableWidget->blockSignals(false);
    m_updating = false;
}

void ForbiddenWordPage::onTableDataChanged() {
    if (m_updating) return;
    QStringList newList;
    int rows = m_tableWidget->rowCount();
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < COLUMN_COUNT; ++c) {
            QTableWidgetItem *item = m_tableWidget->item(r, c);
            if (item && !item->text().trimmed().isEmpty()) {
                newList.append(item->text().trimmed());
            }
        }
    }
    // 手动比较，避免 QList::operator !=
    if (newList.size() != m_forbiddenWords.size()) {
        m_forbiddenWords = newList;
        buildAutomaton();
        saveToDefaultFile();
    } else {
        bool same = true;
        for (int i = 0; i < newList.size(); ++i) {
            if (newList[i] != m_forbiddenWords[i]) {
                same = false;
                break;
            }
        }
        if (!same) {
            m_forbiddenWords = newList;
            buildAutomaton();
            saveToDefaultFile();
        }
    }
}

void ForbiddenWordPage::onAddRow() {
    for (int i = 0; i < COLUMN_COUNT; ++i)
        m_forbiddenWords.append(QString());
    refreshTable();
    buildAutomaton();
    saveToDefaultFile();
    int newRow = m_tableWidget->rowCount() - 1;
    if (newRow >= 0) m_tableWidget->scrollToBottom();
}

void ForbiddenWordPage::onDeleteRow() {
    int row = m_tableWidget->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要删除的行");
        return;
    }
    int startIdx = row * COLUMN_COUNT;
    int endIdx = startIdx + COLUMN_COUNT;
    if (startIdx >= m_forbiddenWords.size()) {
        QMessageBox::warning(this, "警告", "所选行没有有效内容");
        return;
    }
    for (int i = endIdx - 1; i >= startIdx; --i) {
        if (i < m_forbiddenWords.size())
            m_forbiddenWords.removeAt(i);
    }
    refreshTable();
    buildAutomaton();
    saveToDefaultFile();
}

void ForbiddenWordPage::onSave() {
    QStringList nonEmpty;
    for (const QString &w : std::as_const(m_forbiddenWords)) {
        if (!w.trimmed().isEmpty())
            nonEmpty << w.trimmed();
    }
    if (nonEmpty.isEmpty()) {
        QMessageBox::warning(this, "警告", "没有可保存的违禁词");
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(this, "导出违禁词列表",
                                                    "forbidden_words.txt",
                                                    "文本文件 (*.txt)");
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法保存文件");
        return;
    }
    QTextStream out(&file);
    for (const QString &w : std::as_const(nonEmpty))
        out << w << "\n";
    file.close();
    QMessageBox::information(this, "成功", QString("已导出 %1 个违禁词").arg(nonEmpty.size()));
}

void ForbiddenWordPage::onBatchImport() {
    QString fileName = QFileDialog::getOpenFileName(this, "导入违禁词列表", "",
                                                    "文本文件 (*.txt)");
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法打开文件");
        return;
    }
    QTextStream in(&file);
    QStringList newWords;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty())
            newWords << line;
    }
    file.close();
    if (newWords.isEmpty()) {
        QMessageBox::information(this, "提示", "文件中没有有效违禁词");
        return;
    }
    m_forbiddenWords.append(newWords);
    refreshTable();
    buildAutomaton();
    saveToDefaultFile();
    QMessageBox::information(this, "成功", QString("已导入 %1 个违禁词").arg(newWords.size()));
}

void ForbiddenWordPage::onTestFilter() {
    QString input = m_testInputEdit->toPlainText();
    QString filtered = filterText(input);
    m_testResultLabel->setText(filtered);
}

// ---------- AC 自动机与过滤核心 ----------
void ForbiddenWordPage::buildAutomaton() {
    if (m_ac) delete m_ac;
    m_ac = new ForbiddenWordAhoCorasick;
    for (int i = 0; i < m_forbiddenWords.size(); ++i) {
        QString word = m_forbiddenWords[i].trimmed();
        if (!word.isEmpty())
            m_ac->insert(word, i);
    }
    m_ac->build();
}

QVector<QPair<int,int>> ForbiddenWordPage::getMatchIntervals(const QString &text) const {
    if (!m_ac) return {};
    QVector<QPair<int,int>> intervals;
    int state = 0;
    for (int pos = 0; pos < text.length(); ++pos) {
        QChar ch = text[pos];
        while (state != 0 && !m_ac->nodes[state].next.contains(ch))
            state = m_ac->nodes[state].fail;
        if (m_ac->nodes[state].next.contains(ch))
            state = m_ac->nodes[state].next[ch];
        for (int idx : std::as_const(m_ac->nodes[state].output)) {
            QString pattern = m_forbiddenWords[idx].trimmed();
            if (pattern.isEmpty()) continue;
            int len = pattern.length();
            intervals.append({pos - len + 1, pos + 1});
        }
    }
    std::sort(intervals.begin(), intervals.end());
    QVector<QPair<int,int>> merged;
    for (const auto &inv : intervals) {
        if (merged.isEmpty() || inv.first > merged.last().second)
            merged.append(inv);
        else
            merged.last().second = qMax(merged.last().second, inv.second);
    }
    return merged;
}

QString ForbiddenWordPage::filterText(const QString &input) const {
    if (m_forbiddenWords.isEmpty()) return input;
    auto intervals = getMatchIntervals(input);
    if (intervals.isEmpty()) return input;

    QString result;
    int lastEnd = input.length();
    for (int i = intervals.size() - 1; i >= 0; --i) {
        int start = intervals[i].first;
        int end = intervals[i].second;
        result.prepend(input.mid(end, lastEnd - end));
        result.prepend("....");
        lastEnd = start;
    }
    result.prepend(input.left(lastEnd));
    return result;
}