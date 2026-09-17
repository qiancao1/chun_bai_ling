#include "themecolorwidget.h"

#include "themecolors.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QColorDialog>
#include <QMessageBox>

namespace {

const int kColumns = 6;     // 每行几个色块

// 按底色亮度选黑/白文字，保证色块上的名字始终看得清
QColor contrastText(const QColor &bg)
{
    const double lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    return lum > 150.0 ? QColor("#263241") : QColor("#FFFFFF");
}

QString hexUpper(const QString &name)
{
    return name.toUpper();
}

} // namespace

ThemeColorWidget::ThemeColorWidget(QWidget *parent) : QWidget(parent)
{
    setupUI();
}

void ThemeColorWidget::setupUI()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);
    root->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    // ---------- 标题行：标题 + 说明 + 预设 ----------
    QHBoxLayout *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    titleRow->addWidget(new QLabel("界面配色：点击色块修改（立即生效）", this));

    QLabel *hint = new QLabel("仅影响主窗口全局样式", this);
    hint->setStyleSheet("color: #96979B; font-size: 11px;");
    hint->setToolTip("个别自带样式表的页面（账号页、聊天页、插件市场等）不受影响");
    titleRow->addWidget(hint);
    titleRow->addStretch();

    QLabel *presetLabel = new QLabel("预设：", this);
    presetLabel->setStyleSheet("color: #96979B; font-size: 11px;");
    titleRow->addWidget(presetLabel);

    // 预设按钮由 presets() 决定，加一套预设只改 ui/themecolors.cpp 那张表
    for (const ThemePreset &preset : ThemeColors::presets()) {
        QPushButton *btn = new QPushButton(preset.name, this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(QString("一键套用「%1」").arg(preset.name));

        const QString key = preset.key;
        const QString name = preset.name;
        connect(btn, &QPushButton::clicked, this, [this, key, name]() {
            // 「默认配色」不用确认（就是还原）；其它预设会盖掉用户当前调好的颜色，问一句
            if (key != "default"
                && QMessageBox::question(this, "套用预设",
                                         QString("套用「%1」会覆盖当前调好的配色，确定吗？").arg(name),
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;

            ThemeColors::applyPreset(key);
            refreshSwatches();
        });

        titleRow->addWidget(btn);
    }
    root->addLayout(titleRow);

    // ---------- 色块 ----------
    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(4);
    m_grid->setVerticalSpacing(4);

    const QList<ThemeRole> &table = ThemeColors::roles();
    for (int i = 0; i < table.size(); ++i) {
        const ThemeRole &r = table[i];

        QPushButton *btn = new QPushButton(r.name, this);
        btn->setFixedSize(108, 30);
        btn->setCursor(Qt::PointingHandCursor);

        const QString key = r.key;
        connect(btn, &QPushButton::clicked, this, [this, key]() { pickColor(key); });

        m_swatches.insert(key, btn);
        m_grid->addWidget(btn, i / kColumns, i % kColumns);
    }
    m_grid->setColumnStretch(kColumns, 1);   // 末尾留白，色块不被拉宽
    root->addLayout(m_grid);

    refreshSwatches();
}

void ThemeColorWidget::refreshSwatches()
{
    for (const ThemeRole &r : ThemeColors::roles()) {
        QPushButton *btn = m_swatches.value(r.key);
        if (!btn)
            continue;

        const QColor cur = ThemeColors::value(r.key);

        // 色块自带样式（不用全局配色），这样不管用户把界面改成什么样，这里始终能看清
        btn->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: 1px solid #C9C9C9;"
            " border-radius: 6px; font-weight: 600; }"
            "QPushButton:hover { border: 1px solid #8A94A6; }")
            .arg(cur.name(), contrastText(cur).name()));

        btn->setToolTip(QString("%1\n当前：%2\n默认：%3\n\n点击选择颜色")
                            .arg(r.name, hexUpper(cur.name()), hexUpper(r.def)));
    }
}

void ThemeColorWidget::pickColor(const QString &key)
{
    const QColor old = ThemeColors::value(key);
    const QColor picked = QColorDialog::getColor(old, this, "选择颜色");
    if (!picked.isValid())
        return;

    // setValue 内部：落 g_config + saveConfig + 通知主窗口重套样式表
    ThemeColors::setValue(key, picked);
    refreshSwatches();
}
