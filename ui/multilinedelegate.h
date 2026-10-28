#ifndef MULTILINEDELEGATE_H
#define MULTILINEDELEGATE_H

// 表格单元格的「多行文本」编辑器。
//
// 为什么需要它：QTableWidget 默认编辑器是**单行** QLineEdit，按回车 = 提交并关掉编辑器，
// 所以默认情况下单元格里根本打不出换行（不是用户不会用，是压根没这个能力）。
// 换成 QPlainTextEdit 之后：
//   · 回车   = 插入换行
//   · Ctrl+回车 = 提交并结束编辑（想直接确认的时候用）
//   · 点别处 / 失焦 = 提交（QStyledItemDelegate 基类行为）
//   · Esc    = 取消编辑
//
// 用法（一定要配后两行，不然多行内容会被压成一行高、看不全）：
//   auto *ml = new MultiLineDelegate(table);
//   table->setItemDelegateForColumn(col, ml);
//   table->setWordWrap(true);
//   table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
//
// 纯头文件、无 Q_OBJECT（不需要 moc），可以给多个表格共用一个实例（delegate 无状态）。

#include <QStyledItemDelegate>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QAbstractItemModel>
#include <QTextOption>

class MultiLineDelegate : public QStyledItemDelegate
{
public:
    explicit MultiLineDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

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

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &opt,
                              const QModelIndex &) const override
    {
        QRect r = opt.rect;
        int wantH = qMax(96, r.height());        // 一行高的输入框写不了多行
        if (QWidget *vp = editor->parentWidget()) {
            // 别超出表格可视区：底部那几行编辑时被裁掉就只剩一条缝
            const int avail = vp->height() - r.top();
            if (avail > r.height())
                wantH = qMin(wantH, avail);
        }
        r.setHeight(qMax(r.height(), wantH));
        editor->setGeometry(r);
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
