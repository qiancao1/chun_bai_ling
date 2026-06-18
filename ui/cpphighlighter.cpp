#include "cpphighlighter.h"

CppHighlighter::CppHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    // 关键字（C++ 常用）
    keywordFormat.setForeground(Qt::blue);
    keywordFormat.setFontWeight(QFont::Bold);
    QStringList keywordPatterns = {
        "\\bchar\\b", "\\bclass\\b", "\\bconst\\b", "\\bdouble\\b",
        "\\benum\\b", "\\bexplicit\\b", "\\bfriend\\b", "\\binline\\b",
        "\\bint\\b", "\\blong\\b", "\\bnamespace\\b", "\\boperator\\b",
        "\\bprivate\\b", "\\bprotected\\b", "\\bpublic\\b",
        "\\bshort\\b", "\\bsigned\\b", "\\bsizeof\\b", "\\bstatic\\b",
        "\\bstruct\\b", "\\btemplate\\b", "\\btypedef\\b", "\\btypename\\b",
        "\\bunion\\b", "\\bunsigned\\b", "\\bvoid\\b", "\\bvolatile\\b",
        "\\breturn\\b", "\\bif\\b", "\\belse\\b", "\\bswitch\\b",
        "\\bcase\\b", "\\bbreak\\b", "\\bcontinue\\b", "\\bdefault\\b",
        "\\bdo\\b", "\\bwhile\\b", "\\bfor\\b", "\\bgoto\\b",
        "\\btry\\b", "\\bcatch\\b", "\\bthrow\\b", "\\bnew\\b",
        "\\bdelete\\b", "\\bthis\\b", "\\bauto\\b", "\\bconst_cast\\b",
        "\\bdynamic_cast\\b", "\\breinterpret_cast\\b", "\\bstatic_cast\\b"
    };
    for (const QString &pattern : keywordPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    // 函数调用（后面跟着左括号）
    functionFormat.setForeground(QColor(0x705d9a)); // 紫色
    rule.pattern = QRegularExpression("\\b([A-Za-z0-9_]+)\\s*\\(");
    rule.format = functionFormat;
    highlightingRules.append(rule);

    // 预处理指令
    preprocessorFormat.setForeground(QColor(0x808080));
    rule.pattern = QRegularExpression("^\\s*#\\s*[A-Za-z_]+");
    rule.format = preprocessorFormat;
    highlightingRules.append(rule);

    // 字符串（双引号）
    quotationFormat.setForeground(QColor(0x3d8b40)); // 绿色
    rule.pattern = QRegularExpression("\".*\"");
    rule.format = quotationFormat;
    highlightingRules.append(rule);

    // 单行注释
    singleLineCommentFormat.setForeground(Qt::gray);
    rule.pattern = QRegularExpression("//[^\n]*");
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    // 多行注释（简单模式，不嵌套）
    multiLineCommentFormat.setForeground(Qt::gray);
    // 将在 highlightBlock 中特殊处理
}

void CppHighlighter::highlightBlock(const QString &text)
{
    // 应用所有正则规则
    for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // 处理多行注释（/* ... */）
    setCurrentBlockState(0);
    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf("/*");

    while (startIndex >= 0) {
        int endIndex = text.indexOf("*/", startIndex);
        int commentLength;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + 2;
        }
        setFormat(startIndex, commentLength, multiLineCommentFormat);
        startIndex = text.indexOf("/*", startIndex + commentLength);
    }
}