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