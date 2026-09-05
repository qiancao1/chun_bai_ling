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

// EventTask.h
#ifndef EVENTTASK_H
#define EVENTTASK_H

#include <QRunnable>
#include <QObject>
#include "global.h"

// 任务处理器接口（实际处理事件的业务逻辑）
class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual void handle(const MessageEvent &event) = 0;
};

class EventTask : public QRunnable
{
public:
    // handler 可以是任意 QObject 子类，需要实现 handle 方法（通过 dynamic_cast 或函数回调）
    // 这里采用简单的函数回调方式，更灵活
    using EventCallback = std::function<void(const MessageEvent&)>;

    explicit EventTask(const MessageEvent &event, EventCallback callback);
    void run() override;

private:
    MessageEvent m_event;
    EventCallback m_callback;
};

#endif // EVENTTASK_H