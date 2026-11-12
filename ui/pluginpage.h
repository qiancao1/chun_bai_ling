#ifndef PLUGINPAGE_H
#define PLUGINPAGE_H


#include <qprocess.h>
#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#pragma pop_macro("slots")

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QMap>
#include <QHash>
#include <QSet>              // 铃铛商店：在途锁（一次只能一单）
#include <QReadWriteLock>
#include <QMutex>            // 铃铛商店：待结算订单表（工作线程建单 / 主线程结算）
#include <QTimer>            // 铃铛商店：1 秒扫一次过期订单
#include <QStringList>
#include <QTextBrowser>
#include <QJsonObject>
#include <QLibrary>
#include <QPlainTextEdit>  // pip/npm 安装日志区（纯文本，别用 QTextEdit —— 本工程被 #define 成 PlaceholderTextEdit 了）
#include <functional>      // sendData32Async 的回调
#include <atomic>          // m_ruleVersion（兜底提示的缓存版本号，工作线程读）
#include "qqbotclient.h"



class PluginManager;   // 前置声明
class QScrollArea;
class QLayout;


namespace py = pybind11;
const char* myCallback(const char* uuid,int apiId, int appid, const char* _1, const char* _2,
                       const char* _3, const char* _4, const char* _5,
                       const char* _6, const char* _7, const char* _8);
const char* myCallbackA(const char* uuid,int apiId, int appid, const char* _1, const char* _2,
                        const char* _3, const char* _4, const char* _5,
                        const char* _6, const char* _7, const char* _8);

// ---- pname 直接当「发起方 uuid」的载体 ----
// 展示标签一律以 '[' 开头（"[插件名|%1ms]" / "[关键词匹配|%1ms]" / "[聊天室]" …），插件侧的
// pname **传裸 uuid**（不带 '['）。所以只看首字符就能判定：
//   · 发送管线入口（send_msgAsync / send_messages 首行）用 senderUuidFromPname() 取 uuid →
//     把正文里的 [注册原名](...) 重定向成改名后的指令；
//   · addmsglog 用 pluginPnameText() 把裸 uuid 还原成 "[插件名|%1ms]" 再显示，日志看不出区别。
QString senderUuidFromPname(const QString &pname);  // 带 '[' 的展示标签 → 空；否则就是裸 uuid
QString pluginPnameText(const QString &uuid);       // 裸 uuid → "[插件名|%1ms]"；不在插件表 → 空

// 插件要发出的正文里，把 [注册原指令](...) 重定向成 [生效指令](...)（= 改名后的新名）。
//   uuid = 发起这次发送的插件；空 / 不在插件表里（框架、AI、WebUI、关键词匹配）→ 原文一字不动。
//   唯一调用点：发送管线入口（send_msgAsync / send_messages 的第一行）——
//   所以 md / 普通模式、同步 / 异步图片路径全都覆盖。
// 插件输出正文里的 [注册原指令](...) → [生效指令](...)（改名后的按钮兜底重定向）
//
// 背景：插件注册的指令被用户改名后，插件内部仍按**注册原名**输出按钮，例如 [ping]()。
// 点它发出去的是原名，而框架此时只按新名匹配（pong）→ 按钮点不动 / 提示指令不存在。
// 这里按「**该插件自己的规则表**」把正文改写成新名：
//   · [ping]()            目标为空 → 改方括号里的文字（点击就是发这段文字）
//   · [点我](ping)        目标就是指令 → 改圆括号里的目标
//   · [ping](https://..) / [x](其他) → 原样不动
// 只取「该 uuid 的插件」+「newKey 非空且 != key（真正改过名）」的规则，所以：
//   · 没改过名的插件 / 关键词匹配（uuid 不在插件表里）→ 空映射 → 原文一字不动（零开销）
//   · 别的插件的同名指令不受影响
// appid = 这条消息是**哪个机器人**发的（改名可以按机器人分开配）；<= 0 / 没配过 → 用默认档。
QString redirectPluginCmdsMarkdown(const QString &text, const QString &uuid, int appid = 0);
// API 回调：DLL 插件拿这个函数指针回主程序要数据（get_plugin_info 的第 2 参，加载时传入）。
//
// ⚠⚠ 返回值所有权（跨语言契约，动之前先想清楚）：
//   · 返回值是**框架持有**的 thread_local 缓冲（core/api.cpp 里每线程一份的 std::string）：
//     **只读**，调用方**不许 free / delete** —— 那是别的模块的堆，Windows 上直接崩堆。
//   · **不需要释放，也不可能泄漏**：没有分配就没有泄漏，下一次同线程调用会把它重置复用，
//     线程退出（goroutine 结束 / QThreadPool 回收空闲 worker）时析构。旧版插件同样安全。
//   · 唯一约束：有效期 = **本线程的下一次调用**。要留着就自己拷（strcpy / C.GoString /
//     Marshal.PtrToStringUTF8 / QString::fromUtf8），别跨线程传、别存进全局等下次用。
//   · 返回空指针 = 没有内容可回（不是错误码）；出错一律是 JSON 里的 {"error": ...}。
typedef const char* (*UniversalApiCallback)(const char* uuid,int apiId, int appid, const char* _1, const char* _2,
                                            const char* _3, const char* _4, const char* _5,
                                            const char* _6, const char* _7, const char* _8);




typedef const char* (*GetPluginInfoFunc)(char*,UniversalApiCallback);
typedef void (*OnMessageFunc)(const char*);                 // 旧版：1 参
typedef void (*OnMessageFunc2)(const char*,qint64);         // 中间版：2 参
// 新版（**导出符号 onMessagev3**，不看插件信息里的字段）：3 参
//   (json   完整消息帧；正文 = **用户实输原文**
//    fun    框架匹配到的处理函数地址（插件据此分派到自己的 handler）
//    cmd    该指令的「**生效命令词**」= 界面上改过名就是新名、没改就是原名，**绝不为空**)
//   ⚠ 参数怎么取：插件拿 cmd 去切 json 正文里的参数（如正文 "战斗 打谁"、cmd "战斗" → 参数 "打谁"）。
//     **不能拿自己注册的原名去切** —— 改名后原名与生效名长度对不上（"攻击"→"修仙攻击" 就切错）。
//   **返回值 = 想让机器人发的话**（框架代发到本条消息的来源）。NULL / 空串 = 不发，
//   插件也可以照旧自己调 send_message，两种都支持。
//   ⚠ 所有权：返回的字符串由**插件持有**（静态缓冲 / 字面量），框架只读、绝不释放。
//   ⚠ v3 是新入口，签名就从这里定死成返回 const char*；on_message / onMessagev2 仍是 void。
typedef const char* (*OnMessageFunc3)(const char*,qint64,const char*);

// ---- 铃铛商店：商品处理函数（框架只在「兑换」时调它，不进消息派发链）----
//   int  (*)(const char* json_frame, uint64_t fun, const char* cmd)
//     json_frame  完整消息帧（与 onMessagev3 第 1 参同一份）
//     fun         该商品在 "shop" 里注册的 fun 原样回传（C/C++/Rust/C# = 函数地址；Go = 处理函数 id）
//     cmd         「序号」之后的原文（含数量，可能为空串）
//   **返回值只有成败**：非 0 = 成功（框架扣费）/ 0 = 失败（不扣费、不给货）。
//   ⚠⚠ 故意**不返回字符串**：const char* 回来谁释放？插件静态缓冲怕并发、堆内存框架不知道要 free、
//      Python 侧还要再过一遍 GIL —— 一律改成 bool，要发什么话插件自己调发送 API（框架只发账单回执）。
//   ⚠ 用 int 而不是 bool：C ABI 里 bool 只占 1 字节（AL），EAX 高 24 位是垃圾，跨语言/跨编译器读法不一致；
//      int 是 0/1 全 32 位，Go / Rust / C# 都能直接用。C++ 作者写 return true/false 也没问题（隐式转 1/0）。
typedef int (*ShopFunc)(const char*,qint64,const char*);
typedef void (*OnFunc0)();

// ---- 昵称审核接口（插件可选实现，加进昵称审核窗口的下拉框后生效）----
// 函数名定死，Python 与原生库插件同名：
//   get_review_list(开始位置:int, 数量:int, 状态:int) -> JSON 文本 {"总数量":xx,"data":[{"id":整数,"name":"..","id2":".."}]}
//       状态（第 3 个参数）：1=待审核  2=已审核
//   submit_review(JSON文本) -> 任意文本  入参 {"状态":"同意"/"拒绝","data":[...]}
constexpr const char *kPluginFuncGetReviewList = "get_review_list";
constexpr const char *kPluginFuncSubmitReview  = "submit_review";
typedef const char* (*ReviewFetchFunc)(int, int, int);   // get_review_list(开始位置, 数量, 状态)
typedef const char* (*ReviewSubmitFunc)(const char*);    // submit_review
typedef const char* (*OnFunc1)();    // submit_review
typedef const char* (*OnFunc2)(const char*,const char*);    // submit_review

enum class MatchType {
    Equals,
    StartsWith,
    EndsWith,
    Contains,
    Regex,
    event
};

// 指令的「启用类型」（role）取值，三套 Rule 结构共用：
//   -1 = 停用（该指令不参与匹配）
//    0 = 全部人可用（默认）
//  1..5 = 只放行「第 1..5 级及更高权限」的人 → 见 roleAllowed()
// 数值越小权限越高，所以设置 N 表示 ev.member_role <= N 的成员都能用
// （如设置 5 → 1超管/2机器人管理/3群主/4群管理/5代管 全部放行，6群成员不行）。
// ⚠ 用 constexpr 而不是 #define：本工程已有若干宏（emit/slots/QTextEdit…）踩过坑，别再引新宏。
constexpr int kRuleRoleOff = -1;   // 停用
constexpr int kRuleRoleAll =  0;   // 全部人可用（默认）

struct Rule {
    MatchType type;
    QString key;            // 插件注册的原始指令（永不改动）
    QString newKey;         // 用户重命名后的指令（空 = 未重命名）；匹配用「非空的那个」
    int role = kRuleRoleAll;   // 启用类型（-1 停用 / 0 全部人 / 1..5 限定权限）
    py::object function;    // 已解析的 Python 可调用对象
    bool wantsCmd = false;  // 该处理函数是否接受第 2 个参数「生效命令词」（加载时按形参个数探测；
                            //   老插件只写 1 个形参 → false，仍按 1 参调用，不报错）
    bool caseSensitive = true;
    QRegularExpression regex;   // 多线程安全，只读使用
};
struct Rule_js {
    MatchType type;
    QString key;            // 插件注册的原始指令
    QString newKey;         // 重命名后的指令（空 = 未重命名）
    int role = kRuleRoleAll;   // 启用类型（-1 停用 / 0 全部人 / 1..5 限定权限）
    QString fun;    // 插件侧函数名
    bool caseSensitive = true;
    QRegularExpression regex;   // 多线程安全，只读使用
};
struct Rule_Dll {
    MatchType type;
    QString key;            // 插件注册的原始指令
    QString newKey;         // 重命名后的指令（空 = 未重命名）
    int role = kRuleRoleAll;   // 启用类型（-1 停用 / 0 全部人 / 1..5 限定权限）
    qint64 fun;    // 插件侧函数地址/序号
    bool caseSensitive = true;
    QRegularExpression regex;   // 多线程安全，只读使用
};

// 指令编辑页用的一行（只读视图，不把 py::object / 函数地址暴露给 UI 层）
struct RuleConfigRow {
    int     type    = 0;        // MatchType 的整数值
    QString key;                // 插件注册的原始指令
    QString newKey;             // 用户设置的新指令（空 = 不改名）
    int     role    = kRuleRoleAll;   // 启用类型（-1 停用 / 0 全部人 / 1..5 限定权限）
};

// 商品定价页用的一行（只读视图，同样不把插件内部结构暴露给 UI 层）
struct ShopPriceRow {
    QString item;               // 商品备注（插件注册时给的 key，同一插件内唯一 → 改价就按它认）
    qint64  basePrice = 0;      // 插件注册的价（只读展示，让你知道改的是什么）
    qint64  price     = 0;      // 当前这一档实际生效的价
    bool    overridden = false; // 当前这一档有没有单独改过（false = 跟上一级）
    // false = 下架（不卖）。⚠ **默认 false**：没配过就是下架，商品要手动上架才会卖。
    // 和「原生库崩过被自动停用」是**同一个状态**（ShopOffer::buyable），
    // 只是原因不同：那边是执行出错，这边是管理员没开 / 主动关。
    bool    allowed = false;
};

// ================= 「启用类型」下拉的取值表（UI / 匹配 共用一份，别各写各的） =================
// 下拉第 i 项 ↔ role = i - 1（第 0 项是「禁用」，所以 role = -1）。默认选中「全部人」（role 0）。
QString     ruleRoleName(int role);        // role → 界面文字（未知值兜底成「全部人」）
QStringList ruleRoleComboItems();          // 下拉框 item 顺序（与 role = index - 1 对应）
int         ruleRoleFromComboIndex(int i); // 下拉下标 → role
int         ruleRoleToComboIndex(int role);// role → 下拉下标
bool        ruleRoleMatched(int role, int memberRole);   // 权限判定：这个人能不能用这条指令

// 「某机器人 × 某插件 × 某条指令」的覆盖（按机器人区分的唯一一份数据）：启用类型 + 新指令名。
// 只存差异：没被单独配置过的机器人根本不在表里 → 匹配时直接用规则自带（= 默认）的值，零额外开销。
struct BotRuleOverride {
    int     type   = 0;                 // 匹配方式（防止同名不同匹配方式的指令串味）
    int     role   = kRuleRoleAll;      // 该机器人下这条指令的启用类型
    bool    hasNewKey = false;          // 配置里有没有 "n" 字段（有 = 覆盖，哪怕是空串）
    QString newKey;                     // 该机器人下生效的新指令名（hasNewKey 才有意义；空 = 不改名）
};

// ==================== 铃铛商店（插件出售的商品）====================
// ⚠ 定价单位：**积分**。框架设定「看一次广告 = 1000 积分」（g_points_per_ad，默认 1000），
//   所以 price 请按 **1000 的倍数** 定（1000 = 看 1 次广告、2000 = 2 次、500 = 半次就很别扭）。
//   自己心里先把「这商品值几次广告」定下来，再乘 1000 写成 price。
//
// 插件在 get_plugin_info 返回的 JSON 里多给一个 **shop 数组**（与 equals / startswith …同级）：
//   "shop": [
//     {"key": "商品备注", "fun": <处理函数>, "price": 1000}
//   ]
//   · key   = 商品备注（商店列表里显示的名字，同一插件内别重名）
//   · fun   = 处理函数：原生库 = **导出函数的地址**（与规则里的 fun 同一口径，8 字节整数）；
//             有 onShop 导出的插件（如 Go 版 SDK）= 自定义 id，框架原样回传；
//             Python  = main.py 里的**函数名**（字符串）
//   · price = 单价（**整数**，单位 = 全局货币名 g_currency_name，默认「铃铛」；1 次广告 = 1000）
//
// 用户侧两条指令（框架级，与插件规则无关，任何插件都不用自己注册）：
//   1) 「<货币名>商店」（默认「铃铛商店」）→ 列出**所有启用插件**注册的商品（全局统一序号）
//   2) 「兑换物品 <序号> <数量> <其他内容…>」→ 空格分参数；数量省略 = 1
//        · 先算总价 = 单价 × 数量，余额不够直接回「余额不足」，**根本不调插件函数**
//        · 够 → 调该商品的 fun，参数与 onMessagev3 完全一致：fun(json帧, fun, cmd)
//          cmd = 「序号」这个参数**之后的原文**（含数量与其后的内容，可能为空串）
//        · 扣费顺序：**先原子扣费 → 再调插件 → 插件说失败就原路补回**（见 ShopFunc 注释）
//        · 异步端（JS / 32 位易语言）：扣费后**开一张单子**（ShopOrder）再投请求 → 回执到了销单结算，
//          超时没回执由定时器扫出来退款（详见下面 ShopOrder 的说明）
//        · 异步端一笔最多等 1 分钟；这期间同一用户对同一商品再发兑换会被拦回「上一笔还在处理中」
//
// 原生库有两种调法（框架自动选）：
//   ① 导出了 **onShop**：调 onShop(json, fun, cmd)，fun 原样回传（Go 这种拿不了函数地址的用这个）
//   ② 没导出 onShop：把 fun 直接当函数地址调（C/C++/Rust/C# 最常见，不用多写一个导出）
struct ShopItem_Dll {
    QString key;         // 商品备注
    qint64  fun = 0;     // 插件侧处理函数地址（原生库）
    qint64  price = 0;   // 单价
};
struct ShopItem_py {
    QString    key;         // 商品备注
    py::object function;    // 已解析的可调用对象
    bool       wantsCmd = false;  // 形参 >= 2 就传 (消息, cmd)，只写 1 个就不传（与规则同一套探测）
    qint64     price = 0;   // 单价
};
// JS（type 3）：**可购买**，但走异步 —— 插件跑在独立 node 进程的 Worker 里，
//   框架「先扣费 → 开单 → 发请求 → 回包销单再发账单 / 失败退款」，不阻塞插件派发线程。
//   最多等 1 分钟（插件里要发 HTTP 等第三方结果），超时按失败自动退款。
// 32 位易语言（type 2）：**可购买**，走共享内存「任务槽目标 2」（= 工作线程 on_message 那条路）——
//   不新增命令号，只是往原始消息帧里多塞一个 **reqid** 再投一次：
//   · 宿主看到 reqid → 知道这条要回值 → 调完用 `_post4(1831501026, reqid, "", "1")` 回吐
//   · 没有 reqid 的普通消息它照旧不回（老行为不变）
//   · 回吐 "1"（或 "true"）= 成交（此时才扣费）；空 / "0" / 其它 = 失败 → 不扣钱、只回话
//   `fun` = **32 位进程里的函数值**，框架只原样透传、绝不本地解引用（那是另一个进程的地址）。
//   和 JS 一样是「先扣费 → 开单 → 投出去 → 回执销单」，最多等 1 分钟，超时由定时器退款。
struct ShopItem_js {
    QString key;            // 商品备注
    QString fun;            // main.js 里的函数名
    qint64  price = 0;      // 单价（积分）
};

// ==================== 铃铛商店：待结算订单（异步端专用）====================
// ⚠ 扣费顺序（2026-10-31 用户拍板改的）：**先问插件 → 插件说成交了才扣钱**。
//   原来「先扣费再调插件」的问题是：原生库/JS 执行时把整个框架带崩，钱已经落盘扣掉了、
//   货一行没给，重启后也没人退（订单表只在内存）。现在顺序反过来，崩溃最坏是「货给了没扣到」，
//   而插件本来就在同一个进程里，能跑到「给货」几乎必然能跑到「扣费」；用户不吃亏是硬要求。
//
//   1) **预检余额**（getPoints，只读不扣）：不够直接回「余额不足」，不开单、不调插件
//   2) **抢「一次只能一单」**（m_shopInflight）：同一用户同一时刻只允许一笔在途，
//      所以不存在「两笔都过了预检、第二笔扣不动」的并发白送（同步端也走这把锁）
//   3) 同步端（Python / 原生库）当场拿到成败：**成功才 spendPoints**，失败一分不扣
//      异步端（JS / 32 位易语言）只能等回包：开单（只记「应付多少」，不扣）→ 投出去 → 立即返回
//   4) 回执到了 → settleShopOrder(id, ok, 原因)：**先把单子从表里摘掉**，再办事 ——
//      摘不到就是「已经结算过了」（回执和超时撞车 / 插件重复回包），直接返回，绝不二次扣费
//   5) 定时器（1 秒一次）扫一遍：过了 deadline 还没回执的（宿主崩了 / 回包丢了 / 插件卡死）
//      → 按失败处理：**不扣钱、直接丢单**，回一句「超时未响应（未扣费）」
//
// 为什么订单表可以只放内存：**钱还没扣**，单子丢了 = 这单不存在，用户没有任何损失。
//   （这就是改成「先成功再扣款」换来的最大好处 —— 崩溃再也不用对账/退款了。）
struct ShopOrder {
    qint64     id = 0;              // 订单号（自增，从 1 开始）
    int        appid = 0;           // 哪个机器人
    int        mtype = 0;           // 消息类型（0 群 / 2 私聊）
    QString    groupId;             // 回执发这里
    QString    msgId;               // 走被动回复（事件/消息 id）
    QString    pname;               // 回执的 pname（= 插件 uuid，日志按它还原成 [插件名|%1ms]）
    QString    pluginName;          // 日志用
    QString    itemName;            // 商品备注（回执用）
    qint64     qty = 0;             // 数量
    qint64     fee = 0;             // **应付**积分（0 = 免费；成交时才扣，失败/超时一分不扣）
    QByteArray openidBin;           // 谁的钱（16 字节 openid）
    qint64     deadlineMs = 0;      // 到期时刻（毫秒时间戳）
};

struct PythonPluginobj {
    //py::dict globals;

    py::object instance;                 // on_message 函数
    py::object onSet;                   // 加载后调用
    py::object onEnable;                 // 启用时调用
    py::object onDisable;                // 禁用时调用
    py::object onUnload;                 // 卸载前调用
    py::object getReviewList;            // 昵称审核：get_review_list(开始位置,数量,状态)
    py::object submitReview;             // 昵称审核：submit_review(JSON文本)
    py::object get_config_list;
    py::object set_config_value;
    bool isV2 = false;      // true = get_plugin_info 返回的 JSON 里 sdk >= 2 → 属「新版」，才允许指令重命名
    QList<Rule> rules;      // 所有规则列表（替代原来的 equals hash）
    QList<ShopItem_py> shop;  // 铃铛商店：这个插件出售的商品（get_plugin_info 的 "shop"）


};
struct JsPlugin {
    QProcess* process = nullptr;
    QString entryScript;           // main.js 完整路径
    bool isReady = false;          // 是否已就绪（收到 ready 消息）
    bool isV2 = false;             // true = get_plugin_info 返回的 JSON 里 sdk >= 2 → 属「新版」，才允许指令重命名
    QJsonObject pendingRequest;    // 如果请求响应模式需要，可以暂存
    QList<Rule_js> rules;      // 所有规则列表（替代原来的 equals hash）
    QList<ShopItem_js> shop;   // 铃铛商店：可购买（异步调 Worker 里的函数，认 exports 上的函数名）
};


struct DLLPluginobj {
    GetPluginInfoFunc getPluginInfo;
    OnFunc1 get_config_list;
    OnFunc2 set_config_value;
    OnMessageFunc onMessage;
    OnMessageFunc2 onMessage2;
    OnMessageFunc3 onMessage3 = nullptr;
    bool isV2 = false;          // 新版（才允许指令重命名）：原生库(1) 看是否导出 onMessagev3；
                                // 32 位/易语言(2) 看插件信息 JSON 里的 sdk >= 2（它没有导出符号可查）
    OnFunc0 onEnable;
    OnFunc0 onDisable;
    OnFunc0 onUnload;
    OnFunc0 onSet;
    ReviewFetchFunc getReviewList = nullptr;    // 昵称审核：get_review_list(开始位置,数量,状态)
    ReviewSubmitFunc submitReview = nullptr;    // 昵称审核：submit_review(JSON文本)
    ShopFunc onShop = nullptr;  // 铃铛商店：可选导出 onShop(json, fun, cmd)；有它时 fun 只当 id 用
    QList<Rule_Dll> rules;      // 所有规则列表（替代原来的 equals hash）
    QList<ShopItem_Dll> shop;   // 铃铛商店：出售的商品（type 1 = 可购买；type 2 易语言 = 只上架）
};
struct PluginInfo {
    QString id;
    QString name; //插件名字

    QString version; //版本

    QString author; //作者
    QString description; // 插件说明
    QString path;    // 路径
    QString icon;
    QLibrary* dllLib = nullptr;
    QString loadedDllPath;
    PythonPluginobj python;
    DLLPluginobj DLL;
    JsPlugin js;
    QString uuid;
    QList<int> appid;
    int type=0;        // "DLL" 或 "内置"
    int version_int=0; //版本
    int SendQuantity=0;
    bool enabled;
};
// 自定义列表小部件

class PluginItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginItemWidget(const PluginInfo &info, QWidget *parent = nullptr);
    void updateInfo(const PluginInfo &info);

private:
    QLabel *iconLabel;
    QLabel *statusIndicator;
    QLabel *nameLabel;
    QLabel *authorLabel;
    QLabel *versionLabel;
    QLabel *typeLabel;      // 插件类型徽章（Python / DLL / DLL32 / JS）
};

class PluginPage : public QWidget {
    Q_OBJECT
public:
    explicit PluginPage(QWidget *parent = nullptr);
    void foruninstall_Plugin();
    QString LoadPlugin(const QString &path,int type,bool enabled,QList<int> &array);
    QString LoadPlugin_DLL(PluginInfo &info);
    QString LoadPlugin_py(PluginInfo &info);
    bool uninstall_Plugin(int index);//卸载
    bool uninstall_Plugin2(int index);
    // 市场「覆盖安装」专用：先记下旧插件的启用态再卸（不弹确认框）。
    // 必须走它、不能直接用 uninstall_Plugin2 —— Python / JS 重装走的是
    // doLoadPythonPlugin / doLoadPlugin，那两条入口内部写死 LoadPlugin(..., enabled=false)，
    // 旧记录又被 removePlugin 删掉了，结果就是「更新完插件自动变禁用」。
    // 这里把旧的 enabled 挂在 m_reinstallEnabled 上，LoadPlugin 建新记录时按同目录取回。
    bool uninstall_PluginForReinstall(int index);
    bool uninstall_Plugin(PluginInfo &info);
    bool Enabled_Plugin(int index);//启用
    bool Enabled_Plugin(PluginInfo &info);
    bool Reload_Plugin(int index);//重载
    // 按「市场口径」（插件 id，为空时退回名称）找已载入插件的下标，找不到返回 -1。
    // 与私有的 findPluginIndex 区分：那个按 path / uuid 精确匹配，判断的是"同一路径有没有被重复加载"；
    // 这个对应插件市场里 `info.id == p.id` 的那套。给市场「重新安装」用 —— 覆盖前先卸载旧实例。
    int findPluginIndexByIdentity(const QString &id, const QString &name = QString()) const;
    bool disable_Plugin(PluginInfo &info);//禁用
    void savePlugins();
    void loadPlugins();
    int dispatch_message(const QString &text, MessageEvent &msg);

    // ==================== 未命中兜底：能提示给用户的指令词总表 ====================
    // 只给 core/global.cpp 的 botnomsg 用（兜底回复里的 [匹配指令] 占位符），
    // 拿它跟用户输入算相似度、挑最像的几条。收词口径：
    //   · 只收「用户会照着打」的匹配方式（等于 / 开头是 / 结尾是 / 包含）；
    //     正则（key 是模式串）与事件（key 是事件名）跳过 —— 提示出来用户也打不出来。
    //   · 取**生效命令词**（该机器人下改过名就是新名），停用（role = -1）的跳过。
    //   · 插件本身没启用 / 对该 appid 禁用 → 整个插件跳过，与 dispatch_message 同口径。
    //   · 结果去重（忽略大小写），顺序稳定（按 m_pluginList 顺序）。
    // ⚠ 读 m_pluginList 是「工作线程只读、GUI 线程改」那套口径（与 dispatch_message 一致）。
    QStringList pluginCommandWords(int appid) const;

    // 规则配置版本号：**任何**会改变「生效命令词 / 启用类型」的保存动作都会让 rebuildBotRuleRoles() ++。
    // 只给兜底提示（global.cpp）做缓存失效用 —— 它把候选指令词表按 appid 缓存，靠这个数判断要不要重建。
    quint64 ruleVersion() const { return m_ruleVersion.load(std::memory_order_relaxed); }

    // ==================== AI 工具：直接调用某个插件的一条注册指令 ====================
    // 与 dispatch_message 的区别：**不做关键词匹配**，按「插件 + 注册原指令 + 匹配方式」定位
    // 到那一条规则直接调，所以 AI 调哪条就只跑哪条，不会顺带触发别的插件 / 框架指令。
    //
    //   pluginId / pluginName  插件身份（id 优先，空则名）；按 findPluginIndexByIdentity 查
    //   matchType              该规则注册时的 MatchType 整数值（防同名不同匹配方式串味）
    //   key                    插件注册的**原始**指令（不是改名后的）
    //   argsText               AI 给的参数原文（可为空）；内部会拼成 "生效指令 参数…" 塞进 ev.msg
    //   ev                     调用方持有的事件副本（appid / type / groupId / user / msgId 都要有）
    //   outText                有**同步返回值**时填这里（Python 插件 return 的文本 / 原生库 onMessagev3 返回值）
    //   err                    失败原因
    // 返回 0  = outText 可用，调用到此结束
    //      1  = 已投递，但这个插件/这条入口**没有返回值**（JS / 32 位易语言 / 老式原生库）
    //           → 调用方应等「发送拦截」（global.h 的 toolGateWait）
    //     -1  = 失败（err 已填）
    int invokeToolCommand(const QString &pluginId, const QString &pluginName,
                          int matchType, const QString &key, const QString &argsText,
                          MessageEvent &ev, QString &outText, QString &err);

    // 该指令在某机器人下**实际生效**的命令词（界面上改过名 = 新名，没改 = 注册原名）。
    // 插件不存在 / 找不到这条规则时原样返回 key。
    QString effectiveCommandWord(const QString &pluginId, const QString &pluginName,
                                 int matchType, const QString &key, int appid) const;

    bool handleAdRewardEvent(MessageEvent &msg);

    // ==================== 铃铛商店（框架级指令）====================
    // 「<货币名>商店」/「兑换物品 序号 数量 其他内容」都由框架直接处理，插件只需在
    // get_plugin_info 里注册 "shop"（见上面 ShopItem_Dll / ShopItem_py 的说明）。
    // 同一入口还管一条**管理员指令**「充值 <数量>」：只有超管(1，= 全局管理) / 机器人管理(2)
    // 能用（与 qqbotclient 的 member_role 同口径，私聊里也会被置成 1/2）；
    // 普通成员发「充值」直接 return false（不拦不回，命令词还给插件）。
    // 用途：测商店购买、运营补偿；单次上限 kShopRechargeMax（1 亿），改动都记事件日志。
    //   frame = 本条消息的**原始 JSON 帧**（与 onMessagev3 第 1 个参数同一份）；
    //   msg   = 本条消息事件（用户 openid / 群 / appid / msgId 都从这里取）。
    // 返回 true = 这条消息是商店指令、**已经自己回过消息了**（调用方别再往下派发，也别让 AI 兜底）。
    bool handleShopCommand(const QString &frame, MessageEvent &msg);

    // ---- 异步端（JS / 32 位易语言）的待结算订单：建单 / 销单 / 结算 / 超时丢单 ----
    // 线程说明：建单发生在**工作线程**（dispatch_message），结算发生在**主线程**（回执回调 / 定时器）
    //   → m_shopOrders 一律走 m_shopOrdersMutex；settle 用「摘掉才算我的」保证只结算一次。
    qint64 newShopOrder(const ShopOrder &o);              // 建单，返回订单号（> 0）
    bool   takeShopOrder(qint64 id, ShopOrder &out);      // 取单并从表里删掉（取不到 = 已被别人结算）
    // ---- 「一次只能一单」：同一用户（同一机器人）同一时刻只允许一笔兑换在途 ----
    // 为什么需要：现在改成「插件说成交才扣钱」，两笔同时过预检的话，第二笔结算时可能已经没钱 →
    //   货发了钱扣不到。一把在途锁直接把这种情况从「概率问题」变成「不可能」。
    // 覆盖**所有端**（同步端调插件期间也占着锁）；异步端建单后由 settle / 超时 / 退出负责释放。
    // 抢不到锁说明同一用户还有一笔没结束 → 一句「上一笔还在处理中」，连插件都不调。
    bool   tryBeginShopPurchase(int appid, const QByteArray &openidBin);  // 抢到返回 true
    void   endShopPurchase(int appid, const QByteArray &openidBin);       // 释放（重复释放无害）

    // ---- 「执行崩过的商品」黑名单（只针对 64 位原生库）----
    // 原生库商品函数是裸指针调用，插件里一个空指针解引用就能把框架带走。SEH / C++ 异常接住之后
    // 把**这个商品**记进黑名单：列表里标「已停用」，购买时直接拒绝，不再二次调用（崩溃往往可复现，
    // 再调一次就是再崩一次）。key = "<插件 uuid>|<fun>"，**只在内存**里 —— 重启 = 重新信任一次，
    // 修好插件后重启即可恢复，不用去改文件。
    QString shopBrokenKey(const QString &pluginUuid, qint64 fun) const;
    bool   isShopItemBroken(const QString &key) const;
    void   markShopItemBroken(const QString &key, const QString &why);

    void   settleShopOrder(qint64 id, bool ok, const QString &reason);  // 结算：成交才扣费并发账单 / 失败只回话
    void   sweepShopOrders();                             // 定时器每秒调：过期单按「超时失败」丢单（不扣钱）
    void   discardShopOrdersOnExit();                     // 程序退出：把还没回执的单子丢掉（没扣过钱，无需退款）

    // ---- 商品定价（框架里改，**每个机器人一份**；插件注册的 price 当作默认价）----
    // 生效顺序：bots[appid][商品]  >  items[商品]（默认档）  >  插件注册的 price
    // ⚠ 读发生在**工作线程**（每条商店指令都要取一次价），写发生在 GUI 线程 →
    //   整张表只认 m_shopPriceLock（读锁查、写锁改），别在别处直接摸 m_shopPriceCfg。
    qint64 shopPriceFor(const PluginInfo &p, const QString &itemKey, int appid) const;   // 没配过 → -1
    // 这件商品在这个机器人下**允许卖吗**。⚠ **默认 false（下架）**：新装插件 / 新注册的商品
    // 一律先下架，必须管理员在「商品定价」里改成「上架」才会卖（见 shopItemAllowed）。
    // false 时列表里标「未上架」，买它直接被拒（不调插件、不扣钱）。
    // 与「原生库崩过自动停用」共用 ShopOffer::buyable 一个状态，只是原因分开写（offByAdmin / broken）。
    bool   shopItemAllowed(const PluginInfo &p, const QString &itemKey, int appid) const;
    QList<int> shopPricePluginIndexes() const;      // 注册了 shop 的插件下标（含已全部下架的）
    QString    shopPricePluginLabel(int index) const;
    QList<ShopPriceRow> shopPriceRows(int index, int appid = 0) const;
    // 存盘 + 立即生效。appid <= 0 → 写默认档（全部机器人共用的基准）；> 0 → 写该机器人的覆盖。
    // prices / allowed 里**没传的项保持不动**（别把界面上没显示出来的商品悄悄上架 / 改价）。
    // 与上一级**相同**的项会被删掉（= 继续跟随），所以「改回去」不会留下垃圾配置。
    void applyShopPrices(int index, const QHash<QString, qint64> &prices,
                         const QHash<QString, bool> &allowed, int appid = 0);
    void clearBotShopPrices(int index, int appid);   // 删掉该机器人的定价 → 重新跟随默认
    bool hasBotShopPrices(int index, int appid) const;

    // ==================== 测试指令（检查指令会不会复读用户内容）====================
    // 判据：测试正文按匹配方式拼成「命令词 + 探测串(测试测试测试)」，真调一次插件，
    //      它想发出去的话在 core/api.cpp 的发送入口被扣下（pluginTestIntercept 那条），
    //      回复里出现探测串 = 把用户输入原样复读回去了。
    // 只有「开头是 / 结尾是 / 包含（寻找文本）」标复读；等于 / 正则 / 事件 只显示回复、不判。
    //
    // ⚠ 测试会**真的执行指令**（发话题 / 请求 AI / 写库都照做，只是它要说的话被扣下不发），
    //   所以「编辑指令」窗口的表格每行有一个「测试」按钮 → 走 testPluginCommand() 只跑那一条。
    void testPluginCommands(int index, int cfgAppid = 0, const QStringList &forceOffKeys = QStringList());
                                                                   // 整个插件全测（「编辑指令」窗口底部的「一键测试」）
                                                                   // ⚠ 全测会**跳过匹配方式是「等于」的指令**（那种正文只能是命令词本身，
                                                                   //   测了等于白调一次有副作用的逻辑）；要单独验某条「等于」，点它那行的「测试」。
                                                                   // cfgAppid = 按哪个机器人解析启用类型（0 = 默认档，即表格里那一套）。
                                                                   // forceOffKeys = 表格里**当下**标成「禁用」的条目（"<匹配方式>\x1f<注册原名>"，
                                                                   //   含还没点保存的改动）→ 一律不测，这就是「单独关掉某条指令测试」的开关。
    // 只测一条：key = 插件注册的**原始**指令、matchType = 它的匹配方式（编辑指令表格里的值）。
    // 用的是**当前已生效**的配置（表格里没点保存的改动不参与）；cfgAppid / forceOffKeys 同上。
    // 这条指令的启用类型是「禁用」（或表格里当下标成禁用）→ 不测。
    void testPluginCommand(int index, int matchType, const QString &key, int cfgAppid = 0,
                           const QStringList &forceOffKeys = QStringList());

    // ==================== 指令编辑（启用类型 / 重命名）====================
    // 配置按「插件 id」存；id 为空时退化成插件名。给 RuleEditDialog 用的几个只读视图接口。
    QList<int> ruleConfigPluginIndexes() const;         // 有指令的插件下标（按列表顺序）
    QString    ruleConfigPluginLabel(int index) const;  // 列表里显示的文字
    // appid = 0 / <0 时是「默认（全部机器人）」那一档；传具体 appid 时返回**该机器人**的生效视图
    // （没单独配过 → 就是默认值，所以打开就能看到它现在实际跑的是什么）。
    QList<RuleConfigRow> ruleConfigRows(int index, int appid = 0) const;
    bool ruleConfigAllowRename(int index) const;        // 只有新版插件才允许改名（原生库看 onMessagev3 符号；32位/Python/JS 看 sdk 字段）
    // 存盘 + 立即对活体生效。appid <= 0 → 写「默认」档（启用类型 + 新指令名，全部机器人共用）；
    // > 0 → 只写该机器人的覆盖（启用类型 + 新指令名；改名按「与默认的差异」存，见 m_ruleCfg 注释）。
    void applyRuleConfigRows(int index, const QList<RuleConfigRow> &rows, int appid = 0);
    void clearBotRuleConfig(int index, int appid);      // 删掉该机器人的覆盖 → 重新跟随默认
    bool hasBotRuleConfig(int index, int appid) const;  // 该插件对某机器人有没有单独配置
    // 发送管线（core/api.cpp 的 markdown 指令重定向）用：某机器人下这条指令实际生效的新指令名。
    // 没单独配过 → 返回 baseNewKey；appid <= 0 → 也返回 baseNewKey（零查表）。
    QString effectiveNewKey(int appid, const QString &pluginKey, int type,
                            const QString &key, const QString &baseNewKey) const;

    // 机器人下拉用的两个列表（一一对应）。第 0 项固定是「默认（全部机器人）」、appid = 0。
    QStringList ruleConfigBotLabels() const;
    QList<int>  ruleConfigBotAppids() const;

    void initPluginList(const QList<PluginInfo> &plugins);
    void appendPlugin(const PluginInfo &info);
    void insertPlugin(int index, const PluginInfo &info);
    void removePlugin(int index);
    void updatePlugin(int index, const PluginInfo &newInfo);
    void addPluginItemToUI(int index, const PluginInfo &info);
    void insertPluginItemToUI(int index, const PluginInfo &info);
    void updatePluginItemInUI(int index);
    // pip / npm 安装的**实时**输出回调（可空）。每读到一块输出就回调一次：
    //   text    = 该块文本（已按行清洗，可能多行）
    //   isError = 来自 stderr（桌面弹窗标红）
    //   isFinal = **最后一条**（子进程已退出，装完了）。成功/失败看 isError；
    //             成功时也照样带 true —— WebUI 靠它决定「什么时候可以关安装弹窗」。
    // 桌面那个安装日志弹窗照旧显示，这个回调只是把**同一份**输出额外给外部
    // （目前唯一使用者：WebUI 控制台的安装弹窗，见 network/webuiadmin.cpp）。
    using InstallLogFn = std::function<void(const QString &text, bool isError, bool isFinal)>;
    void npmJSpk(const QString &dir, InstallLogFn onLog = InstallLogFn());
    QString LoadPlugin_js(PluginInfo &info);
    int findPluginIndex(const QString &id) const;

    // ==================== 往 32 位模块（miaomiao32）发命令 ====================
    // 桥接本身只有「写任务槽 → 阻塞等返回值」这一条同步通道（processRequestsA）。
    // 直接在 GUI 线程上等 = 界面「无响应」+ 全 app 的 QTimer / 网络 / AI 一起停摆
    //（32 位模块没在跑时要白等满 5 秒），所以内核改成了**异步**：
    //   写任务槽 → 立刻返回 → 每 100ms 用 QTimer 回头看一次有没有返回值 → 拿到就回调，到点超时。
    // 100ms 这个间隔是 2026-10-09 用户拍板的（对用户无感）。
    QString sendData32(int type,PluginInfo &info,const QString &appidlist = QString());
    QString sendData32(int type, PluginInfo &info, const QString &id, const QString &value);
    // 异步内核：回调在**调用者线程**执行（GUI 线程发的就在 GUI 线程回调）。
    //   timeoutMs <= 0 → 只发不等（返回值没人要的场景，连等待者都不登记、一个定时器都不起）
    //   target = 任务槽的「目标」字段：
    //     1（默认）= 主线程通道（易语言在窗口线程里按 type 分派：1 加载 / 10 同步插件 …）
    //     2       = 工作线程通道（易语言 `_完成端口投递` → on_message 派发，**多线程**）
    //     商店购买用 2：跟普通消息投递同一条路，只是帧里多带 reqid → 宿主处理完回吐结果。
    // 每条命令都会领一个**请求号**塞进 JSON 的 "reqid"（易语言回吐时原样带回，见
    // SharedMemoryBridge::deliverCommandResult），所以**多条命令可以同时在飞**、互不串包。
    // 只有「桥里登记失败」（请求号用尽）才会立即回调空串。
    void sendData32Async(const QJsonObject &req, int timeoutMs,
                         const std::function<void(const QString &)> &cb, int target = 1);
    // 只发不等：和 sendData32 一样的组帧，但甩完就走（调用点本来也不看返回值）
    void sendData32NoWait(int type, PluginInfo &info, const QString &appidlist);
    QString LoadPlugin_DLL32(PluginInfo &info);
    void syncPluginsTo32();
    // 安装 Python 引用库：入参是「用户输入的文本」（一行一个 / 空格分隔，支持 # 注释），
    // 直接在独立终端窗口里 `pip install <包名...> Pillow`（原来是 `-r <txt 路径>`）
    QString anzpip(const QString &pkgsText);
    void safeCall(const py::object &func);

    void LoadPlugin_Python_pip(const QString &dir, InstallLogFn onLog = InstallLogFn());

private slots:
    void onPluginSelected(int row);
    void onAccountCheckStateChanged(QListWidgetItem *item);
    void onPluginRowsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row);
    void stopAsyncioThread();
    void onItemDoubleClicked(QListWidgetItem *item);//列表被双击

private:
    // 插件加载完成（rules 已解析）后套用已保存的指令配置（启停 / 重命名 / regex 重建）
    void applySavedRuleConfig(PluginInfo &info);

    // 32 位命令的**同步外壳**（给上面那两个 sendData32 用）：
    //   GUI 线程 → 局部事件循环驱动 100ms 轮询（界面照常重绘，其它定时器/网络也照跑）；
    //   ⚠ 等待期间不派发用户输入（按钮点击排队，等这次做完再处理），否则连点两下会撞上
    //     「同一时间只允许一个命令在飞」的桥接约束；
    //   非 GUI 线程（WebUI 的 HTTP 线程 / 机器人线程）没有事件循环可跑 → 保持原来的阻塞等待。
    QString sendData32Wait(const QJsonObject &req, int timeoutMs);

    void doLoadPlugin(const QString &dir); // 从 LoadPlugin_JS 中提取加载逻辑
    void initPython();

    void doLoadPythonPlugin(const QString &dir);  // 加载插件核心逻辑

    // ⚠ pip / npm 的安装状态**故意不放在成员里**（2026-10-11 改）。
    //    以前是 m_pipDialog / m_pipLog / m_pipProcess 三个单指针，于是「同一时刻只有一次安装」
    //    成了隐含硬约束：第二次安装一赋值就把第一次的句柄覆盖掉 → 旧进程 finished 时拿着
    //    新进程的 workingDirectory() 去 doLoadPythonPlugin（装完加载的是**另一个插件**），
    //    旧进程那个 10 秒 QTimer 还会把 m_pipDialog 置空、让新弹窗从此收不到任何日志。
    //    桌面单线程 + 弹窗挡着主界面看不出问题，但 WebUI 走的是**全局唯一**的 pluginPage
    //    （core/global.h），WebSocket 回包也在 GUI 线程 → 一并发就炸。
    //    现在每次安装各自 new 一套 dialog / log / process，用 shared_ptr<InstallCtx>
    //    贯穿所有回调（见 pluginpage.cpp 的 InstallCtx / drainInstallOutput），真正可以重复创建。

    void setupUi();

    // 配置区视口宽度一变（首次布局 / 拉窗口），折行数就变 → 高度要跟着重算
    bool eventFilter(QObject *obj, QEvent *event) override;

    // 测试指令的干活函数：onlyKey 非空 = 只跑「这一条」（注册原名 + 匹配方式），空 = 整个插件全跑。
    // cfgAppid = 按哪个机器人解析启用类型 / 新指令名（0 = 默认档）—— 用「编辑指令」窗口里选的那一档，
    // 保证「表格上显示禁用 = 测试真的不跑」，与真实派发时的按机器人覆盖一致。
    // 上面 testPluginCommands / testPluginCommand 都是它的薄壳。
    void runPluginTest(int index, const QString &onlyKey, int onlyType, int cfgAppid,
                       const QStringList &forceOffKeys);

    void updateInfo(const PluginInfo &info);
    void LoadPlugin_DLL();
    // 「添加-脚本」：选文件夹 → 先看 main.py，再看 main.js，按命中项分流到 pip / npm 入口
    // （两个都没有就提示；默认目录定位到 <程序目录>/plugins）
    void LoadPlugin_Script();
    void updateDetailPanel(int index);
    void updateAccountCheckList(int pluginIndex);
    // 返回 true = 该插件**有注册规则命中**这条消息（空规则表的插件不算命中）。
    // dispatch_message 用它累加 ok：有任何一处命中就 return 250（= 已被处理，别走 AI 兜底）。
    bool onMessageReceived(MessageEvent &msg, const PluginInfo &p, std::optional<py::gil_scoped_acquire> &gil) ;
    // ---- 插件配置（get_config_list / set_config_value，四种类型插件都可实现）----
    void rebuildConfigPanel(int index);
    void clearConfigPanel();
    void updateConfigScrollHeight();          // 按可用宽度重算配置区高度（不超过 5 行）
    int  configAvailableWidth() const;        // 配置区可用宽度（兜底 260）
    QString callGetConfigList(int index);
    QString callSetConfigValue(int index, const QString &id, const QString &value);
    void applyPluginConfig(int index, const QString &id, const QString &value);
    QListWidget *pluginListWidget;
    QPushButton *reloadBtn;
    QPushButton *openDirBtn;
    QLabel *detailpathLabel;
    QTextBrowser *detailDescLabel;
    QScrollArea *configScroll = nullptr;      // 插件配置滚动区
    QWidget *configContainer = nullptr;       // 配置项容器
    QLayout *configLayout = nullptr;          // 配置项布局（自适应折行，见 pluginpage.cpp）
    bool m_configHeightLock = false;          // 重算高度时防递归（改高度会再触发视口 resize）
    QListWidget *rightCheckList;
    QPushButton *pypip,*ai_c_j,*ai_b_j;
    QPushButton *plugin_sc;
    QPushButton *loadBtn;
    QPushButton *addPluginBtn;    // 添加-DLL
    QPushButton *addScriptBtn;    // 添加-脚本（Python / JS 合并成一个按钮）
    QPushButton *uninstallBtn;   // 卸载按钮
    QPushButton *setBtn;
    QPushButton *bj_zl = nullptr;   // 「编辑指令」按钮
    QPushButton *shopPriceBtn = nullptr;   // 「商品定价」按钮（铃铛商店按机器人改价）
    // 指令配置。结构（2026-10-20 起支持按机器人区分）：
    //   { "<插件id或名>": {
    //         "rows": [ {"t":匹配方式,"k":注册原指令,"n":新指令,"r":启用类型} ... ],   // 默认（全部机器人）
    //         "bots": { "<appid>": { "<注册原指令>": {"t":匹配方式,"r":启用类型,"n":新指令} } }
    //     } }
    //   · "bots" 里的 "n" 可以**不写**（= 跟随默认档的改名）；写了空串 = 这个号不改名。
    // ⚠ 老配置（值是**数组**）读进来时会被 migrateRuleCfg() 直接包成 { "rows": [...] }，行为不变。
    QJsonObject m_ruleCfg;
    // 规则配置的版本号：rebuildBotRuleRoles() 每次都 ++（载入配置 / 每次保存各一次）。
    // 兜底提示（global.cpp）拿它判断「候选指令词缓存」要不要重建 —— 改名 / 改启用类型后立刻生效。
    // atomic：写发生在 GUI 线程，读发生在工作线程 / 主线程定时器。
    std::atomic<quint64> m_ruleVersion{0};
    // 商品定价 / 上下架（结构见 shopPriceFor 上方的注释）：
    //   { "<插件id>": { "items": { "<商品备注>": 1200 },              // 默认档价格
    //                   "bots":  { "<appid>": { "<商品备注>": 500 } },
    //                   "on":    { "<商品备注>": true },               // 上架白名单（缺省 = 下架）
    //                   "onbots":{ "<appid>": { "<商品备注>": true } } } }
    // ⚠ key 必须是**跨重启稳定**的插件 id（`PluginInfo::id`，空则 name），**不能用 uuid**：
    //   uuid 是每次加载新生成的 QUuid，拿它当 key 重启后一条都对不上（价格回落、商品全变下架）。
    // 只存**改过的**项；没配过 = 用插件注册的 price / 下架。工作线程读、GUI 线程写 → 认 m_shopPriceLock。
    QJsonObject m_shopPriceCfg;
    mutable QReadWriteLock m_shopPriceLock;
    // 上面 "bots" 的**热路径缓存**（匹配每条消息都要查，不能每次去翻 QJsonObject）：
    //   appid → 插件key → 注册原指令 → 覆盖
    QHash<int, QHash<QString, QHash<QString, BotRuleOverride>>> m_botRuleRole;
    // ⚠ dispatch_message 在 QThreadPool 的**多个工作线程**上并发跑，而保存配置发生在 GUI 线程
    //   → 这张表必须加锁（重建用写锁、匹配用读锁；读锁路径只有一次 isEmpty 快判 + 一次查表）。
    mutable QReadWriteLock m_botRoleLock;
    void rebuildBotRuleRoles();          // 从 m_ruleCfg 重建上面的缓存（载入配置 / 保存后各调一次）
    // 取某机器人对某条指令的覆盖：role / newKey 都是 **in-out**（传入默认值，命中覆盖就改写）。
    // 该机器人没单独配过 → 原样返回，等价于改动前；newKey 出参空 = 不改名。
    void botRuleOverrideOf(int appid, const QString &pluginKey, int type,
                           const QString &key, int &role, QString &newKey) const;
    int currentSelected_index;
    std::thread m_asyncio_thread; // 改成成员变量

    // ==================== 市场覆盖安装：旧启用态暂存 ====================
    // key = 归一化后的插件**目录**绝对路径（见 pluginpage.cpp 的 reinstallKey）。
    // 只有 market 的 uninstall_PluginForReinstall 会写，LoadPlugin 建新记录时 take 一次即删。
    QHash<QString, bool> m_reinstallEnabled;

    // ==================== 铃铛商店：待结算订单表 ====================
    // ⚠ 工作线程建单 + 主线程结算（回执回调 / 定时器）→ 全表只认 m_shopOrdersMutex。
    QHash<qint64, ShopOrder> m_shopOrders;
    mutable QMutex           m_shopOrdersMutex;
    qint64                   m_shopOrderSeq = 0;      // 自增订单号（在锁里 ++）
    QTimer                  *m_shopOrderTimer = nullptr;   // 1 秒扫一次过期单（构造里建）

    // ==================== 铃铛商店：在途锁（一次只能一单）====================
    // key = "<appid>:<openid 的 hex>"。三个地方动它：handleShopCommand 抢、结算/超时/退出放。
    // 用 QSet 而不是计数：抢锁本身是原子的（插不进去就是已经有一笔在途）。
    QSet<QString>            m_shopInflight;
    mutable QMutex           m_shopInflightMutex;

    // 执行崩过的商品（key = "<uuid>|<fun>"）：见 shopBrokenKey 注释，只在内存
    QSet<QString>            m_shopBroken;
    mutable QMutex           m_shopBrokenMutex;

    py::object m_asyncio_mod;
    py::object m_run_coro_func;
    py::object m_loop;
};

#endif // PLUGINPAGE_H
