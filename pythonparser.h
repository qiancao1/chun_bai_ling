#ifndef PYTHONPARSER_H
#define PYTHONPARSER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

struct PythonFunction {
    QString name;           // 函数名
    QString signature;      // 完整函数定义行（含 def/async def 和参数，可能跨多行）
    QStringList params;     // 参数名列表（简化提取）
    QString body;           // 函数体（不含签名，仅缩进内容）
    int startLine;          // 起始行号（def 所在行）
    int endLine;            // 结束行号（函数体最后一行）
    bool isMethod;          // 是否为类方法
    QString className;      // 所属类名（如果是方法）
    QString indent;         // 基准缩进（def 行的缩进）
};

QList<PythonFunction> extractFunctions(const QString &filePath);

QString getFunctionCode(const QString &filePath,
                               const QString &funcName,
                               bool isMethod = false,
                               const QString &className = "");
bool replaceFunction(const QString &filePath,
                     const QString &funcName,
                     const QString &newCode,
                     bool isMethod,
                     const QString &className,
                     QString *errorMsg);

#endif