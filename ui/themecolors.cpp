#include "themecolors.h"

#include "global.h"

#include <QObject>
#include <QPair>
#include <QWidget>
#include <utility>

namespace {

// 当前生效的预设 key（存 g_config）。作用只有一个：以后新增配色项时，
// 让深色主题下不会突然冒出一个浅色的新控件。
const char *kPresetKeyName = "theme_preset";

// 自带样式表的页面：控件 -> 它的「基础样式表」（颜色还是默认色的那一版）。
// 主窗口的样式表是全局的，但页面自己 setStyleSheet 会盖住它，
// 所以页面把基础样式表登记在这里，换配色时统一重套。
QHash<QWidget *, QString> &pageSheets()
{
    static QHash<QWidget *, QString> sheets;
    return sheets;
}

// 换配色后把登记过的页面样式表全部重套一遍
void refreshPageSheets()
{
    const QHash<QWidget *, QString> snapshot = pageSheets();   // 迭代中可能被改，先拷一份
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        if (it.key())
            it.key()->setStyleSheet(ThemeColors::apply(it.value()));
    }
}

// 所有可调项。加/删一项只要动这张表，界面（ThemeColorWidget）会自动跟着变。
//
// ⚠️ ThemeSeed.text 必须和样式表里的原文逐字一致（含 "background: " 这样的属性前缀
//    和空格）；text 里必须真的含有 color 那串。
//    同一个角色可以有多条 seed —— 主窗口样式表 + 各页面自己的样式表（首页 / 账号 /
//    日志 / 聊天 / 插件市场 / 沙箱）写的颜色不完全一样，都列上才能全覆盖。
//
// ⚠️⚠️ 千万别加「是另一条 seed 前缀」的片段（例如同时加 "#ccc" 和 "#cccccc"）：
//    apply() 是纯文本替换，短的会把长的截断成 "#XXXXccc" 这种垃圾。同理只加
//    `background: #FFF` 这种短码前，先确认没有 `background: #FFFFFFF…`。当前表里无冲突。
//
// ⚠️ 文字色只加「深色文字」那一类（#17202A / #344054 / #8A94A6 …）。
//    `color: #FFFFFF` 这种是「彩色按钮上的白字」，改成主题色会变成同色底同色字，
//    所以一律不给它 seed。
const QList<ThemeRole> &roleTable()
{
    static const QList<ThemeRole> table = {
        // key              界面名          默认色        样式表里用到它的地方
        { "window_bg",     "窗口背景",     "#FFF8EF", { { "background: #FFF8EF", "#FFF8EF" } } },
        { "content_bg",    "内容区背景",   "#F7EFE5", { { "background: #F7EFE5", "#F7EFE5" } } },
        { "sidebar_bg",    "侧栏背景",     "#FEFEFC", { { "background: #FEFEFC", "#FEFEFC" } } },
        { "card_bg",       "卡片底色",     "#FFFFFF", { { "background: #FFFFFF", "#FFFFFF" },
                                                       { "background: #FFF9F2", "#FFF9F2" } } },
        { "input_bg",      "输入框底色",   "#FEFEFE", { { "background: #FeFeFe", "#FeFeFe" },
                                                       { "background: #FFFCF8", "#FFFCF8" },
                                                       { "background: #F9F9F9", "#F9F9F9" },
                                                       { "background-color: #f8f8f8", "#f8f8f8" },
                                                       { "alternate-background-color: #FFFCF8", "#FFFCF8" } } },
        { "combo_bg",      "下拉框底色",   "#FFFFFF", { { "background: white", "white" },
                                                       { "background-color: white", "white" } } },
        { "combo_arrow_bg","下拉按钮底",   "#F0F0F0", { { "background: #f0f0f0", "#f0f0f0" },
                                                       { "background: #e0e0e0", "#e0e0e0" },
                                                       { "background-color: #e0e0e0", "#e0e0e0" },
                                                       { "background-color: #c0c0c0", "#c0c0c0" } } },
        { "accent",        "主强调色",     "#FF7F32", { { "color: #FF7F32", "#FF7F32" },
                                                       { "background: #FF7F32", "#FF7F32" },
                                                       { "background-color: #FF7F32", "#FF7F32" },
                                                       { "border: 1px solid #FF7F32", "#FF7F32" } } },
        { "accent_hover",  "强调悬停色",   "#FF914D", { { "color: #FF914D", "#FF914D" },
                                                       { "background: #FF914D", "#FF914D" },
                                                       { "background-color: #FF914D", "#FF914D" },
                                                       { "border-color: #FF914D", "#FF914D" } } },
        { "accent_soft",   "强调浅底",     "#FFF0DE", { { "background: #FFF0DE", "#FFF0DE" } } },
        { "soft_hover",    "浅底悬停",     "#FFF6EA", { { "background: #FFF6EA", "#FFF6EA" } } },
        { "btn_hover",     "按钮悬停",     "#FFE5C8", { { "background: #FFE5C8", "#FFE5C8" } } },
        { "btn_pressed",   "按钮按下",     "#FFD7A8", { { "background: #FFD7A8", "#FFD7A8" } } },
        // 「控件悬停底」也兼收各页面那些没有语义的中性浅底（进度条槽、图标底、工具按钮底）
        { "hover_bg",      "控件悬停底",   "#FFF7EA", { { "background: #FFF7EA", "#FFF7EA" },
                                                       { "background: #F6F7F9", "#F6F7F9" },
                                                       { "background: #F4F8EA", "#F4F8EA" },
                                                       { "background: #EEF9E9", "#EEF9E9" },
                                                       { "background: #F8EFE6", "#F8EFE6" },
                                                       { "background-color: #E0D6CC", "#E0D6CC" } } },
        { "text_main",     "主文字色",     "#263241", { { "color: #263241", "#263241" },
                                                       { "color: #17202A", "#17202A" },
                                                       { "color: #344054", "#344054" },
                                                       { "color: #5D4037", "#5D4037" } } },
        { "text_sub",      "次要文字色",   "#687589", { { "color: #687589", "#687589" },
                                                       { "color: #8A94A6", "#8A94A6" },
                                                       { "color: #7A8798", "#7A8798" },
                                                       { "color: #8A8A8A", "#8A8A8A" } } },
        { "text_sub2",     "浅文字色",     "#596579", { { "color: #596579", "#596579" } } },
        { "border",        "边框色",       "#E0E0E0", { { "border: 1px solid #E0E0E0", "#E0E0E0" },
                                                       { "border: 1px solid #e0e0e0", "#e0e0e0" },
                                                       { "border: 1px solid #E6D4C0", "#E6D4C0" },
                                                       { "border: 1px solid #F1ECE6", "#F1ECE6" },
                                                       { "border: 1px solid #C0B5A6", "#C0B5A6" } } },
        { "border_soft",   "浅边框色",     "#F4E8DA", { { "border-right: 1px solid #F4E8DA", "#F4E8DA" },
                                                       { "border: 1px solid #F2E8DE", "#F2E8DE" },
                                                       { "border: 1px solid #F3E7DA", "#F3E7DA" },
                                                       { "border: 1px solid #EEDCCA", "#EEDCCA" },
                                                       { "border: 2px dashed #F0B680", "#F0B680" },
                                                       { "border: 1px solid #F1E3D5", "#F1E3D5" },
                                                       { "border-right: 1px solid #F1E3D5", "#F1E3D5" },
                                                       { "border-bottom: 1px solid #F1E3D5", "#F1E3D5" },
                                                       { "gridline-color: #F1E3D5", "#F1E3D5" } } },
        { "focus",         "聚焦/选中色",  "#FFB066", { { "border: 1px solid #FFB066", "#FFB066" },
                                                       { "background: #FFB066", "#FFB066" },
                                                       { "selection-background-color: #FFB066", "#FFB066" },
                                                       { "selection-background-color: #7CB7FF", "#7CB7FF" } } },
        { "scroll_handle", "滚动条滑块",   "#E7D9C8", { { "background: #E7D9C8", "#E7D9C8" },
                                                       { "background: #D9C9B8", "#D9C9B8" },
                                                       { "background: #C9D5E3", "#C9D5E3" } } },
        { "table_head_bg", "表头底色",     "#F5F5F5", { { "background-color: #f5f5f5", "#f5f5f5" } } },
        { "table_grid",    "表格线",       "#D0D0D0", { { "gridline-color: #d0d0d0", "#d0d0d0" },
                                                       { "border: 1px solid #d0d0d0", "#d0d0d0" } } },
        // 选择夹（QTabWidget）——未选中 / 选中 两个底色，选中态的文字与描边复用
        // 「主强调色」「聚焦/选中色」，所以改主色时页签会跟着一起变
        { "tab_bg",        "选择夹底色",   "#F3EADF", { { "background: #F3EADF", "#F3EADF" } } },
        { "tab_sel_bg",    "选择夹选中底", "#FFEDD9", { { "background: #FFEDD9", "#FFEDD9" } } },
    };
    return table;
}

// 预设整套配色。第一个「默认」= 全部还原默认色，所以 colors 是空的。
const QList<ThemePreset> &presetTable()
{
    static const QList<ThemePreset> table = {
        { "default", "默认配色", {} },
        { "dark",    "黑色主题", {
            // 背景层次：窗口 → 内容区 → 侧栏 → 卡片 → 输入框
            { "window_bg",     "#1E1E1E" },
            { "content_bg",    "#252526" },
            { "sidebar_bg",    "#1A1A1A" },
            { "card_bg",       "#2D2D2D" },
            { "input_bg",      "#2E2E2E" },
            { "combo_bg",      "#2E2E2E" },
            { "combo_arrow_bg","#444444" },
            // 强调色用亮一档的橙，深底上才看得清
            { "accent",        "#FFA05C" },
            { "accent_hover",  "#FFB87A" },
            { "accent_soft",   "#3A2A1E" },
            { "soft_hover",    "#2E2620" },
            { "btn_hover",     "#4A3524" },
            { "btn_pressed",   "#5A4129" },
            { "hover_bg",      "#2A2A2A" },
            { "text_main",     "#E6E6E6" },
            { "text_sub",      "#A0A0A0" },
            { "text_sub2",     "#8C8C8C" },
            { "border",        "#3C3C3C" },
            { "border_soft",   "#333333" },
            { "focus",         "#FFA05C" },
            { "scroll_handle", "#4A4A4A" },
            { "table_head_bg", "#2D2D2D" },
            { "table_grid",    "#3C3C3C" },
            // 选择夹：未选中比卡片再亮一点区分层次，选中底是暗橙
            { "tab_bg",        "#2A2A2A" },
            { "tab_sel_bg",    "#3B2A1E" },
        } },
    };
    return table;
}

std::function<void()> &applyHook()
{
    static std::function<void()> hook;
    return hook;
}

} // namespace

namespace ThemeColors {

void setApplyHook(std::function<void()> hook)
{
    applyHook() = std::move(hook);
}

void notifyChanged()
{
    if (applyHook())
        applyHook()();

    refreshPageSheets();   // 自带样式表的页面（首页 / 日志 / 聊天 …）
}

const QList<ThemeRole> &roles()
{
    return roleTable();
}

const QList<ThemePreset> &presets()
{
    return presetTable();
}

QColor value(const QString &key)
{
    for (const ThemeRole &r : roleTable()) {
        if (r.key != key)
            continue;

        const QString saved = g_config.value("theme_" + key).toString();
        if (!saved.isEmpty()) {
            QColor c(saved);
            if (c.isValid())
                return c;
        }

        // 这一项还没有存过色（比如刚新增的配色项，老配置里没有它的键）：
        // 跟随当前生效的预设，免得深色主题下突然冒出一个浅色的新控件
        const QString curPreset = g_config.value(kPresetKeyName).toString();
        if (!curPreset.isEmpty() && curPreset != "default") {
            for (const ThemePreset &p : presetTable()) {
                if (p.key != curPreset)
                    continue;
                const QString presetColor = p.colors.value(key, QString());
                if (!presetColor.isEmpty()) {
                    QColor c(presetColor);
                    if (c.isValid())
                        return c;
                }
                break;
            }
        }

        return QColor(r.def);
    }
    return QColor();
}

void setValue(const QString &key, const QColor &color)
{
    if (!color.isValid())
        return;

    for (const ThemeRole &r : roleTable()) {
        if (r.key != key)
            continue;

        g_config["theme_" + key] = color.name();   // #rrggbb
        saveConfig();
        notifyChanged();
        return;
    }
}

void applyPreset(const QString &presetKey)
{
    for (const ThemePreset &p : presetTable()) {
        if (p.key != presetKey)
            continue;

        for (const ThemeRole &r : roleTable()) {
            QColor c(p.colors.value(r.key, r.def));
            g_config["theme_" + r.key] = c.isValid() ? c.name() : r.def;
        }

        // 记下当前用的是哪套预设：以后新增配色项时用它兜底，
        // 不会在深色主题里冒出一个默认浅色的色块
        g_config[kPresetKeyName] = presetKey;

        saveConfig();
        notifyChanged();
        return;
    }
}

void resetToDefault()
{
    applyPreset("default");
}

QString apply(const QString &baseQss)
{
    QString qss = baseQss;

    // 两阶段替换：先把要改的片段换成占位符，最后统一填色。
    // 这么做是因为「一轮一轮直接替换」会串色 —— 比如用户把主文字色也设成 #FF7F32，
    // 那么后面处理「主强调色」时会把刚写进去的文字色又改一遍。
    QList<QPair<QString, QString>> pending;   // 占位符 -> 颜色

    const QList<ThemeRole> &table = roleTable();
    for (int i = 0; i < table.size(); ++i) {
        const ThemeRole &r = table[i];

        const QString cur = value(r.key).name();
        if (cur.compare(QColor(r.def).name(), Qt::CaseInsensitive) == 0)
            continue;   // 这一项没改过，样式表原样不动

        const QString ph = QString("__THEME_%1__").arg(i);
        bool hit = false;

        for (const ThemeSeed &seed : r.seeds) {
            if (!qss.contains(seed.text, Qt::CaseInsensitive))
                continue;   // 基础样式表里没有这段（改过样式表就会走到这）

            QString dst = seed.text;
            dst.replace(seed.color, ph, Qt::CaseInsensitive);
            qss.replace(seed.text, dst, Qt::CaseInsensitive);
            hit = true;
        }

        if (hit)
            pending.append(qMakePair(ph, cur));
    }

    for (const QPair<QString, QString> &p : pending)
        qss.replace(p.first, p.second);

    return qss;
}

void applyTo(QWidget *widget, const QString &baseQss)
{
    if (!widget)
        return;

    QHash<QWidget *, QString> &sheets = pageSheets();
    if (!sheets.contains(widget)) {
        // 控件没了就把登记项摘掉（done: 信号在 ~QObject 里、断开连接之前发出，来得及）
        QObject::connect(widget, &QObject::destroyed, widget, [](QObject *obj) {
            pageSheets().remove(static_cast<QWidget *>(obj));
        });
    }

    sheets.insert(widget, baseQss);
    widget->setStyleSheet(apply(baseQss));
}

} // namespace ThemeColors
