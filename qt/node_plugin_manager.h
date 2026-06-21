/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef NODE_PLUGIN_MANAGER_H
#define NODE_PLUGIN_MANAGER_H

#include <QObject>
#include <QHash>
#include <QVariantMap>

class NodeProcess;

class NodePluginManager : public QObject {
    Q_OBJECT
public:
    static NodePluginManager& instance();

    // 加载插件：返回元数据，若失败则包含 "error" 字段
    QVariantMap loadPlugin(const QString& dirPath, const QString& uuid);
    bool unloadPlugin(const QString& uuid);
    bool enablePlugin(const QString& uuid);
    bool disablePlugin(const QString& uuid);
    bool isPluginEnabled(const QString& uuid) const;

    // 投递事件（可跨线程，自动转主线程）
    void postEvent(const QString& uuid, const QString& eventType, const QString &data);
    void postEventAsync(const QString& uuid, const QString& eventType, const QString &data);
    // 头文件
public:
    QString processApiRequest(const QString& uuid, const QString& method, const QJsonArray& params);
    void onApiRequest(const QString& uuid, int id, const QString& method, const QJsonArray& params);
signals:
    void pluginExited(const QString& uuid);

private slots:


    void onProcessExited(const QString& uuid);

private:
    NodePluginManager() = default;
    QHash<QString, NodeProcess*> m_processes;
};

#endif // NODE_PLUGIN_MANAGER_H