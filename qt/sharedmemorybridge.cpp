#include "SharedMemoryBridge.h"
#include <QCoreApplication>
#include <QProcess>
#include <QUuid>
#include <cstring>
#include <QElapsedTimer>
#include <QApplication>
#include <qjsondocument.h>
#include <qjsonobject.h>
// 共享内存布局（两边必须一致）
static const size_t BLOCK_SIZE = 1024 * 1024;           // 1 MB
static const int    NUM_BLOCKS = 5;
static const size_t REQ_REGION_OFFSET = 0;
static const size_t RESP_REGION_OFFSET = 5 * BLOCK_SIZE;
static const size_t TOTAL_SIZE = 11 * 1024 * 1024;      // 10 MB
SharedMemoryBridge *bridge = nullptr;
// 事件名后缀约定
static const char* REQ_EVENT_SUFFIX = "_ReqEvent";
static const char* RESP_EVENT_SUFFIX = "_RespEvent";

SharedMemoryBridge::SharedMemoryBridge(QObject *parent) : QObject(parent) {}

SharedMemoryBridge::~SharedMemoryBridge()
{
    stopServer();
}

void SharedMemoryBridge::setCallback(Callback cb)
{
    m_callback = std::move(cb);
}

bool SharedMemoryBridge::startServer(bool debug)
{
    if (m_hMapFile) return false;
    m_debug=debug;
    // 1. 生成随机的共享内存基础名（不带 Global\）
    QString baseName = QString("QtBridge_%1")
                           .arg(QUuid::createUuid().toString(QUuid::Id128).mid(1, 8));
    m_sharedMemName = baseName;
    QString reqEventName = baseName + REQ_EVENT_SUFFIX;
    QString respEventName = baseName + RESP_EVENT_SUFFIX;
    if(debug)
    {
        m_sharedMemName="aaaaa";
        reqEventName="bbbbb";
        respEventName="ccccc";
    }
    // 2. 创建 10 MB 共享内存
    m_hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                    0, TOTAL_SIZE, m_sharedMemName.toLocal8Bit().constData());
    if (!m_hMapFile) {
        qCritical("CreateFileMapping failed, err=%lu", GetLastError());
        return false;
    }
    m_pView = MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, TOTAL_SIZE);
    if (!m_pView) {
        qCritical("MapViewOfFile failed, err=%lu", GetLastError());
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
        return false;
    }
    memset(m_pView, 0, TOTAL_SIZE);
    m_hReqEvent = CreateEventA(nullptr, FALSE, FALSE, reqEventName.toLocal8Bit().constData());
    m_hRespEvent = CreateEventA(nullptr, FALSE, FALSE, respEventName.toLocal8Bit().constData());
    if (!m_hReqEvent || !m_hRespEvent) {
        qCritical("CreateEvent failed, err=%lu", GetLastError());
        stopServer();
        return false;
    }
    if(!debug)
    {
        m_exePath = QCoreApplication::applicationDirPath() + "/miaomiao32.exe";
        m_args.clear();
        m_args << m_sharedMemName << reqEventName << respEventName;
        bool ok = QProcess::startDetached(m_exePath, m_args);
        if (!ok) {
            qWarning("Failed to launch miaomiao32.exe, path: %s", qPrintable(m_exePath));
        }
    }


    m_stop = false;
    m_workerThread = QThread::create([this] { workerLoop(); });
    m_workerThread->start();

    return true;
}
bool SharedMemoryBridge::restartYiProcess()
{
    if(m_debug) return true;

    QJsonObject cmd;
    cmd["type"] = 6;
    QByteArray data = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    writeResponseToBlock(1, data.constData());   // 发送退出命令
    QThread::msleep(300);

    return QProcess::startDetached(m_exePath, m_args);
}
void SharedMemoryBridge::stopServer()
{
    m_stop = true;
    // 设置事件以便线程退出等待
    if (m_hReqEvent)
        SetEvent(m_hReqEvent);
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    if (m_hReqEvent) { CloseHandle(m_hReqEvent); m_hReqEvent = nullptr; }
    if (m_hRespEvent) { CloseHandle(m_hRespEvent); m_hRespEvent = nullptr; }
    if (m_pView) { UnmapViewOfFile(m_pView); m_pView = nullptr; }
    if (m_hMapFile) { CloseHandle(m_hMapFile); m_hMapFile = nullptr; }
}

void SharedMemoryBridge::workerLoop()
{
    while (!m_stop) {
        // 等待易语言发来的请求事件，超时 500ms 用于检查停止标志
        DWORD ret = WaitForSingleObject(m_hReqEvent, 500);
        if (ret == WAIT_OBJECT_0) {
            processRequests();       // 处理所有块
        } else if (ret == WAIT_FAILED) {
            break;
        }
    }
}

void SharedMemoryBridge::processRequests()
{
    if (!m_pView || !m_callback) return;

    for (int i = 0; i < NUM_BLOCKS; ++i) {
        char* pBlock = static_cast<char*>(m_pView) + REQ_REGION_OFFSET + i * BLOCK_SIZE;
        LONG* pState = reinterpret_cast<LONG*>(pBlock);

        if (*pState != 1)
            continue;

        RequestData req;
        bool ok = copyRequestFromBlock(i, req);

        InterlockedExchange(pState, 0);

        if (ok) {
            auto task = new Task(this, req);
            QThreadPool::globalInstance()->start(task);
        }

    }
}

QString SharedMemoryBridge::processRequestsA(int timeoutMs)
{
    if (!m_pView) return QString();
    char* pBlock = static_cast<char*>(m_pView) + 10 * 1024 * 1024;  // 第 11 MB 基址
    LONG* pState = reinterpret_cast<LONG*>(pBlock);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (*pState == 1) {

            const qint32* pApiId = reinterpret_cast<const qint32*>(pBlock + 8);


            if (*pApiId != 1831501026) {
                InterlockedExchange(pState, 0);
                return QString();
            }
            const char* pStr = pBlock + 16;
            while (pStr < pBlock + 1024 * 1024 && *pStr != '\0')
                ++pStr;
            if (*pStr == '\0')
                ++pStr;  // 跳过 uuid 的结束符

            QByteArray text1Data;
            while (pStr < pBlock + 1024 * 1024 && *pStr != '\0') {
                text1Data.append(*pStr);
                ++pStr;
            }

            InterlockedExchange(pState, 0);
            return QString::fromUtf8(text1Data);
        }
        QApplication::processEvents();
        QThread::msleep(1);
    }

    // 超时
    qWarning() << "processRequestsA: timeout";
    return QString();
}

bool SharedMemoryBridge::copyRequestFromBlock(int blockIdx, RequestData &outReq)
{
    const char* pBlock = static_cast<const char*>(m_pView) + REQ_REGION_OFFSET + blockIdx * BLOCK_SIZE;
    const char* pData = pBlock + 4;      // 跳过状态(4字节)
    const char* pEnd = pBlock + BLOCK_SIZE;

    // 头部：resultAddr(4B) + apiId(4B) + uid(8B) = 16 字节
    if (pData + 16 > pEnd) return false;

    outReq.resultAddr = *reinterpret_cast<const quint32*>(pData);
    outReq.apiId      = *reinterpret_cast<const qint32*>(pData + 4);
    outReq.uid        = *reinterpret_cast<const qint64*>(pData + 8);
    const char* pStr = pData + 12;

    // 读取 uuid
    size_t remain = pEnd - pStr;
    outReq.uuid = readNullTerminatedString(pStr, remain);
    if (outReq.uuid.empty() && pStr[0] != '\0') return false;
    pStr += outReq.uuid.size() + 1;
    if (pStr > pEnd) return false;

    // 读取 8 个文本参数
    for (auto& text : outReq.texts) {
        remain = pEnd - pStr;
        if (remain == 0) break;
        text = readNullTerminatedString(pStr, remain);
        pStr += text.size() + 1;
        if (pStr > pEnd) return false;
    }

    return true;
}

std::string SharedMemoryBridge::readNullTerminatedString(const char* start, size_t maxLen)
{
    size_t len = strnlen(start, maxLen);
    return std::string(start, len);
}

bool SharedMemoryBridge::writeResponseToBlock(quint32 resultAddr, const char* response)
{
    if (!m_pView) return false;

    // 可写入数据的最大字节数（不包括结尾 '\0'）
    const size_t maxDataLen = BLOCK_SIZE - 12 - 1;   // 12=头部，1=结束符

    // 确定最终要写入的数据指针和长度
    const char* dataToWrite = nullptr;
    size_t dataLen = 0;

    if (!response) {
        dataToWrite = "Error: null response";
        dataLen = strlen(dataToWrite);
    } else {
        size_t originalLen = strlen(response);
        if (originalLen > maxDataLen) {
            // 超长，写入错误提示
            dataToWrite = R"({"error":"返回数据超过限制 1MB 请类型作者添加缓冲区"})";
            dataLen = strlen(dataToWrite);
        } else {
            dataToWrite = response;
            dataLen = originalLen;
        }
    }

    // 抢占空闲块
    for (int n=0;n<100;n++)
    {
        for (int i = 0; i < NUM_BLOCKS; ++i) {
            char* pBlock = static_cast<char*>(m_pView) + RESP_REGION_OFFSET + i * BLOCK_SIZE;
            LONG* pState = reinterpret_cast<LONG*>(pBlock);


            if (InterlockedCompareExchange(pState, 1, 0) == 0) {
                *reinterpret_cast<quint32*>(pBlock + 4) = resultAddr;
                *reinterpret_cast<quint32*>(pBlock + 8) = static_cast<quint32>(dataLen);
                memcpy(pBlock + 12, dataToWrite, dataLen);
                pBlock[12 + dataLen] = '\0';
                InterlockedExchange(pState, 2);
                SetEvent(m_hRespEvent);
                return true;
            }
        }
        QThread::msleep(8);
    }
    return false;   // 无空闲块
}

void SharedMemoryBridge::Task::run()
{
    if (!m_bridge->m_callback) return;

    // 准备参数
    const char* textPtrs[8] = {nullptr};
    for (int i = 0; i < 8; ++i)
        textPtrs[i] = m_req.texts[i].empty() ? nullptr : m_req.texts[i].c_str();

    const char* result = m_bridge->m_callback(
        m_req.uuid.c_str(),
        m_req.apiId,
        m_req.uid,
        textPtrs[0], textPtrs[1], textPtrs[2], textPtrs[3],
        textPtrs[4], textPtrs[5], textPtrs[6], textPtrs[7]
        );
    if(m_req.apiId==1 || m_req.apiId>=10000 && m_req.apiId<=10002) return;
    if (result) {
        m_bridge->writeResponseToBlock(m_req.resultAddr, result);
    }
}