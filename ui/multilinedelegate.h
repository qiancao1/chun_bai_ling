#ifndef MULTILINEDELEGATE_H
#define MULTILINEDELEGATE_H

// 表格单元格的「多行文本」编辑器 + 折叠显示。
//
// 编辑器部分：QTableWidget 默认编辑器是**单行** QLineEdit，按回车 = 提交并关掉编辑器，
// 所以默认情况下单元格里根本打不出换行（不是用户不会用，是压根没这个能力）。
// 换成 QPlainTextEdit 之后：
//   · 回车   = 插入换行
//   · Ctrl+回车 = 提交并结束编辑（想直接确认的时候用）
//   · 点别处 / 失焦 = 提交（QStyledItemDelegate 基类行为）
//   · Esc    = 取消编辑
//
// 显示部分（2026-11-03 加）：单元格里**只显示一行**（换行折叠成空格 + 右侧省略号），
// 行高固定一小截，不再按内容把整行撑成一大片（一屏只能看一两行、根本没法用）。
// 完整内容在：① 鼠标停住的 tooltip；② 点开编辑器后那个大输入框。
//
// 用法：
//   auto *ml = new MultiLineDelegate(table);
//   table->setItemDelegateForColumn(col, ml);
//   table->setWordWrap(true);                 // 显示端已强制单行，这里只为兼容旧代码
//   table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
//   table->setEditTriggers(... | QAbstractItemView::SelectedClicked);  // 想要「点一下再点一下编辑」时加
//
// 纯头文件、无 Q_OBJECT（不需要 moc），可以给多个表格共用一个实例（delegate 无状态）。

#include <QStyledItemDelegate>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QTextOption>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QToolTip>
#include <QApplication>
#include <QStyle>
#include <QPainter>

class MultiLineDelegate : public QStyledItemDelegate
{
public:
    explicit MultiLineDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    // 折叠显示：把整段压成一行，交给样式去画（超出宽度自动省略号）
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const QString full = opt.text;
        if (full.contains(QLatin1Char('\n')) || full.contains(QLatin1Char('\r'))) {
            QString one = full;
            one.replace(QLatin1Char('\r'), QLatin1Char(' '));
            one.replace(QLatin1Char('\n'), QLatin1Char(' '));
            opt.text = one.simplified();
        }
        // 关键：去掉 WrapText，样式才会单行 + 省略号；否则它按宽度折行、行高被撑开
        opt.features &= ~QStyleOptionViewItem::WrapText;

        // 不走基类 paint（基类内部会重新 initStyleOption，把上面的折叠覆盖掉），直接用样式画
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    }

    // 行高：永远一行的高度，多行内容不再参与计算
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QFontMetrics fm(opt.font);
        return QSize(120, qMax(30, fm.height() + 10));
    }

    // 鼠标停住显示完整内容（截断到 20 行，免得一大坨糊住屏幕）
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (event && event->type() == QEvent::ToolTip) {
            QString t = index.data(Qt::DisplayRole).toString();
            if (!t.trimmed().isEmpty()) {
                const QStringList lines = t.split(QLatin1Char('\n'));
                if (lines.size() > 20)
                    t = lines.mid(0, 20).join(QLatin1Char('\n'))
                        + QStringLiteral("\n…（还有 %1 行，双击看全部）").arg(lines.size() - 20);
                else
                    t += QStringLiteral("\n（双击编辑：回车换行，Ctrl+回车确认）");
                QToolTip::showText(event->globalPos(), t, view);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *ed = new QPlainTextEdit(parent);
        ed->setTabChangesFocus(true);            // Tab 别插制表符，留给表格切格
        ed->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ed->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ed->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        return ed;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *ed = qobject_cast<QPlainTextEdit *>(editor))
            ed->setPlainText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *ed = qobject_cast<QPlainTextEdit *>(editor))
            model->setData(index, ed->toPlainText(), Qt::EditRole);   // 保留换行
    }

    // 编辑器尺寸：默认就开到 minH 那么高（不再是一小条），内容更多就继续长，最多 capH，
    // 且**整个编辑器必须落在表格可视区内** —— 下面放不下就往上翻（跟下拉框上翻一个道理）。
    // 原来只钳高度、不动 top：最后一行的格子在可视区底部时，编辑器下半截直接被裁掉、没法编辑。
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &opt,
                              const QModelIndex &) const override
    {
        const QRect r = opt.rect;
        const int minH = 200, capH = 520;      // 200 ≈ 10 行，写字够用；内容再多长到 520

        int vpH = 0;
        if (QWidget *vp = editor->parentWidget())
            vpH = vp->height();

        int wantH = qMax(r.height(), minH);
        if (auto *ed = qobject_cast<QPlainTextEdit *>(editor)) {
            QTextDocument *doc = ed->document();
            doc->setTextWidth(qMax(120, r.width() - ed->frameWidth() * 2 - 8));
            // 文档高度 + 边框 + 文档 margin（少了这一截，最后一行的下一行会被切一半）
            wantH = qMax(wantH, int(doc->documentLayout()->documentSize().height()) + 24);
        }

        if (vpH > 0) {
            // 高度上限 = 可视区高度（留 8px 余量），再跟 capH 取小；同时不能比格子本身还矮
            const int hi = qMax(r.height(), qMin(capH, vpH - 8));
            wantH = qBound(r.height(), wantH, hi);
        } else {
            wantH = qMax(r.height(), qMin(capH, wantH));
        }

        int top = r.top();
        if (vpH > 0 && top + wantH > vpH - 4)
            top = qMax(0, vpH - 4 - wantH);      // 放不下 → 整体上移，盖住上面几行

        QRect g(r.left(), top, r.width(), wantH);
        editor->setGeometry(g);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(ev);
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                auto *ed = qobject_cast<QPlainTextEdit *>(obj);
                if (ed && (ke->modifiers() & Qt::ControlModifier)) {
                    // Ctrl+回车：提交并关掉编辑器（跟默认表格的"回车确认"手感对齐）
                    emit commitData(ed);
                    emit closeEditor(ed, QStyledItemDelegate::NoHint);
                    return true;
                }
                if (ed) {
                    // 回车 = 换行。基类默认会 commitData + closeEditor，必须提前截住，
                    // 否则永远打不出第二行（这正是"表格不能换行"的根因）。
                    ed->insertPlainText(QStringLiteral("\n"));
                    return true;
                }
            }
        }
        return QStyledItemDelegate::eventFilter(obj, ev);
    }
};

#endif // MULTILINEDELEGATE_H
