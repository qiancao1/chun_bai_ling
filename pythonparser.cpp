#include "pythonparser.h"
#include <QFile>
#include <QTextStream>

#include <QRegularExpression>

QList<PythonFunction> extractFunctions(const QString &filePath)
{
    QList<PythonFunction> result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        //qDebug() << "无法打开文件:" << filePath;
        return result;
    }
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    QStringList lines = content.split('\n');

    // 正则匹配 def 或 async def 行（只匹配行首，不考虑缩进）
    QRegularExpression funcStartRegex(R"(^(\s*)(?:async\s+)?def\s+(\w+)\s*\()");
    // 用于后续匹配冒号（判断签名结束）
    QRegularExpression colonRegex(R"(:\s*$)");

    int i = 0;
    while (i < lines.size()) {
        QString line = lines[i];
        QRegularExpressionMatch match = funcStartRegex.match(line);
        if (match.hasMatch()) {
            PythonFunction func;
            func.startLine = i;
            func.indent = match.captured(1);
            func.name = match.captured(2);
            func.isMethod = !func.indent.isEmpty();

            // 收集完整的签名（可能跨多行）
            QStringList sigLines;
            int sigEnd = i;
            int parenBalance = 0;
            bool foundColon = false;
            // 从当前行开始，逐行拼接直到括号平衡且遇到冒号
            for (int j = i; j < lines.size(); ++j) {
                QString l = lines[j];
                sigLines << l;
                // 统计括号
                for (QChar ch : l) {
                    if (ch == '(') parenBalance++;
                    else if (ch == ')') parenBalance--;
                }
                // 检查这一行是否包含冒号（且不在字符串或注释中？简化处理，只检查行尾或行内）
                if (colonRegex.match(l).hasMatch() && parenBalance == 0) {
                    foundColon = true;
                    sigEnd = j;
                    break;
                }
                // 如果括号平衡但仍未冒号，继续（可能下一行）
                if (parenBalance == 0 && j > i) {
                    // 但可能还没有冒号，继续
                }
            }
            if (!foundColon) {
                // 未找到完整签名，跳过
                i++;
                continue;
            }
            func.signature = sigLines.join('\n');

            // 提取参数列表（从签名中解析）
            QString sigText = sigLines.join(' '); // 合并成一行方便处理
            int openParen = sigText.indexOf('(');
            int closeParen = sigText.lastIndexOf(')');
            if (openParen != -1 && closeParen > openParen) {
                QString paramsStr = sigText.mid(openParen + 1, closeParen - openParen - 1);
                // 按逗号分割，但要注意嵌套括号（如默认值中的元组），简化处理：按逗号分割，忽略括号内逗号
                // 使用更稳健的方法：手动分割
                QStringList paramItems;
                int depth = 0;
                QString current;
                for (QChar ch : std::as_const(paramsStr)) {
                    if (ch == '(' || ch == '[' || ch == '{') depth++;
                    else if (ch == ')' || ch == ']' || ch == '}') depth--;
                    else if (ch == ',' && depth == 0) {
                        paramItems << current.trimmed();
                        current.clear();
                        continue;
                    }
                    current += ch;
                }
                if (!current.isEmpty()) paramItems << current.trimmed();

                // 提取每个参数的名称（忽略默认值和注解）
                for (const QString &item : std::as_const(paramItems)) {
                    QString trimmed = item.trimmed();
                    if (trimmed.isEmpty()) continue;
                    // 去掉默认值（= 后面的部分）
                    QString paramPart = trimmed.split('=').first().trimmed();
                    // 去掉类型注解（: 后面的部分）
                    paramPart = paramPart.split(':').first().trimmed();
                    // 如果是 *args 或 **kwargs，保留原样
                    if (paramPart.startsWith('*') || paramPart.startsWith('**')) {
                        func.params << paramPart;
                    } else {
                        // 提取第一个标识符
                        QRegularExpression idRegex(R"(^[a-zA-Z_]\w*)");
                        auto idMatch = idRegex.match(paramPart);
                        if (idMatch.hasMatch()) {
                            func.params << idMatch.captured();
                        } else {
                            // 保底：直接使用整个
                            func.params << paramPart;
                        }
                    }
                }
            }

            // 查找函数体结束（从 sigEnd+1 开始）
            int funcEnd = sigEnd;
            QString baseIndent = func.indent;
            for (int j = sigEnd + 1; j < lines.size(); ++j) {
                QString nextLine = lines[j];
                if (nextLine.trimmed().isEmpty() || nextLine.trimmed().startsWith('#')) {
                    // 空行或注释行跳过，但视为函数体的一部分（保留）
                    continue;
                }
                // 计算当前行的缩进
                QRegularExpression indentRegex(R"(^(\s*)\S)");
                auto indentMatch = indentRegex.match(nextLine);
                if (indentMatch.hasMatch()) {
                    QString currentIndent = indentMatch.captured(1);
                    // 如果缩进小于等于基准缩进，说明函数结束
                    if (currentIndent.length() <= baseIndent.length()) {
                        break;
                    }
                }
                funcEnd = j;
            }

            // 提取函数体（从 sigEnd+1 到 funcEnd）
            QStringList bodyLines;
            for (int j = sigEnd + 1; j <= funcEnd; ++j) {
                bodyLines << lines[j];
            }
            func.body = bodyLines.join('\n');
            func.endLine = funcEnd;

            // 识别所属类（如果是方法）
            if (func.isMethod) {
                for (int k = func.startLine - 1; k >= 0; --k) {
                    QRegularExpression classRegex(R"(^class\s+(\w+)\s*[:\(])");
                    auto classMatch = classRegex.match(lines[k]);
                    if (classMatch.hasMatch()) {
                        func.className = classMatch.captured(1);
                        break;
                    }
                    // 如果遇到缩进小于当前缩进的行，停止
                    QRegularExpression indentRegex(R"(^(\s*)\S)");
                    auto indentMatch = indentRegex.match(lines[k]);
                    if (indentMatch.hasMatch()) {
                        QString currentIndent = indentMatch.captured(1);
                        if (currentIndent.length() < func.indent.length()) {
                            break;
                        }
                    }
                }
            }

            result.append(func);
            i = funcEnd + 1; // 跳过已处理的函数
        } else {
            i++;
        }
    }

    return result;
}

QString getFunctionCode(const QString &filePath,
                                      const QString &funcName,
                                      bool isMethod,
                                      const QString &className)
{
    auto funcs = extractFunctions(filePath);
    for (const auto &func : funcs) {
        if (func.name == funcName && func.isMethod == isMethod) {
            if (isMethod && !className.isEmpty()) {
                if (func.className != className) continue;
            }
            // 提取从 startLine 到 endLine 的所有行
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return QString();
            }
            QString content = QString::fromUtf8(file.readAll());
            file.close();
            QStringList lines = content.split('\n');
            QStringList codeLines;
            for (int i = func.startLine; i <= func.endLine; ++i) {
                codeLines << lines[i];
            }
            return codeLines.join('\n');
        }
    }
    return QString();
}

#include <QFile>
#include <QTextStream>

#include <QRegularExpression>
bool replaceFunction(const QString &filePath,
                                   const QString &funcName,
                                   const QString &newCode,
                                   bool isMethod,
                                   const QString &className,
                                   QString *errorMsg)
{
    if (errorMsg) errorMsg->clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = "无法打开文件: " + filePath;
        return false;
    }
    QTextStream in(&file);
    in.setCodec("UTF-8");  // 强制 UTF-8
    QStringList lines;
    while (!in.atEnd()) {
        lines << in.readLine();
    }
    file.close();

    // 确定查找范围（如果指定了类名，先定位类）
    int searchStart = 0;
    int searchEnd = lines.size() - 1;
    QString classIndent; // 类的缩进（用于判断类内方法）

    if (!className.isEmpty()) {
        QRegularExpression classRegex(R"(^class\s+)" + QRegularExpression::escape(className) + R"(\s*[:\(])");
        bool foundClass = false;
        for (int i = 0; i < lines.size(); ++i) {
            if (classRegex.match(lines[i]).hasMatch()) {
                searchStart = i;
                // 确定类的缩进
                QRegularExpression indentRegex(R"(^(\s*)\S)");
                auto match = indentRegex.match(lines[i]);
                if (match.hasMatch()) classIndent = match.captured(1);
                // 查找类结束位置（下一个顶格非空行）
                for (int j = i + 1; j < lines.size(); ++j) {
                    QString l = lines[j];
                    if (l.trimmed().isEmpty() || l.trimmed().startsWith('#')) continue;
                    QRegularExpression indentRegex2(R"(^(\s*)\S)");
                    auto m2 = indentRegex2.match(l);
                    if (m2.hasMatch() && m2.captured(1).length() <= classIndent.length()) {
                        searchEnd = j - 1;
                        break;
                    }
                }
                foundClass = true;
                break;
            }
        }
        if (!foundClass) {
            if (errorMsg) *errorMsg = "未找到类: " + className;
            return false;
        }
    }

    // 构建函数定义的正则（支持 async def）
    QString defPattern = QString(R"(^\s*(?:async\s+)?def\s+%1\s*\()")
                             .arg(QRegularExpression::escape(funcName));
    QRegularExpression defRegex(defPattern);

    int funcStart = -1;
    int funcEnd = -1;
    QString baseIndent;

    // 在搜索范围内查找
    for (int i = searchStart; i <= searchEnd; ++i) {
        QString line = lines[i];
        if (defRegex.match(line).hasMatch()) {
            // 检查缩进是否符合 isMethod 要求
            QRegularExpression indentRegex(R"(^(\s*)\S)");
            auto match = indentRegex.match(line);
            QString indent = match.hasMatch() ? match.captured(1) : "";
            bool isMethodCandidate = !indent.isEmpty();
            if (isMethod != isMethodCandidate) continue; // 缩进不匹配

            // 如果是类方法，还要确保缩进大于 classIndent
            if (isMethod && !classIndent.isEmpty()) {
                if (indent.length() <= classIndent.length()) continue;
            }

            funcStart = i;
            baseIndent = indent;
            // 确定函数体结束（从下一行开始，直到缩进 <= baseIndent 且非空非注释）
            for (int j = i + 1; j <= searchEnd; ++j) {
                QString l = lines[j];
                if (l.trimmed().isEmpty() || l.trimmed().startsWith('#')) {
                    // 空行和注释属于函数体，继续
                    continue;
                }
                QRegularExpression indentRegex2(R"(^(\s*)\S)");
                auto m2 = indentRegex2.match(l);
                if (m2.hasMatch()) {
                    QString curIndent = m2.captured(1);
                    if (curIndent.length() <= baseIndent.length()) {
                        funcEnd = j - 1;
                        break;
                    }
                }
            }
            if (funcEnd == -1) funcEnd = searchEnd; // 到文件末尾
            break;
        }
    }

    if (funcStart == -1) {
        if (errorMsg) *errorMsg = "未找到函数: " + funcName;
        return false;
    }

    // 执行替换：将 [funcStart, funcEnd] 替换为 newCode 的分行
    QStringList newLines = newCode.split('\n');
    // 如果 newCode 没有缩进，而原函数有基准缩进（类方法），需要自动添加缩进吗？
    // 但我们要求用户提供的新代码必须已经包含正确的缩进（包括类方法的额外缩进）
    // 这里直接替换
    lines.erase(lines.begin() + funcStart, lines.begin() + funcEnd + 1);
    for (int i = 0; i < newLines.size(); ++i) {
        lines.insert(lines.begin() + funcStart + i, newLines[i]);
    }

    // 写回文件
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMsg) *errorMsg = "无法写入文件: " + filePath;
        return false;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << lines.join('\n');
    file.close();
    return true;
}