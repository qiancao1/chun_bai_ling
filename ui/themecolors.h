#ifndef THEMECOLORS_H
#define THEMECOLORS_H

// 界面配色（可自定义）
//
// 原理：mainwindow.cpp::applyStyleSheet() 里那张大样式表里的颜色，本来是写死的。
// 这里给其中「有意义的那些颜色」各起一个名字（ThemeRole），用户改过之后，
// 把样式表里的默认色替换成用户选的颜色，再 setStyleSheet 上去。
//
// 注意 seed 记的是「样式表里要替换的片段 + 该片段里的颜色」而不是裸颜色值：
// 同一个颜色在样式表里可能既是底色又是文字色（例如 #FFFFFF 既做卡片底色，也做选中文字色
// selection-color），只按裸色值替换会把不该改的地方也改掉，所以按片段精确替换。
//
// 存储：g_config 里的 "theme_<key>"（未知/无效 → 用默认色）。

#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QColor>
#include <functional>

// 样式表里的一处替换：把 text 片段里的 color 换成用户当前选的颜色
struct ThemeSeed {
    QString text;    // 要替换的原文（含属性名，如 "background: #FFF0DE"）
    QString color;   // 该片段里的颜色（默认值，可以是 "white" 这样的关键字）
};

struct ThemeRole {
    QString key;            // 配置键：g_config["theme_" + key]
    QString name;           // 界面上显示的名字
    QString def;            // 默认色（就是全局样式表里原本写死的那个）
    QList<ThemeSeed> seeds; // 样式表里所有用到这个颜色的地方
};

struct ThemePreset {
    QString key;                  // 预设标识
    QString name;                 // 按钮文字
    QHash<QString, QString> colors;   // 角色 key -> 颜色；没列出的用该角色的默认色
};

namespace ThemeColors {

// 主窗口注册自己的 applyStyleSheet：配色一变就重新套用样式表
void setApplyHook(std::function<void()> hook);

// 通知「配色变了」（会调用上面注册的 hook）
void notifyChanged();

// 所有可调项（顺序 = 界面显示顺序）
const QList<ThemeRole> &roles();

// 所有预设（第一个是「默认」，即把全部角色还原成默认色）
const QList<ThemePreset> &presets();

// 当前某个配色的值（没设过就返回默认色）
QColor value(const QString &key);

// 改一个配色：立即落 g_config + saveConfig + 通知主窗口重套样式
void setValue(const QString &key, const QColor &color);

// 套用整套预设（落库 + 通知）
void applyPreset(const QString &presetKey);

// 全部恢复默认：等价于 applyPreset("default")
void resetToDefault();

// 把基础样式表里的默认色替换成当前配色（没改过的一项都不动，保证零改动时输出一致）
QString apply(const QString &baseQss);

} // namespace ThemeColors

#endif // THEMECOLORS_H
