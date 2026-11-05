#include "themecolorwidget.h"

#include "themecolors.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QColorDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStyle>

#include "global.h"

namespace {

// 每行 8 个色块（用户指定）。25 项 → 4 行，最后一行只有 1 个；
// 卡片内宽约 990px，8×108 + 7×4 = 892 放得下。
const int kColumns = 8;

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

    // 标题用和基础页卡片一致的样式（objectName 由 ui/set.cpp 的页面样式表定义）
    QLabel *title = new QLabel("界面配色", this);
    title->setObjectName("setCardTitle");
    titleRow->addWidget(title);

    QLabel *hint = new QLabel("点击色块修改，立即生效", this);
    hint->setObjectName("setCardHint");
    hint->setToolTip("仅影响主窗口全局样式；个别自带样式表的页面（账号页、聊天页、插件市场等）不受影响");
    titleRow->addWidget(hint);
    titleRow->addStretch();

    QLabel *presetLabel = new QLabel("预设：", this);
    presetLabel->setObjectName("setCardHint");
    titleRow->addWidget(presetLabel);

    // 预设按钮由 presets() 决定，加一套预设只改 ui/themecolors.cpp 那张表
    for (const ThemePreset &preset : ThemeColors::presets()) {
        QPushButton *btn = new QPushButton(preset.name, this);
        btn->setObjectName("presetChip");          // 样式在 ui/set.cpp 的页面样式表里
        btn->setProperty("presetKey", preset.key); // 用来对比「当前生效的是哪套」
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

        m_presetButtons.append(btn);
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

        // 左边一个圆角色点，右边是角色名。
        // 以前是「整块底色填当前色 + 按亮度选黑/白字」—— 主文字色、次要文字色这类
        // 深色项上字色经常和底色撞成一片，标签名根本读不出来。
        QPixmap dot(28, 28);
        dot.fill(Qt::transparent);
        {
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(0, 0, 0, 45), 1.2));    // 近白色也有边界
            p.setBrush(cur);
            p.drawRoundedRect(QRectF(2.5, 2.5, 23.0, 23.0), 7, 7);
        }
        btn->setIcon(QIcon(dot));
        btn->setIconSize(QSize(14, 14));

        // 色卡按钮跟着主题走：底色取「控件悬停底」、字色取主文字、描边取边框色。
        // 以前这里硬编码白底深字 —— 浅色主题下没问题，切到黑色主题就变成一片白块，
        // 和深色卡片撞得很突兀。现在三色都从 ThemeColors 取，任意主题都自适应。
        const QString bg     = ThemeColors::value("hover_bg").name();
        const QString fg     = ThemeColors::value("text_main").name();
        const QString border = ThemeColors::value("border").name();
        const QString hoverB = ThemeColors::value("accent_soft").name();
        const QString accent = ThemeColors::value("accent").name();
        const QString pressB = ThemeColors::value("btn_pressed").name();

        btn->setStyleSheet(QString(
            "QPushButton {"
            "   background: %1;"
            "   color: %2;"
            "   border: 1px solid %3;"
            "   border-radius: 6px;"
            "   font-weight: 600;"
            "   font-size: 12px;"
            "   text-align: left;"
            "   padding-left: 8px;"
            "}"
            "QPushButton:hover {"
            "   background: %4;"
            "   border: 1px solid %5;"
            "   color: %5;"
            "}"
            "QPushButton:pressed { background: %6; }")
            .arg(bg, fg, border, hoverB, accent, pressB));

        btn->setToolTip(QString("%1\n当前：%2\n默认：%3\n\n点击选择颜色")
                            .arg(r.name, hexUpper(cur.name()), hexUpper(r.def)));
    }

    refreshPresetState();
}

// 预设按钮里把「当前生效的那套」标成实心橙底（走 ui/set.cpp 里的
// QPushButton#presetChip[active="true"]），其余保持普通按钮样式。
// 老配置里没有 theme_preset 这个键，一律当默认配色。
void ThemeColorWidget::refreshPresetState()
{
    QString cur = g_config.value("theme_preset").toString();
    if (cur.isEmpty())
        cur = QStringLiteral("default");

    for (QPushButton *btn : m_presetButtons) {
        if (!btn)
            continue;

        const bool active = (btn->property("presetKey").toString() == cur);
        if (btn->property("active").toBool() == active)
            continue;

        btn->setProperty("active", active);
        btn->style()->unpolish(btn);   // 属性选择器要手动重新 polish 才生效
        btn->style()->polish(btn);
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
