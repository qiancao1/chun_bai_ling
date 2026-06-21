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

// RingBuffer.h
#pragma once
#include <QVector>
#include <QList>
#include <QAtomicInt>

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(){}

    void setCapacity(int newCapacity) {
        if (newCapacity <= 0) {
            m_data.clear();
            m_capacity = 0;
            m_head=0;
        } else {
            m_data.resize(newCapacity);
            m_capacity = newCapacity;
            m_head=0;
        }
    }
    int allocate() {
        if (m_capacity==0) return -1;
        return m_head.fetchAndAddOrdered(1) % m_capacity;
    }

    T* allocate2() {
        if (m_capacity==0) return nullptr;
        int idx = m_head.fetchAndAddOrdered(1) % m_capacity;
        return &m_data[idx];
    }

    T& at(int index) {
        static T dummy;
        if (m_capacity==0) return dummy;
        Q_ASSERT(index >= 0 && index < m_capacity);
        return m_data[index];
    }

    // 原有的通用读取
    QVector<T> readLatest(int n) const {
        if (m_capacity == 0) return QVector<T>();   // 直接返回空向量
        QVector<T> result;
        int head = m_head.loadAcquire();
        if (head == 0) return result;
        int total = qMin(n, head);
        result.reserve(total);
        int firstLogical = (head > m_capacity) ? (head - m_capacity) : 0;
        int startLogical = qMax(firstLogical, head - total);
        for (int logical = startLogical; logical < head; ++logical) {
            int physical = logical % m_capacity;
            result.append(m_data[physical]);
        }
        return result;
    }

    int totalWritten() const {
        if(m_capacity==0) return 0;
        return m_head.loadAcquire(); }
    int capacity() const { return m_capacity; }

private:
    QVector<T> m_data;
    int m_capacity=0;
    mutable QAtomicInt m_head;
};