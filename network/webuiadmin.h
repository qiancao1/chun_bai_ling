// webuiadmin.h
// 纯白铃 - WebUI 管理面板扩展接口
//
// 本文件为「新增」文件，不修改原有 websocketserver.cpp 的任何逻辑，
// 只在 websocketserver.cpp 的 action 分发链末尾挂一个分支：
//     } else if (webuiAdminHandle(action, params, client, reqId)) {
//         // 已由本模块处理
//     } else {
//         sendError(client, "Unknown action: " + action, reqId);
//     }
//
// 所有 action 都在主线程执行（与 onClientMessageReceived 同线程），
// 网络请求走 QNetworkAccessManager 异步回调，不会阻塞界面。

#ifndef WEBUIADMIN_H
#define WEBUIADMIN_H

#include <QString>
#include <QJsonObject>

class ClientConnection;

// 处理 webui 管理类扩展指令。
// 返回 true  -> 该 action 已被本模块接管（响应可能稍后异步发出）
// 返回 false -> 不是本模块的 action，交给上层原有的 sendError 处理
bool webuiAdminHandle(const QString &action,
                      const QJsonObject &params,
                      ClientConnection *client,
                      const QString &reqId);

#endif // WEBUIADMIN_H
