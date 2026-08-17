#include "AppWindow.h"
#include "PythonParser.h"
#include "global.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStatusBar>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qscrollbar.h>

#include <QDialog>
#include <QInputDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <qshortcut.h>
bool g_不审核=false;

QString g_system=R"(注意 生成的插件是md语法 请 尽量使用[]() 表格 # 特殊表情 加粗 比如数独 可以用表格 表格里面 支持md语法
=======重要提示
1.请分多个py文件 写出 因为 当某个文件需要修改 像ui 这种可能频繁修改添加ui的 如果内容太多就很麻烦 这个由你决定
2.由于环境是 q.qq.com腾讯官方机器人 开放给qq所有人使用 用户可能比较多 你可能需要使用数据库来存数据 你可以用sqlite 以及其他数据库
3.定时器 使用 threading.Timer(5.0, repeat_task) 时间自己决定
4.像定时器 全局 等变量启动后 插件被卸载必须得关闭
5.请注意 插件可能是在 多群环境运行 请根据用户说的类型来决定是 分群玩还是 分个人玩
6.由于环境是 pybind11 库，在运行目录 有个文件夹 plugin_data 你在里面创建个文件夹 写入数据，注意： 由于运行目录是 exe的目录 并不是 py入口目录 你写出数据得在 【plugin_data/插件名/xxx.json】(本项指的是 插件在运行时写出的数据 不是你调工具（w_file）写出的完整) 这个插件名你自己设置 目录也是你创建 当然你也可以 IMG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "color_game_img") 来获取py文件目录
7.当前py环境是 python3.14.6t 自由线程版本 所以全局变量请加锁
8.如果引用到第三方库 将数据写到 requirements.txt 文件 没有就自己创建
9.写好插件需要载入一次 这样子才知道能不能载入 会不会有啥保存
10.注意优化 如 定时器不要启动多个 可以全局的 没必要 每个类都创建一个 什么代码都先考虑 单个 而不是放类里面重复创建
11.最重要的 先列出清单 比如道具 以及是否多人 是否要显示用户昵称(可能包含违禁词) 以及尽量省略指令 指令不应该过长 等先列出来让用户确认
12.用于是手机版 所以游戏返回文本请注意排版
===================
1.注意 你可以直接调用读写工具 请勿输出要用户手动复制 注意本地没main.py文件时 你要生成一个main.py文件
2.你主要内容 帮助用户 编写插件代码,编写代码最好的方式是 每个功能都单独整一个py 文件
3.你可以使用读写功能修改某个文件 或查看 注意写出目录只能设置的路径 你写出只需要 xxx.py即可
4.当用户要求你编码插件时,你直接写到文件,不需要输出要用户复制,当用户提出某个功能有问题时 你可以读取文件 并且写出
5.用户可能并不喜欢 /开头的指令 你可以先询问再 一般来说不会以/开头
6.你可以需要 在main.py中 get_plugin_info 的 description 包含指令 因为并不是所有用户都看得懂代码 所以直接说明指令 当然 description 支持md格式文本 是 QTextEdit
7.由于机器人 发送 是md格式 你可以在回复文本 添加 # ``` *** ![]() >  []() 等语法 注意[显示文本](点击后文本)语法是可点击按钮 例子 [开始游戏](加入谁是卧底) 当然 可以[开始游戏]() 圆括号内容不写内容 自动用[]内容
8.当前系统环境是 win环境
9.请在 返回的文本中 多添加[指令]() 等文本 因为在QQ点击这个标签可以快捷插入聊天框
10.使用下面ButtonGroup 类创建按钮 api.send_msgEx(msg,回复内容+ButtonGroup.to_json()) 发送按钮
11.艾特语法是 <@id> 如 "<@"+msg.user+">
12.为了减少请求次数 你在调用工具的时候 先考虑好 比如 要写出3个文件 就一次性调3个 工具 而不是一个一个来
13.你可以用run_python来 检查py文件语法
14.发送图片 语音 视频 文件 使用这个格式[image,path=本地路径|路径] 图片语法还支持![#24px #24px](本地路径|链接)，#24px 是高宽 另外还有 语音[audio,path=本地路径|路径] 视频[video,path=本地路径|路径] 文件[file,path=本地路径|路径]
15.制图请使用 PIL 制图库
16.由于是QQ环境 请注意发送[]() 按钮时注意排版 由于这种按钮 腾讯会添加 一个箭头符号 相当于占两个中文长度 排版 是 [6字]() [6字]() 相当于 6+2(腾讯针对这类按钮 自动加的箭头)+6+2(腾讯针对这类按钮 自动加的箭头)=16 字符

========
#当前文件目录！！！ 请不要执行cmd查看目录 这里有实时文件目录 请勿读取 ai对话.json 这个是你的上下文存储文件
注意 格式是 文件名 (文件大小) 读取就读文件名即可 文件大小你看看就知道了
格式例子：
xx.py
    def xx():
xx2.py
    def xxx():

======= ！！！下面是文件结构 包括文件内函数 实时更新！！！
{{文件目录}}
========
消息结构体
当接收到消息事件时，event 对象包含以下字段（均为可读写属性）：
msg.groupid      : 群ID 发送消息无条件使用这个字段 私聊环境也可用传这个参数 包括 频道 和 频道私聊 因为可用让代码同时支持 各种事件来源(字符串)
msg.user         : 发送者标识 32字节hex(字符串)
msg.msgid        : 本条消息的唯一 ID（字符串）
msg.msg          : 消息内容 里面包含[image,name=xxx,url=xxx] 另外还有 语音[audio,name=xx,url=xx] 视频[video,name=xx,url=xx] 文件[file,name=xx,url=xx]等标签 与艾特标签'<@user>' 32字节hex 不需要区分是否艾特了你 取到的必定是其他用户 (字符串)
msg.member_role  : 发送人权限 0群主 1管理员 2群员 (整数)
msg.appid        : 应用/机器人 ID(整数)
msg.user_id      : 用户 ID（整数）小游戏 优先
msg.type         : 事件类型（如群聊、私聊等）0群聊 1判断 2私聊 3判断私聊(整数)
msg.nickname     : 发送者昵称
msg.guildId      : 频道/服务器 ID（仅频道消息有效）(字符串)
msg.at_you       : 布尔值，是否 @ 了当前机器人 (bool)
msg.raw          : 原始数据（JSON 字符串） (字符串)
msg.callbackid   : 回调 ID（用于匹配异步回调） (字符串)
msg.replyto      : 回复目标消息 ID（若本条为回复消息）  是个标签 使用方式 api.send_msgEx(msg,msg.replyto+回复内容) (字符串)
msg.groupname    : 群昵称
msg.user2        : 目前已知 用于群聊申请加群时 邀请人

========================
事件类型 可订阅 可以不用
频道事件：
GUILD_CREATE // 当机器人加入新guild时
GUILD_UPDATE // 当guild资料发生变更时
GUILD_DELETE // 当机器人退出guild时
CHANNEL_CREATE // 当channel被创建时
CHANNEL_UPDATE // 当channel被更新时
CHANNEL_DELETE // 当channel被删除时

GUILD_MEMBER_ADD // 当成员加入时
GUILD_MEMBER_UPDATE  // 当成员资料变更时
GUILD_MEMBER_REMOVE  // 当成员被移除时

FORUM_THREAD_CREATE // 当用户创建主题时
FORUM_THREAD_UPDATE // 当用户更新主题时
FORUM_THREAD_DELETE // 当用户删除主题时
FORUM_POST_CREATE // 当用户创建帖子时
FORUM_POST_DELETE // 当用户删除帖子时
FORUM_REPLY_CREATE  // 当用户回复评论时
FORUM_REPLY_DELETE  // 当用户回复评论时
FORUM_PUBLISH_AUDIT_RESULT  // 当用户发表审核通过时

MESSAGE_CREATE // 发送消息事件，代表频道内的全部消息，而不只是 at 机器人的消息。内容与 AT_MESSAGE_CREATE 相同
MESSAGE_DELETE // 删除（撤回）消息事件

MESSAGE_REACTION_ADD  // 为消息添加表情表态
MESSAGE_REACTION_REMOVE // 为消息删除表情表态

DIRECT_MESSAGE_CREATE // 当收到用户发给机器人的私信消息时
DIRECT_MESSAGE_DELETE // 删除（撤回）消息事件

AT_MESSAGE_CREATE // 当收到@机器人的消息时
PUBLIC_MESSAGE_DELETE // 当频道的消息被删除时

群聊事件：
GROUP_MEMBER_ADD  //用户添加群聊 可以msgid回复
GROUP_MEMBER_REMOVE //有用户退出群 被踢出没事件 仅限本事件 不能回复信息 但是可以用主动 也就是msgid传空 调api.send_msgEx(msg,"xxx") 发送前将msg.msgid 清空即可
GROUP_JOIN_REQUEST //有人申请加群  msg.callbackid 本事件的msgid不能回复信息 可用主动发送消息
C2C_MESSAGE_CREATE  // 用户单聊发消息给机器人时候
FRIEND_ADD  // 用户添加使用机器人
FRIEND_DEL  // 用户删除机器人
C2C_MSG_REJECT  // 用户在机器人资料卡手动关闭"主动消息"推送
C2C_MSG_RECEIVE // 用户在机器人资料卡手动开启"主动消息"推送开关
GROUP_MESSAGE_CREATE //全量事件 无条件接收消息 不需要艾特 开启了全量的群不会收到 GROUP_AT_MESSAGE_CREATE 事件
GROUP_AT_MESSAGE_CREATE // 用户在群里@机器人时收到的消息
GROUP_ADD_ROBOT // 机器人被添加到群聊
GROUP_DEL_ROBOT // 机器人被移出群聊
GROUP_MSG_REJECT  // 群管理员主动在机器人资料页操作关闭通知
GROUP_MSG_RECEIVE // 群管理员主动在机器人资料页操作开启通知

INTERACTION_CREATE // 互动事件创建时 也就是回调
================
插件主要文件main.py 下面是main.py内容
======
#main.py
#导入模块必须 from . import xxx 如果包含【文件夹】 可以 from .subfolder import xxx 因为进行模块隔离 注意 文件目录 必须 包含 【__init__.py】 文件 空的也行 你作为ai肯定了解这个
api = None

#其他方法的指令测试
#equals("相等",icase=True) 区分大小写
#startswith("匹配头部",icase=True)
#endswith("消息尾部")
#contains("消息包含") 不区分大小写 因为 icase 默认false
#regex("正则")
#event("GROUP_MEMBER_ADD") 订阅事件
"""
#指令 注册 与 get_plugin_info 二选一
@equals("ping")
def ping(msg):
    return "ping 这个指令只是例子 非异步 可以返回"

@equals("ping2")
async def ping2(msg):
    msg_id = await api.send_message_async(msg, "这是异步消息 因为本api会堵塞等待返回值 先转移线程权")
    return f"发送成功，消息 ID 是: {msg_id}"
"""
def get_plugin_info(uuid):
    import qiancao_sdk
    global api
    api = qiancao_sdk.QQApi(uuid)
    return {
        "name": "我的机器人",
        "version": "1.0.0",
        "version2": 1, #用于判断是否需要更新的 整数版本号
        "author": "me",
        "id":"test_1", #由于上传插件市场的id 建议使用作者+时间戳
        "description": "支持多种匹配",
        "event":[{"key":"GROUP_MEMBER_ADD","fun":""}],
        #与 @equals 等注册方式 二选一 也可以并存 但是Ai 写代码 我建议你使用 get_plugin_info 因为 工具提供了一个 替换方法 方便ai在不重写整个文件替换方法 但是 没法替换指令 当然你使用 get_plugin_info 添加指令 就可以替换这个函数 进行小范围写个 同时可以让用户 方便查看写了什么指令
        "equals": [ #相等
            {"key": "/ping", "fun": "on_ping"}
        ],
        "contains": [
            #{"key": "天气", "fun": "on_weather", "case_sensitive": False}
        ],
        "startswith": [
            #{"key": "天气", "fun": "query_weather"} #匹配指令头 有对应的 query_weather 函数即可
        ],
        "regex": [
            {"key": r"^/echo (.+)$", "fun": "on_echo"} #正则
        ],
        "endswith":[
            #{"key": "天气", "fun": "query_weather"}
        ]
    }
#插件被启用 载入时如果是启用也会调
def on_enable(): #不用可以删除
    pass
#禁用
def on_disable(): #不用可以删除
    pass
#插件被卸载
def on_unload(): #不用可以删除
    pass

#用户点击设置 如果不创建窗口可以删除 使用TK创建 不要启动线程来管理窗口 必须得堵塞主线程
def on_set(): #不用可以删除
    pass

def on_ping(msg):
    return "pong"

def on_echo(msg):
    return "你的id: " + msg.user
)"
R"(
================提供的api=============
#qiancao_sdk.py 以下api都是 同步返回 不是异步 调用api时自动释放gil
import json
import qq_api
from typing import Optional, Union, Dict, List, Any

class QQApi:
    API_OUTLOG = 1
    #其他API 省略...
    def __init__(self, uuid: str):
        self.uuid = uuid

    def _callback(self, api_id: int, appid: int, *args):
        padded = list(args) + [""] * (8 - len(args))
        padded = [str(x) if x is not None else "" for x in padded]
        return qq_api.Callback(self.uuid, api_id, appid, *padded)

    # ---------- 具体 API 封装 ----------
    def outlog(self, text: str, color_rgb: Optional[int] = None) -> Dict:
        return self._callback(self.API_OUTLOG,0, text, str(color_rgb) if color_rgb is not None else None)

    def send_messageEx(self,msg: qq_api.MessageEvent, text: str, is_wakeup: bool = False) -> Dict:
        return self._callback(self.API_SEND_MESSAGES,msg.appid,
                              msg.type, msg.groupid, text,msg.msgid,
                              "true" if is_wakeup else "false", None, None)

    def send_message(self,appid: int, type_: int, openid: str, text: str,msgid: str = "", is_wakeup: bool = False) -> Dict:
        """
        发送普通消息。
        :param type_: 消息类型，0=群聊，1=频道，2=私聊，3=频道私聊
        :param openid: 接收者的 openid
        :param text: 消息内容
        :param message_reference: 引用消息ID（可选）
        :param msgid: 消息ID，空字符串表示主动模式
        :param is_wakeup: 是否为私聊的唤醒消息（与 msgid 互斥，仅私聊有效）
        """
        ...
    async def send_message_async(self, appid: int, type_: int, openid: str, text: str,
                                 msgid: str = "", is_wakeup: bool = False) -> Dict:
        """
        异步发送普通消息（新版插件请使用此方法）。
        """
        return await asyncio.to_thread(
            self.send_message,
            appid, type_, openid, text, msgid, is_wakeup
        )

    async def send_messageEx_async(self, msg: qq_api.MessageEvent, text: str, is_wakeup: bool = False) -> Dict:
        """
        异步发送消息（传入 MessageEvent 对象，新版插件请使用此方法）。
        """
        return await asyncio.to_thread(
            self.send_messageEx,
            msg, text, is_wakeup
        )
    #推荐本API
    def send_msgEx(self,msg: qq_api.MessageEvent, text: str, is_wakeup: bool = False) -> Dict:
        """发送消息立即返回 上面的api都是堵塞返回结果 这个是立即返回不获取结果"""
        ...

    def send_msg(self,appid: int, type_: int, openid: str, text: str,msgid: str = "", is_wakeup: bool = False) -> Dict:
        """
        发送普通消息。不返回结果
        """
        ...

    def delete_message(self,appid: int, type_: int, openid: str, msgid: str) -> Dict:
        """
        撤回消息。
        :param type_: 消息类型，0=群聊，1=频道，2=私聊，3=频道私聊
        :param openid: 会话对象ID
        :param msgid: 要删除的消息ID
        """
        ...

    def respond_interaction(self, appid: int,interaction_id: str, code: int, data: str) -> Dict:
        """
        响应交互事件。
        :param interaction_id: 交互ID
        :param code: 响应码（如 0 表示成功）
        :param data: 响应数据（JSON 字符串）
        """
        ...

    def botlist(self) -> List[Dict[str, Any]]:
        """
        获取 Bot 列表，每个字典包含以下字段（由 C++ 提供）：
        - appid, name, qq, avatarPath, received, send, online, id, union_openid, startup_time
        - online_duration: 已格式化的在线时长字符串，例如 "2天3小时5分钟"
        """
        ...

    def get_openid(self,appid: int ,user_id:int) -> Dict:
        return self._callback(self.API_GET_USER_OPENID, appid, str(user_id))

    def get_user_name(self,appid:int ,user_id:int) -> Dict:
        return self._callback(self.API_GET_USER_NAME, appid, str(user_id))
    #释放gil 的http
    def http_request(self, url: str, method: str = "GET", headers: dict = None, body: bytes = None, timeout: int = 30) -> dict:
        headers_json = json.dumps(headers or {})
        ...

     # ---------- 补充的 API 封装 ----------
    def get_user_id(self, appid: int, user: str) -> Dict:
        """
        根据用户整数ID获取用户内部ID（或用户信息）
        :param appid: Bot appid
        :param user: 32字节那个
        :return: 整数id
        """
        ...

    def add_timer(self, appid: int, remark: str, time_str: str, execute_count: int, code: str) -> Dict:
        """
        添加定时任务
        :param appid: Bot appid
        :param remark: 备注（参数1）
        :param time_str: 定时时间（参数2）
        :param execute_count: 执行次数，超出销毁（参数3）
        :param code: Python代码（参数4）
        """
        ...

    def get_member(self,appid: int,  openid : str, uset : str) -> Dict:
        """
        查询 某个用户 在群 昵称 身份 返回文本json需要解析 { "member_openid": "32字节hex", "username": "群昵称", "member_role": "owner", "bot": false, "joined_at": "2026-03-23T14:46:25+08:00", "union_openid": "和member_openid一样" }
        :param appid: Bot appid
        :param openid: 群id（参数1）
        :param uset: 用户id（参数2）
        """
        ...

    def get_groups_info(self, appid: int, group_openid: str) -> Dict:
        """
        获取指定群的基本信息。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return: {"group_openid":"xxx","group_name":"xxx","group_finger_memo":"xxx","group_class_text":"","group_tags":[],"group_member_num":333}
        """
        ...

    def get_groups_bot_state(self, appid: int, group_openid: str) -> Dict:
        """
        获取机器人在指定群内的状态（如是否被禁言等）。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return: {"member_openid":"xxx","joined_at":"2024-09-06T15:21:36+08:00","allow_proactive_msg":true,"recv_msg_setting":"all","member_role":"member"}
        """
        ...

    def set_join_request(self, appid: int, group_openid: str, user_openid: str,
                         approve: bool, request_id: str = "",
                         reject_reason: str = "", blacklist: bool = False) -> Dict:
        """
        处理加群请求（同意/拒绝）。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :param user_openid: 申请用户的 openid
        :param approve: True=同意，False=拒绝
        :param request_id: 请求 ID 必填 使用 msg.callbackid 即可
        :param reject_reason: 拒绝理由（可选）
        :param blacklist: 是否拉黑该用户（可选，默认 False）
        :return: 操作结果（JSON 字符串） 成功返回 {} 失败 返回 {"message":"xxx"}
        """
        ...

    def get_join_request_list(self, appid: int, group_openid: str) -> Dict:
        """
        获取指定群的加群请求列表。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return: {"list":[{"join_request_id":"xxx","risk_tips":"","union_openid":"xxx","member_openid":"xxx","username":"๑҉环绕᭄ꦿ໌້ᮨ","apply_at":"2026-08-10T23:06:21+08:00","apply_source":"self_apply","invited_by":"","bot":false,"verify_info":{"method":"admin_review_qa","verify_message":"","review_qa_list":[{"question":"1","answer":"1"}]}}],"next_cursor":"1785949822131345"}
        """
        ...

    def set_mute_g(self, appid: int, group_openid: str, members: Union[List[Dict], str]) -> Dict:
        """
        设置群内成员禁言（批量）。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :param members: 禁言设置列表（JSON 数组字符串 或 列表对象），
                        每个元素包含 user_openid 和 duration_seconds 等字段。
        :return: 操作结果（JSON 字符串） 成功返回 {} 败 返回 {"message":"xxx"}
        """
        ...

    def get_mute_list_g(self, appid: int, group_openid: str) -> Dict:
        """
        获取指定群的禁言成员列表。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return: {"global_rule":{"mode":"none","schedule_rules":[],"recurring_rules":[]},"members":[{"member_openid":"xxx","mute_expire_at":"2026-08-11T11:02:12+08:00","username":"云猫猫💐","union_openid":"xxx"}]} #注意 union_openid 部分机器人可能是空 但是优先使用
        """
        ...


class ButtonGroup:
    def __init__(self):
        self.rows = [[]]          # 二维列表，每个元素是一个按钮字典
        self.row = 0
        self.col = 0

    def _random_id(self, length=8):
        return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

    def add(self,
            name: str,                          # 按钮显示文本
            data: str,                          # 点击后数据 如果action_type 是2将本内容插入到聊天框 如果action_type=0 将转跳到网页
            action_type: int = 2,               # 动作类型：0=链接, 1=回调, 2=发送（默认2）
            btn_id: str = None,                 # 按钮唯一ID，不传则自动生成
            enter: bool = False,                # 是否立即发送（点击后立即执行）
            reply: bool = False,                # 是否引用原消息
            color: int = 2,                     # 按钮样式 0-4 默认2
            permission_type: int = 2,           # 权限类型：0=部分人, 1=管理员, 2=全部（默认2）
            specify_users: list = None,         # 指定可用用户列表（permission_type=0时有效） -->ev.user 获取
            visited_label: str = "visited",     # 回调后按钮显示的文本（默认"visited"）
            unsupport_tip: str = None,          # 不支持时的提示文本（回调code4弹窗）
            modal_content: str = None,          # 确认框内容（最多40字符）
            modal_confirm: str = None,          # 确认按钮文本（最多4字符）
            modal_cancel: str = None,           # 取消按钮文本（最多4字符）
            subscribe_id: int = None,           # 订阅模板ID（整数）
            custom_subscribe_id: str = None):   # 自定义订阅模板ID（字符串）
        ...

    def newrow(self):
        """换行，列归零"""
        self.row += 1
        self.col = 0
        # 预先创建空行（防止索引越界）
        while len(self.rows) <= self.row:
            self.rows.append([])

    def to_json(self, indent=None) -> str:
        """输出 JSON 字符串，顶层包含 content.rows"""
        ...
)";



void AppWindow::addMessage(const QString &text, MessageType type)
{
    if (text.trimmed().isEmpty()) return;
    QString cardHtml = buildMessageHtml(text, type);

    QTextCursor cursor(chatTextEdit->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(cardHtml);
    chatTextEdit->moveCursor(QTextCursor::End);
    chatTextEdit->ensureCursorVisible();
}

QString formatTokens(int count) {
    if (count >= 1'000'000) {
        return QString::number(count / 1'000'000.0, 'f', 4) + "M";
    } else if (count >= 1'000) {
        return QString::number(count / 1'000.0, 'f', 4) + "k";
    } else {
        return QString::number(count);
    }
}
void AppWindow::addMessage2(const QString &text, MessageType type,const QString &toolname)
{
    if (text.trimmed().isEmpty()) return;


    QString cardHtml = buildMessageHtml2(text, type,toolname);
    QTextCursor cursor(chatTextEdit->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(cardHtml);
    chatTextEdit->moveCursor(QTextCursor::End);
    chatTextEdit->ensureCursorVisible();
    statusBar()->showMessage(
        QString("输入:%1  补全:%2  缓存:%3  |  输入:%4  补全:%5  缓存:%6")
            .arg(formatTokens(m_prompt_tokens), formatTokens(m_completion_tokens),
                 formatTokens(m_prompt_tokens_details), formatTokens(m_prompt_tokens2),
                 formatTokens(m_completion_tokens2), formatTokens(m_prompt_tokens_details2))
        );
}
bool confirmCommandExecutionGui(const QString &model,const QString &cmd, QWidget *parent = nullptr) {
    QDialog dialog(parent);
    dialog.setWindowTitle("⚠️ 确认执行系统命令");
    dialog.setMinimumSize(900, 500);
    dialog.setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);

    // 顶部提示
    QLabel *label = new QLabel(
        "AI 请求执行以下命令，请仔细核对。建议先点击「AI检查」辅助判断："
        );
    label->setWordWrap(true);
    mainLayout->addWidget(label);

    // ----- 命令显示区域 -----
    QPlainTextEdit *textEdit = new QPlainTextEdit();
    textEdit->setPlainText(cmd);
    textEdit->setReadOnly(true);
    textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);  // 长命令不换行，横向滚动
    QFont font("Courier New", 10);
    font.setStyleHint(QFont::Monospace);
    textEdit->setFont(font);
    textEdit->setMinimumHeight(150);
    mainLayout->addWidget(textEdit, 1);  // 拉伸占满

    // ----- AI 检查结果显示区域 -----
    QLabel *aiStatusLabel = new QLabel("🤖 AI检查状态：尚未检查");
    aiStatusLabel->setStyleSheet("padding: 5px; background-color: #f0f0f0; border-radius: 3px;");
    aiStatusLabel->setWordWrap(true);
    mainLayout->addWidget(aiStatusLabel);

    // ----- 底部按钮布局 -----
    QHBoxLayout *btnLayout = new QHBoxLayout();

    // 1. 复制按钮
    QPushButton *copyBtn = new QPushButton("📋 复制命令");
    QObject::connect(copyBtn, &QPushButton::clicked, [&cmd]() {
        QApplication::clipboard()->setText(cmd);
    });

    QPushButton *aiCheckBtn = new QPushButton("🤖 AI检查");
    QObject::connect(aiCheckBtn, &QPushButton::clicked, [=]() {
        // 1. 禁用按钮，防止重复点击
        aiCheckBtn->setEnabled(false);
        aiCheckBtn->setText("⏳ 检查中...");
        aiStatusLabel->setText("🤖 AI检查状态：正在调用模型...");
        aiStatusLabel->setStyleSheet("padding: 5px; background-color: #fff3cd; border-radius: 3px;");

        // 用 QPointer 保护按钮和标签（它们是 QObject 子类，会被 dialog 析构时删除）
        QPointer<QPushButton> btnPtr = aiCheckBtn;
        QPointer<QLabel> labelPtr = aiStatusLabel;

        // 启动后台线程
        std::thread([=]() {
            // 【子线程】执行耗时 AI 检查
            QString result = ai_ui->Ai_post(
                model,
                "你的主要任务是 检查下面 代码是否危害系统 比如格式化 删除文件 或者 覆盖写入系统文件 "
                "\n请返回 如果无危险返回【无危险】 如果可能有问题返回【危险】并且给理由 当然还可以输出其他内容 因为用户根据你说的进行判断\n\n" + cmd,
                0
                );


            QMetaObject::invokeMethod(qApp, [=]() {
                // 检查控件是否还存在（若对话框已关闭，则二者为 null）
                if (btnPtr.isNull() || labelPtr.isNull()) {
                    return; // 对话框已销毁，忽略结果
                }

                // 恢复按钮
                btnPtr->setEnabled(true);
                btnPtr->setText("🤖 AI检查");

                // 更新标签
                labelPtr->setText("🤖 AI检查结果：" + result);

                // 根据结果变色
                if (result.contains("高危") || result.contains("危险")) {
                    labelPtr->setStyleSheet("padding: 5px; background-color: #ffcccc; border-radius: 3px; color: #cc0000;");
                } else if (result.contains("中危")) {
                    labelPtr->setStyleSheet("padding: 5px; background-color: #fff3cd; border-radius: 3px; color: #856404;");
                } else {
                    labelPtr->setStyleSheet("padding: 5px; background-color: #d4edda; border-radius: 3px; color: #155724;");
                }
            }, Qt::QueuedConnection);
        }).detach();
    });

    // 3. 确认执行按钮
    QPushButton *yesBtn = new QPushButton("✅ 确认执行");
    yesBtn->setStyleSheet("color: green; font-weight: bold;");

    // 4. 取消按钮（默认焦点）
    QPushButton *noBtn = new QPushButton("❌ 取消");
    noBtn->setDefault(true);
    noBtn->setStyleSheet("color: red; font-weight: bold;");

    // 布局组装
    btnLayout->addWidget(copyBtn);
    btnLayout->addWidget(aiCheckBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(yesBtn);
    btnLayout->addWidget(noBtn);

    mainLayout->addLayout(btnLayout);

    // ----- 信号连接 -----
    QObject::connect(yesBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(noBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // ----- 执行并返回 -----
    return dialog.exec() == QDialog::Accepted;
}
bool confirmCommandExecution(const QString &model, const QString &cmd, QWidget *parent=nullptr) {

    if(!g_不审核)
    {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            return confirmCommandExecutionGui(model, cmd, parent);
        } else {

            bool result = false;
            QEventLoop loop;
            QMetaObject::invokeMethod(qApp, [&]() {
                // 确保在主线程中执行
                result = confirmCommandExecutionGui(model, cmd, parent);
                loop.quit(); // 对话框关闭后，退出事件循环
            }, Qt::QueuedConnection);

            // 阻塞当前线程，直到 loop.quit() 被调用
            loop.exec();
            return result;
        }
    }
    return true;
}

AppWindow::AppWindow(const QString &path,QWidget *parent) : m_dir(path), QMainWindow(parent)
{
    setWindowTitle("AI生成插件 请先打开一个文件夹，如果没有你就创建一个 打开 仅限 框架目录plugin/ 或 plugins/里面 然后就可以让ai写代码了 可以编辑代码 ctrl+s 保存");
    resize(1200, 700);
    if(m_dir.isEmpty())
        m_dir = g_config["aicode_dir"].toString();
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    // --- 顶部栏 ---
    QHBoxLayout *topLayout = new QHBoxLayout();
    pathLabel = new QLabel("当前文件夹：未选择");
    pathLabel->setStyleSheet("font-weight: bold;");
    QPushButton *openBtn = new QPushButton("打开文件夹");
    connect(openBtn, &QPushButton::clicked, this, &AppWindow::openFolder);
    topLayout->addWidget(pathLabel);
    topLayout->addStretch();
    topLayout->addWidget(openBtn);
    mainLayout->addLayout(topLayout);

    // --- 三栏分割器 ---
    QSplitter *splitter = new QSplitter(Qt::Horizontal);

    fileModel = new QFileSystemModel(this);
    fileModel->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);

    // 2. 先设置模型的根路径（告诉模型去哪里加载）
    if(m_dir.isEmpty()){
        QString appDir = QCoreApplication::applicationDirPath();
        fileModel->setRootPath(appDir);
    }else
    {
        fileModel->setRootPath(m_dir);
    }
    // 3. 创建视图
    fileTree = new QTreeView();
    fileTree->setModel(fileModel);           // 【关键！】必须先设置模型
    fileTree->setFixedWidth(200);
    fileTree->setHeaderHidden(true);        // 隐藏表头（只留文件名）
    fileTree->setColumnHidden(1, true);     // 隐藏“大小”
    fileTree->setColumnHidden(2, true);     // 隐藏“类型”
    fileTree->setColumnHidden(3, true);     // 隐藏“修改日期”
    fileTree->setRootIsDecorated(false);    // 不显示根节点的展开图标（看个人喜好）
    fileTree->setFont(QFont("Segoe UI", 10));
    connect(fileTree, &QTreeView::clicked, this, &AppWindow::onFileClicked);
    splitter->addWidget(fileTree);
    QTimer::singleShot(0, this, [=]() {
        QString appDir = QCoreApplication::applicationDirPath();
        QModelIndex rootIdx;
        if(m_dir.isEmpty()){
            QString appDir = QCoreApplication::applicationDirPath();
            rootIdx = fileModel->index(appDir);
        }else
        {
            rootIdx = fileModel->index(m_dir);
        }
        if (rootIdx.isValid()) {
            fileTree->setRootIndex(rootIdx);
        } else {
            qDebug() << "根目录索引无效，可能是路径权限问题";
        }
    });


    codeEditor = new CodeEditor();
    codeEditor->setPlaceholderText("Python 代码将显示在这里……");
    codeEditor->setFont(QFont("Consolas", 10));
    splitter->addWidget(codeEditor);
    new PythonHighlighter(codeEditor->document());
    // 3. AI 聊天区


    QVBoxLayout *mainLayout2 = new QVBoxLayout(this);
    mainLayout2->setContentsMargins(2, 2, 2, 2);

    chatTextEdit = new QTextEdit(this);
    chatTextEdit->setReadOnly(true);        // 只读但允许选择复制
    chatTextEdit->setUndoRedoEnabled(false);
    //chatTextEdit->document()->setDefaultFont(QFont("Segoe UI", 11));
    chatTextEdit->setStyleSheet(
        "QTextEdit {"
        "    background: #f5f5f5;"
        "    border: none;"
        "    padding: 10px;"
        "}"
        );
    connect(chatTextEdit->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &AppWindow::onScrollChanged);
    mainLayout2->addWidget(chatTextEdit);
    // 输入区
    QHBoxLayout *inputLayout = new QHBoxLayout;
    inputLayout->setContentsMargins(0, 0, 0, 0);
    messageInput = new QTextEdit;
    messageInput->setPlaceholderText("输入消息（Ctrl+Enter发送）...");
    messageInput->setFixedHeight(70);
    // 核心修正：禁止水平滚动 + 启用 CSS 强制断行
    messageInput->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    messageInput->setLineWrapMode(QTextEdit::WidgetWidth);
    messageInput->setAcceptRichText(false);
    messageInput->setStyleSheet(
        "QTextEdit {"
        "   border: 1px solid #E7D9C8;"
        "   border-radius: 8px;"
        "   padding: 6px;"
        "   word-break: break-all;"  // 关键！让长数字也能换行
        "}"
        );
    sendBtn = new QPushButton("发送");
    sendBtn2 = new QPushButton("发送");
    clearBtn = new QPushButton("清空对话");
    inputLayout->addWidget(messageInput, 1);
    mainLayout2->addLayout(inputLayout);
    QHBoxLayout *inputLayout0 = new QHBoxLayout;
    QLabel *bq=new QLabel("模型：");
    //bq->setAlignment(Qt::AlignRight);//右对齐
    bq->setMaximumWidth(40);
    inputLayout0->addWidget(bq);
    modelCombo = new QComboBox;
    clearBtn->setMaximumWidth(100);
    sendBtn->setMaximumWidth(100);
    inputLayout0->addWidget(modelCombo);
    nosh = new QCheckBox("不审核");
    inputLayout0->addWidget(nosh);
    inputLayout0->addWidget(clearBtn);
    inputLayout0->addWidget(sendBtn2);
    inputLayout0->addWidget(sendBtn);
    mainLayout2->addLayout(inputLayout0);

    connect(nosh, &QCheckBox::clicked, this, [=]() {
        g_不审核 = nosh->isChecked();
    });
    // 信号连接
    connect(sendBtn, &QPushButton::clicked, this, [=]() {


        if(sendBtn->text()=="中断"){
            if (m_stream) {
                m_stream->userAborted = true;
                if (m_stream->reply) {
                    m_stream->reply->abort();
                }

                m_stream.reset();
                return;
            }
            return;
        }

        QString text = messageInput->toPlainText().trimmed();
        if (!text.isEmpty()) {
            messageInput->clear();
            onSendMessage_stream(text);

        }
    });
    connect(sendBtn2, &QPushButton::clicked, this, [=]() {

        QString text = messageInput->toPlainText().trimmed();
        if(sendBtn->text()=="中断"){
            if (m_stream) {
                addmsg+=text+"\n";
                messageInput->clear();
                return;
            }

        }

        addmsg.clear();
        if (!text.isEmpty()) {
            messageInput->clear();
            onSendMessage_stream(text);

        }
    });
    connect(ai_ui, &AiWidget::modelListUpdated, this, [this]() {

        QMetaObject::invokeMethod(this, [this]() {
            setModels();
        }, Qt::QueuedConnection);
    });
    setModels();
    connect(clearBtn, &QPushButton::clicked, this, &AppWindow::clearChat);


    QWidget *container = new QWidget;   // 创建一个容器窗口部件
    container->setLayout(mainLayout2);  // 将布局设置给容器
    splitter->addWidget(container);     // 将容器添加到分割器


    splitter->setSizes(QList<int>() << 200 << 500 << 500);
    mainLayout->addWidget(splitter);
    m_fun=QJsonArray();
    内置函数();

    if(!m_dir.isEmpty()) Folder();
    QShortcut *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &AppWindow::saveCurrentFile);
}

AppWindow::~AppWindow() {}

void AppWindow::openFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (dir.isEmpty()) return;
    m_dir = dir+"/";
    g_config["aicode_dir"] = m_dir;
    m_prompt_tokens=0;
    m_prompt_tokens_details=0;
    m_completion_tokens=0;
    m_prompt_tokens2=0;
    m_prompt_tokens_details2=0;
    m_completion_tokens2=0;
    saveConfig();
    Folder();
}
void AppWindow::Folder()
{
    pathLabel->setText("当前文件夹：" + m_dir);
    fileTree->setRootIndex(fileModel->setRootPath(m_dir));

    QString filePath = QDir(m_dir).filePath("ai对话.json");
    QFile file(filePath);
    bool loadSuccess = false;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error == QJsonParseError::NoError) {
            sxw = doc.object();
            loadSuccess = true;
        } else {
            sxw = QJsonObject();
        }
    } else {
        sxw = QJsonObject();
    }

    // 清空聊天显示
    chatTextEdit->clear();   // 替换原来的 chatModel->clearMessages()

    QString mode = sxw["model"].toString();
    int index = modelCombo->findText(mode);
    if (index != -1)
        modelCombo->setCurrentIndex(index);

    if (loadSuccess && sxw.contains("messages") && sxw["messages"].isArray()) {
        const QJsonArray msgs = sxw["messages"].toArray();
        QString baoliu;
        for (const QJsonValue &val : msgs) {
            QJsonObject msgObj = val.toObject();
            QString role = msgObj["role"].toString();
            QString content = msgObj["content"].toString();
            if (role == "user") {
                if(!baoliu.isEmpty())
                {
                    addMessage(baoliu,MessageType::AI);
                    baoliu.clear();
                }
                addMessage(content,MessageType::User);
            } else if (role == "assistant") {

                QString displayContent;
                bool hasContent = !content.trimmed().isEmpty();
                bool hasToolCalls = msgObj.contains("tool_calls") && msgObj["tool_calls"].isArray();

                // 提取工具名称（如果有）
                QStringList toolNames;
                if (hasToolCalls) {
                    QJsonArray calls = msgObj["tool_calls"].toArray();
                    for (const QJsonValue &callVal : std::as_const(calls)) {
                        QJsonObject callObj = callVal.toObject();
                        if (callObj.contains("function") && callObj["function"].isObject()) {
                            QJsonObject funcObj = callObj["function"].toObject();
                            QString name = funcObj["name"].toString();
                            if (!name.isEmpty())
                                toolNames << name;
                        }
                    }
                }

                if (hasContent) {
                    displayContent = content;  // 保留原始内容（包括换行）
                }
                // 如果有工具调用，将工具信息附加到内容后面（或单独显示）
                if (!toolNames.isEmpty()) {
                    QString toolMsg = "\n\n[调用工具: " + toolNames.join(", ") + "]";
                    if (hasContent) {
                        displayContent += toolMsg;
                    } else {
                        displayContent = toolMsg;  // 没有文本内容，只显示工具信息
                    }
                }

                // 如果既有内容又有工具，或者只有工具，都需要显示；如果什么都没有则跳过
                baoliu +=+"\n"+ displayContent.trimmed()+"\n";
            }else if (role == "tool") {
                    QString toolName = msgObj["name"].toString();
                    baoliu+="\n[工具返回(" + toolName + ")]：" + content.trimmed()+"\n";//删除 换行再添加 确保格式美观
            }
        }
        if(!baoliu.isEmpty())
        {
            addMessage(baoliu,MessageType::AI);
            baoliu.clear();
        }
    }
    QJsonObject usage = sxw["usage"].toObject();
    m_prompt_tokens = usage["prompt_tokens"].toInt();
    m_prompt_tokens_details = usage["prompt_tokens_details"].toInt();
    m_completion_tokens = usage["completion_tokens"].toInt();
    statusBar()->showMessage(QString("输入:%1 补全:%2 缓存:%3").arg(m_prompt_tokens).arg(m_completion_tokens).arg(m_prompt_tokens_details));
}





QString listDirectoryEntries(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) return QString();
    QStringList result;
    QFileInfoList infoList = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    // 排序：目录在前，文件在后（按名称）
    std::sort(infoList.begin(), infoList.end(),
              [](const QFileInfo& a, const QFileInfo& b) {
                  if (a.isDir() != b.isDir())
                      return a.isDir() > b.isDir();  // 目录优先
                  return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
              });

    const QString indent = "    ";  // 4 个空格
    for (const QFileInfo& info : std::as_const(infoList)) {
        QString name = info.fileName();
        // 过滤不需要的文件
        if (name == "ai对话.json" || name.endsWith(".tmp", Qt::CaseInsensitive))
            continue;

        QString line =   name;
        if (info.isDir()) {
            line += " <DIR>";
        } else if (info.isFile()) {
            // 显示文件大小
            line += QString(" (%1 bytes)").arg(info.size());
        }
        result.append(line);

        // 如果是 .py 文件，提取函数和类定义
        if (info.isFile() && name.endsWith(".py", Qt::CaseInsensitive)) {
            QFile file(info.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray data = file.readAll();
                file.close();
                // 使用 QString::fromUtf8 直接转换（Qt 5/6 通用）
                QString content = QString::fromUtf8(data);

                // 正则匹配 def 或 class 定义（支持 async def）
                QRegularExpression regex(R"(^\s*(?:async\s+)?(def|class)\s+(\w+)\s*[\(:]?)");
                QStringList pyLines;
                for (const QString& line : content.split('\n')) {
                    QRegularExpressionMatch match = regex.match(line);
                    if (match.hasMatch()) {
                        // 提取类型和名称，例如 "def on_message" 或 "class MyClass"
                        QString type = match.captured(1);
                        QString name = match.captured(2);
                        pyLines.append(indent + type + " " + name);
                    }
                }
                if (!pyLines.isEmpty()) {
                    result.append(pyLines);
                }
            }
        }
    }

    return result.join("\n");
}
void AppWindow::saveCurrentFile()
{
    if (currentFilePath.isEmpty()) {
        statusBar()->showMessage("没有打开的文件可保存", 2000);
        return;
    }

    QFile file(currentFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusBar()->showMessage("无法写入文件：" + file.errorString(), 3000);
        return;
    }

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    out << codeEditor->toPlainText();
    file.close();

    statusBar()->showMessage("文件已保存：" + QFileInfo(currentFilePath).fileName(), 2000);
}
void AppWindow::onFileClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    QFileInfo fileInfo = fileModel->fileInfo(index);

    // 单击保存上一个选中的文件（仅在选中文件时记录）
    if (fileInfo.isFile()) {
        if (!lastSelectedFile.isEmpty()) {
            statusBar()->showMessage("上次选中文件：" + lastSelectedFile);
        }
        lastSelectedFile = fileInfo.fileName();
    }

    // 如果是文件夹，仅显示路径
    if (fileInfo.isDir()) {
        codeEditor->setPlainText("当前选中目录：" + fileInfo.absoluteFilePath());
        currentFilePath.clear();   // 清空，避免误保存
        return;
    }

    // 1. 文件大小限制：超过 300KB 不予加载
    if (fileInfo.size() > 300 * 1024) {
        codeEditor->setPlainText(QString("错误：文件大小为 %1 KB，超过 300KB 预览限制，无法打开。").arg(fileInfo.size() / 1024));
        return;
    }

    // 2. 允许预览的文本扩展名列表
    static const QSet<QString> allowedExtensions = {".py", ".txt", ".log", ".json", ".ini", ".xml",".html",".js",".css",".h",".hpp",".c",".cpp"};

    // 获取小写后缀（带点）
    QString suffix = "." + fileInfo.suffix().toLower();

    if (allowedExtensions.contains(suffix)) {
        // 如果后缀在白名单内，读取文件内容
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            // 强制 UTF-8 编码
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            stream.setEncoding(QStringConverter::Utf8);
#else
            stream.setCodec("UTF-8");
#endif
            currentFilePath = fileInfo.absoluteFilePath();   // <-- 添加这一行
            codeEditor->setPlainText(stream.readAll());
            file.close();
        } else {
            codeEditor->setPlainText("无法读取文件内容（权限或文件被占用）");
        }
    } else {
        // 如果后缀不在白名单内，只显示路径，防止二进制乱码
        codeEditor->setPlainText("非文本预览文件（仅支持 .py .txt .log .json .ini .xml），仅显示路径：" + fileInfo.absoluteFilePath());
    }
}

void AppWindow::内置函数(const QString &Nmae,const QString &remark,const QStringList &params)
{

    QJsonObject functionObj;
    functionObj["name"] = Nmae;
    functionObj["description"] = remark;

    QJsonObject parameters;
    parameters["type"] = "object";
    QJsonObject properties;
    QJsonArray required;

    int paramIndex = 1;
    for (const QString &paramName : params) {
        if (paramName.isEmpty()) {
            break;
        }
        QString pKey = QString("p%1").arg(paramIndex);
        QJsonObject paramDef;
        paramDef["type"] = "string";
        paramDef["description"] = paramName;
        properties[pKey] = paramDef;
        required.append(pKey);
        ++paramIndex;
    }

    if (!properties.isEmpty()) {
        parameters["properties"] = properties;
        parameters["required"] = required;
        functionObj["parameters"] = parameters;
    } else {
        functionObj["parameters"] = QJsonObject(); // 无参数时留空对象
    }

    QJsonObject toolObj;
    toolObj["type"] = "function";
    toolObj["function"] = functionObj;
    m_fun.append(toolObj);
}

void AppWindow::内置函数()
{

    内置函数("w_file", "将文本内容写入到指定的文件中，如果文件不存在则创建", {"文件路径 支持不存在的文件夹 如 xx/a.py 不存在就创建", "文件内容"});
    内置函数("read_file", "读取指定文件的内容并以文本形式返回", {"文件路径"});
    内置函数("delete_file", "删除某个文件", {"文件路径"});
    内置函数("append_file", "追加内容", {"文件路径","追加文本"});

    内置函数("LoadPlugin", "添加当前插件到框架 如果不存在就添加 存在就重载 ", {"无参数"});
    内置函数("uninstall_Plugin", "卸载当前插件", {"无参数"});
    内置函数("testplugin", "测试插件 测试指令是否正确返回", {"测试的指令","事件 本项可空 如 GROUP_MEMBER_ADD"});
    内置函数("run_python", "执行python代码 捕获print 日志", {"python代码 注意默认路径是在 exe路径"});
    内置函数("pypip", "pip 下载某python些库 请将需要安装的库写到 requirements.txt 文件", {"无参数"});

    内置函数("getFunctionCode", "读取某个py文件的某个函数代码", {"py文件名","函数名 如 my_function","类名 如果有 没有可空 如 MyClass"});
    内置函数("replaceFunction", "替换个py文件的某个函数代码", {"py文件名","函数名 如 my_function","替换的代码 如","类名 如果有 没有可空 如 MyClass"});


    内置函数("byss","必应搜索",QStringList() << "搜索关键词 如 原神"<<"页码 1开始 如 1");
    内置函数("llwye","浏览网页 返回提取后的文本 当用户发送链接时 可以使用 也可以请求某些api 当用户让你帮我对接api时",QStringList() << "链接 如 https://www.baidu.com");
    内置函数("exec_cmd", "在操作系统命令行中执行一个指令，并返回执行结果 注意 可能无法执行python 指令", {"执行命令 注意已经自动 cd 到 py文件目录 非exe目录"});

}

QString onMessageReceived2(const MessageEvent &msg,int i);

QString browseWeb(const QString &urlString);

QString AppWindow::tools_fun(const QString &tool_name, const QString &args, const QString &model)
{
    // 1. 解析参数
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(args.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        return "错误：工具参数解析失败，JSON 格式不正确。";
    }
    QJsonObject obj = doc.object();

    // 2. 确保工作目录有效
    if (m_dir.isEmpty()) {
        return "错误：当前未打开任何文件夹，无法执行文件操作或命令。";
    }

    QString result;
    if (tool_name == "w_file") {
        // ---------- 写文件 ----------
        QString fileName = obj["p1"].toString();
        QString content = obj["p2"].toString();
        if (fileName.isEmpty()) return "错误：写文件必须提供文件路径 (p1)";

        QString fullPath = QDir(m_dir).filePath(fileName);

        // 【新增】确保目标文件的父目录存在
        QDir parentDir = QFileInfo(fullPath).dir();
        if (!parentDir.exists()) {
            if (!parentDir.mkpath(".")) {
                return "错误：无法创建目录 " + parentDir.path();
            }
        }

        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return "错误：无法打开或创建文件 " + fileName + "。请检查权限或路径。";
        }
        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif
        out << content;
        file.close();
        result = "写入成功，已保存至文件：" + fileName;

    } else if (tool_name == "read_file") {
        // ---------- 读文件 ----------
        QString fileName = obj["p1"].toString();
        if (fileName.isEmpty()) return "错误：读文件必须提供文件路径 (p1)";

        QString fullPath = QDir(m_dir).filePath(fileName);
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return "错误：无法打开文件 " + fileName + "。文件可能不存在或权限不足。";
        }
        QTextStream in(&file);
        // 【关键修改】强制 UTF-8 编码读取
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        result = in.readAll();
        file.close();

    } else if (tool_name == "exec_cmd") {
        QString cmd = obj["p1"].toString();
        if (cmd.isEmpty()) {
            return "错误：执行命令不能为空 (p1)";
        }
        if (!confirmCommandExecution(model,cmd, this)) {
            return "管理员阻止了本次cmd的执行 可能误点 或者代码可能危害系统";
        }
        QProcess process;
        process.setWorkingDirectory(m_dir);


#if defined(Q_OS_WIN)
        QStringList args;
        args << "/c" << cmd;  // cmd 是你原本的命令字符串（如 'python -m pip install ...'）
        process.start("cmd.exe", args);
#else
        process.start(cmd);
#endif

        if (!process.waitForStarted(5000)) {
            return "错误：执行命令超时或无法启动。";
        }

        // 等待进程运行结束（30秒超时）
        if (!process.waitForFinished(30000)) {
            process.kill(); // 超时强行杀进程
            return "错误：命令执行时间过长（超过30秒），已强制终止。";
        }
        QByteArray stdoutData = process.readAllStandardOutput();
        QByteArray stderrData = process.readAllStandardError();

        // 组装返回结果
        QString result = "命令执行完毕。\n";
        if (!stdoutData.isEmpty()) {
            // 用 fromLocal8Bit 解决 Windows 下 CMD 输出中文乱码问题
            result += "【标准输出】\n" + QString::fromLocal8Bit(stdoutData) + "\n";
        }
        if (!stderrData.isEmpty()) {
            result += "【标准错误】\n" + QString::fromLocal8Bit(stderrData) + "\n";
        }
        if (stdoutData.isEmpty() && stderrData.isEmpty()) {
            result += "无输出内容。";
        }

        return result; // 直接 return 结果，避免后续继续拼接
    }else if(tool_name == "byss")
    {
        int y = obj["p2"].toInt();
        if(y<=0) y=1;
        result = browseWeb("https://cn.bing.com/search?q="+ QUrl::toPercentEncoding(obj["p1"].toString()) +"&first="+QString::number(y*10));
    }else if(tool_name == "llwy")
        result = browseWeb(obj["p1"].toString());
    else if (tool_name == "delete_file") {
        QString filePath = obj["p1"].toString();
        if (filePath.isEmpty()) {
            return "错误：文件路径不能为空 (p1)";
        }

        // 基于 m_dir 转为绝对路径（若已是绝对路径则保持不变）
        QString absPath = QDir(m_dir).absoluteFilePath(filePath);
        QFileInfo info(absPath);
        if (!info.exists()) {
            return "错误：文件不存在：" + absPath;
        }
        if (!info.isFile()) {
            return "错误：路径指向的不是一个文件：" + absPath;
        }

        QFile file(absPath);
        if (!file.remove()) {
            return "错误：删除文件失败：" + file.errorString();
        }
        return "文件删除成功：" + filePath;
    }
    else if (tool_name == "append_file") {
        QString filePath = obj["p1"].toString();
        QString content = obj["p2"].toString();   // 内容从 p2 获取
        if (filePath.isEmpty()) {
            return "错误：文件路径不能为空 (p1)";
        }

        // 基于 m_dir 转为绝对路径
        QString absPath = QDir(m_dir).absoluteFilePath(filePath);
        QFile file(absPath);

        // 以追加模式打开（若文件不存在则自动创建）
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            return "错误：无法打开文件进行追加写入：" + file.errorString();
        }

        QTextStream out(&file);
        // 推荐指定编码为 UTF-8，避免中文乱码（与你的 exec_cmd 中 fromLocal8Bit 一致）
        out.setCodec("UTF-8");
        out << content;   // 不加换行，若需要换行请追加 "\n"
        file.close();

        return "内容追加成功：" + absPath;
    }
    else if(tool_name == "LoadPlugin"){
        QString path = subTextReplace(m_dir, QCoreApplication::applicationDirPath()+"/","");
        int index = pluginPage->findPluginIndex(path);
        if(index!=-1)
        {
            AppendEventLog("[重载插件]"+m_pluginList[index].name);
            bool enabled = m_pluginList[index].enabled;
            if (m_pluginList[index].enabled) pluginPage->disable_Plugin(m_pluginList[index]);//调禁用

            pluginPage->uninstall_Plugin(m_pluginList[index]);//里面会重置enabled 变量
            m_pluginList[index].enabled = enabled;

            QString err;
            py::gil_scoped_acquire gil;
            if (m_pluginList[index].type==0)
            {
                err = pluginPage->LoadPlugin_py(m_pluginList[index]);
            }
            if(err.isEmpty())
            {
                QMetaObject::invokeMethod(qApp, [=]() {
                    pluginPage->updatePluginItemInUI(index);
                }, Qt::QueuedConnection);
                if(m_pluginList[index].enabled){
                    if(m_pluginList[index].python.onEnable)
                        m_pluginList[index].python.onEnable();
                }
                return "重载成功 注意 热重载有很多问题 你可能需要让用户重启 才能让代码完整";
            }
            err ="[重载插件]"+m_pluginList[index].name+" 失败 错误信息:"+err+"\n注意 热重载有很多问题 你可能需要让用户重启 才能让代码完整";
            AppendEventLog(err);
            QMetaObject::invokeMethod(qApp, [=]() {
                pluginPage->removePlugin(index);
            }, Qt::QueuedConnection);

            return err;
        }
        QList<int> arr;
        QString err =pluginPage->LoadPlugin(path,0,false,arr);

        if(err.isEmpty()){
            pluginPage->savePlugins();
            err = "载入成功";
            index = pluginPage->findPluginIndex(path);
            if(index!=-1){
                if(m_pluginList[index].enabled){
                    if(m_pluginList[index].python.onEnable)
                        m_pluginList[index].python.onEnable();
                }
            }

        }
        return err;
    }
    else if(tool_name == "uninstall_Plugin"){
        QString path = subTextReplace(m_dir, QCoreApplication::applicationDirPath()+"/","");
        int index = pluginPage->findPluginIndex(path);
        if(index!=-1)
        {
            QMetaObject::invokeMethod(qApp, [=]() {
                pluginPage->uninstall_Plugin2(index);
                pluginPage->savePlugins();
            }, Qt::QueuedConnection);

            return "卸载成功 注意 热重载有很多问题 你可能需要让用户重启 才能让代码完整";
        }

        return "当前插件未载入 不能卸载";
    }
    else if(tool_name == "testplugin"){
        QString path = subTextReplace(m_dir, QCoreApplication::applicationDirPath()+"/","");
        int index = pluginPage->findPluginIndex(path);
        if(index!=-1)
        {
            MessageEvent ev;
            ev.appid=10020001;
            ev.groupId = "A9F143DA1BB80A104EE08554CB8399BA";
            ev.user = "A9F143DA1BB80A104EE08554CB8399BA";
            ev.user_int = 1;
            ev.at_you = true;
            ev.msg = obj["p1"].toString();
            ev.msgId = "msgid";
            ev.nickname = "test";
            ev.msgType = obj["p2"].toString();
            return "测试返回结果(如果是空代表没返回)：" + onMessageReceived2(ev,index);
        }

        return "当前插件未载入 不能测试";
    } else if(tool_name == "run_python"){
        QString py_code = obj["p1"].toString();
        if (!confirmCommandExecution(model,py_code, this)) {
            return "管理员阻止了本次 py_code 的执行 可能误点 或者代码可能危害系统";
        }
        return python_code(py_code);
    }
    else if(tool_name == "getFunctionCode"){
        QString pyfile_name = obj["p1"].toString();
        QString myfun =  obj["p2"].toString();
        QString myclass =  obj["p3"].toString();
        if(pyfile_name.isEmpty()) return "参数1路径为空";
        pyfile_name = m_dir+pyfile_name;
        return getFunctionCode(pyfile_name,myfun,!myclass.isEmpty(),myclass);
    }
    else if(tool_name == "replaceFunction"){
        QString pyfile_name = obj["p1"].toString();
        QString myfun =  obj["p2"].toString();
        QString py_code = obj["p3"].toString();
        QString myclass =  obj["p4"].toString();
        if(pyfile_name.isEmpty()) return "参数1路径为空";
        pyfile_name = m_dir+pyfile_name;
        // 1. 创建备份
        QString backupPath = pyfile_name + ".tmp";
        if (QFile::exists(backupPath)) QFile::remove(backupPath);
        if (!QFile::copy(pyfile_name, backupPath)) {
            return "无法创建备份文件: " + backupPath;
        }

        // 2. 在备份上执行替换
        QString err;
        bool ok = replaceFunction(backupPath, myfun, py_code, !myclass.isEmpty(), myclass,&err);
        if (!ok) {
            QFile::remove(backupPath);
            return "替换失败 可能寻找函数语法错误 请使用其他工具替换 正确使用方法 参数1 py文件名 参数2 函数名 参数3 替换到的函数 参数4 类名(如果有)";
        }

        // 3. 语法检查（尝试多种编码）
        QString script = QString(
                             "import sys, ast\n"
                             "def check_syntax(filename):\n"
                             "    encodings = ['utf-8', 'gbk', 'gb2312', 'latin-1']\n"
                             "    content = None\n"
                             "    for enc in encodings:\n"
                             "        try:\n"
                             "            with open(filename, 'r', encoding=enc) as f:\n"
                             "                content = f.read()\n"
                             "            break\n"
                             "        except UnicodeDecodeError:\n"
                             "            continue\n"
                             "    if content is None:\n"
                             "        print('Error: Cannot decode file with known encodings')\n"
                             "        return\n"
                             "    try:\n"
                             "        ast.parse(content)\n"
                             "        print('SYNTAX_OK')\n"
                             "    except SyntaxError as e:\n"
                             "        print(f'SyntaxError: {e}')\n"
                             "    except Exception as e:\n"
                             "        print(f'Error: {e}')\n"
                             "check_syntax(r'%1')\n"
                             ).arg(backupPath);

        QString result = python_code(script);
        if (result.contains("SYNTAX_OK")) {
            // 4. 语法通过，覆盖原文件
            if (!QFile::remove(pyfile_name)) {
                if (!QFile::rename(backupPath, pyfile_name)) {
                    return "替换成功但无法覆盖原文件，请检查权限，备份保留在 " + backupPath;
                }
            } else {
                if (!QFile::rename(backupPath, pyfile_name)) {
                    return "替换成功但无法重命名备份为原文件，备份位于 " + backupPath;
                }
            }
            QFile::remove(backupPath);
            return "替换成功 语法检查通过";
        } else {
            // 5. 语法不通过，删除备份，保留原文件
            QFile::remove(backupPath);
            return "替换后语法检查 不通过 返回：" + result + "\n本次文件未替换 请更新数据后尝试";
        }
    }
    else if(tool_name == "pypip")
    {

        QString pythonExe = QCoreApplication::applicationDirPath() + "/python3.14t.exe"; //绝对路径 防止跑去跑系统的
        QString cmdLine = QString("cmd /c start \"pip install\" cmd /k \"echo 欢迎使用插件依赖安装工具 & echo 提示： "
                          "& echo   - \"Requirement already satisfied\" 表示库已存在，无需重复下载 "
                          "& echo   - \"Successfully installed\" 表示新库安装成功 "
                          "& echo. & \"%1\" -m pip install -r \"%2\" & echo. & echo 安装完成，请检查上述输出，然后关闭此窗口.\"")
                      .arg(pythonExe,m_dir+"requirements.txt");

        QProcess::startDetached(cmdLine);
        return "由于下载是异步 这里不返回结果 请继续执行 如果你要查看结果 需要 执行 cmd 命令 查看";
    }
    else {
        result = "错误：未知的工具名称 " + tool_name;
    }

    return result;
}

bool matchRule(const Rule &rule, const MessageEvent &ev);

QString onMessageReceived2(const MessageEvent &msg,int i) {

    try {
        py::gil_scoped_acquire gil;

        QString reply;
        // 1. 优先按规则匹配
        for (const Rule &rule : std::as_const(m_pluginList[i].python.rules)) {
            if (matchRule(rule, msg)) {
                // 调用规则对应的函数
                py::object ret = rule.function(msg);
                if (!ret.is_none() && py::isinstance<py::str>(ret)) {
                    if(reply.isEmpty())
                        reply = QString::fromStdString(py::str(ret).cast<std::string>());
                    else
                        reply += "\n" + QString::fromStdString(py::str(ret).cast<std::string>());
                }
            }
        }

        return reply;


    } catch (const std::exception &e) {
        return "[Python] " + m_pluginList[i].name + " 错误: " + e.what();
    } catch (...) {
        return "[Python] " + m_pluginList[i].name + " 未知错误";
    }

}

void AppWindow::setModels()
{
    QString currentText = modelCombo->currentText();  // 假设 comboModel 是 QComboBox*
    modelCombo->clear();
    for (const auto &m : std::as_const(ai_ui->modelList))
    {
        modelCombo->addItem(m.name);
    }
    int index = modelCombo->findText(currentText);
    if (index != -1)
        modelCombo->setCurrentIndex(index);
}

void AppWindow::clearChat()
{
    if(m_dir.isEmpty()) return;
    if(m_stream)
    {
        QMessageBox::warning(this,"无法清空","清空上下文需要 等ai请求接收才能清空 请点击中断后再试试");
        return;
    }
    sxw.remove("messages");
    W_file(m_dir+"/ai对话.json",QJsonDocument(sxw).toJson(QJsonDocument::Indented));
    chatTextEdit->clear();
}

QString AppWindow::Ai_posts(const QString &model) //内部使用请勿公开
{
    QString err;
    for ( const auto &m : std::as_const(ai_ui->modelList))
    {
        if(m.name==model)
        {
            sxw["model"] = m.name;
            int kswz = m.enabledInterfaceIndices.size();
            for(int i = 0;i<kswz;++i)//接口循环
            {
                //模型开始下标 原子+1
                int jk= model_index % m.enabledInterfaceIndices.size();//实时获取
                model_index++;
                auto &key = ai_ui->globalInterfaces[jk].keys;
                int len = key.size();
                for(int i2=0;i2< len;++i2)
                {
                    int index =  ai_ui->globalInterfaces[jk].key_index++;
                    index = index % len;
                    if(qxzd) return QString();
                    QString text =  Ai_post(ai_ui->globalInterfaces[jk].url,key[index].key,err);
                    if(text.isEmpty()) continue;
                    return text;
                }

            }
            return err;
        }
    }

    return "未找到："+model+" 模型 请重新打开本页面";
}

void AppWindow::onSendMessage(const QString &text)
{
    if(m_run)
    {
        qxzd = true;
        return ;
    }
    if(m_dir.isEmpty())
    {
        QMessageBox::warning(this, "", "请先打开一个文件夹，因为本 AI 对话会涉及操作文件");
        return;
    }
    m_run = true;
    sendBtn->setText("中断");
    // 2. 界面显示用户消息
    addMessage(text,  MessageType::User);


    if (sxw.contains("messages") && sxw["messages"].isArray()) {
        init_system(text);
    } else {
        // 如果 sxw 还没有 messages 数组，则直接创建一个新的
        QJsonArray msgs;

        // 1. 先添加系统提示词（必须用独立的 QJsonObject）
        if (!g_system.isEmpty()) {
            QJsonObject systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] =  subTextReplace(g_system,"{{文件目录}}",listDirectoryEntries(m_dir));
            msgs.append(systemMsg);
        }

        // 2. 再添加用户消息（用独立的 QJsonObject，避免覆盖）
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = text;
        msgs.append(userMsg);

        sxw["messages"] = msgs;
    }
    ai_ui->trimToolResponses(sxw, 5, 64); //限制工具长度
    ai_ui->trimContextByMessageCount(sxw, 128); //限制上下文
    QString model = modelCombo->currentText();
    sxw["tools"] = m_fun;
    m_execThread = QThread::create([this, model]() {
        QString aiReply = Ai_posts(model);
        sxw.remove("tools");
         addMessage(aiReply, MessageType::AI);
        QMetaObject::invokeMethod(this, [this, aiReply]() {

            W_file(m_dir + "/ai对话.json", QJsonDocument(sxw).toJson());
            m_run=false;
            qxzd=false;
            sendBtn->setText("发送");
            m_execThread->deleteLater();
            m_execThread = nullptr;
        }, Qt::QueuedConnection);
    });
    m_execThread->start();
}


void AppWindow::init_system(const QString &text)
{
    QJsonArray msgs = sxw["messages"].toArray();

    bool systemFound = false;

    for (int i = 0; i < msgs.size(); ++i) {
        QJsonObject msg = msgs[i].toObject();
        if (msg["role"].toString() == "system") {
            // 使用当前最新的目录列表更新 content
            msg["content"] = subTextReplace(g_system, "{{文件目录}}", listDirectoryEntries(m_dir));
            msgs[i] = msg;
            systemFound = true;
            break; // 通常只有一个 system，找到就停
        }
    }

    if (!systemFound && !g_system.isEmpty()) {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = subTextReplace(g_system, "{{文件目录}}", listDirectoryEntries(m_dir));
        msgs.insert(0, systemMsg); // 插在最前面
    }
    if(!text.isEmpty())
    {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = text;
        msgs.append(userMsg);
        addMessage2(text, MessageType::User);
    }
    sxw["messages"] = msgs;
}

QString AppWindow::Ai_post(const QString &url, const QString &key,QString &err)
{
    for(int i=0; i<50; ++i) {
        QJsonObject obj;
        for(int i2=0; i2<3; ++i2) {
            //qDebug() << "上下文" << sxw;
            init_system("");//更新系统提示词
            QByteArray response = ai_ui->Ai_post3(url, key, sxw, 1800000);
            if(response.isEmpty()) {
                err += "接口返回空\n";
                return QString();
            }

            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(response, &error);
            if (error.error != QJsonParseError::NoError) {
                err += "接口返回错误json:" + error.errorString()+"\n";
                if(err.contains(key)) err = subTextReplace(err, key, "...");
                return QString();
            }
            obj = doc.object();

            QJsonObject obj2 = obj["error"].toObject();
            QString error_mes = obj2["message"].toString();
            if(error_mes.contains("token")) {
                if (sxw.contains("messages") && sxw["messages"].isArray()) {
                    QJsonArray msgs = sxw["messages"].toArray();
                    if (msgs.size() > 1) {
                        msgs.removeAt(1);
                        sxw["messages"] = msgs;
                    }
                }
                continue;
            }
            if(!error_mes.isEmpty())
            {
                err += error_mes+"\n";
                return QString();
            }
            break;
        }

        QJsonArray arr = obj["choices"].toArray();
        if (arr.isEmpty()) {
            err += "返回的 choices 为空 请确认接口是否正确\n";
            return QString();
        }
        QJsonObject o2 = obj["usage"].toObject();
        int prompt = o2["prompt_tokens"].toInt();
        int completion = o2["completion_tokens"].toInt();
        QJsonObject obj2 = arr.at(0).toObject();
        QJsonObject obj3 = obj2["message"].toObject();
        QString text = obj3["content"].toString();
        const QJsonArray arr2 = obj3["tool_calls"].toArray();
        obj3.remove("reasoning_content");

        if (sxw.contains("messages") && sxw["messages"].isArray()) {
            QJsonArray msgs = sxw["messages"].toArray();
            msgs.append(obj3);
            sxw["messages"] = msgs;
        }
        bool ok = false;
        if (!arr2.isEmpty()) {


            for (const QJsonValue &value : arr2) {
                QJsonObject a = value.toObject();
                QJsonObject function = a["function"].toObject();
                QString tool_name = function["name"].toString();
                QString args = function["arguments"].toString();
                QString callID = a["id"].toString();
                QString data = tools_fun(tool_name,args,sxw["model"].toString());
                if(data.isEmpty())
                {
                    data = "调用函数完成 无返回值...";
                }
                if (!text.isEmpty()) {
                    addMessage(text +"\n调用工具："+tool_name +"\n\n参数："+args+"\n\n返回结果："+data+"\n\n输入:"+QString::number(prompt)+" 补全:"+QString::number(completion),  MessageType::AI);
                    text = QString();
                }else
                    addMessage("调用工具："+tool_name +"\n\n参数："+args+"\n\n返回结果："+data+"\n\n输入:"+QString::number(prompt)+" 补全:"+QString::number(completion),  MessageType::AI);
                if(!data.isEmpty())
                {
                    QJsonArray msgs = sxw["messages"].toArray();
                    QJsonObject toolMsg;
                    toolMsg["role"] = "tool";
                    toolMsg["content"] = data;
                    toolMsg["tool_call_id"] = callID;
                    toolMsg["name"] = tool_name;

                    msgs.append(toolMsg);
                    sxw["messages"] = msgs;
                    ok = true;
                }
            }
            if (!text.isEmpty()) {
                addMessage(text,  MessageType::AI);
                text = QString();
            }
            if(qxzd) return QString();
            if (ok) continue;
        }

        return text;
    }
    return QString();
}

QString AppWindow::buildMessageHtml(const QString &text, MessageType type)
{
    // 根据类型设置样式
    QString cardBg, cardBorder, rolePrefix,codeBg,codeTextColor;
    switch (type) {
    case MessageType::User:
        cardBg = "#E8F5E9";
        cardBorder = "#4CAF50";
        rolePrefix = "👤 用户:";
        codeBg = "#ED90BC";
        codeTextColor = "#000";
        break;
    case MessageType::AI:
        cardBg = "#EEF2F6";
        cardBorder = "#9C27B0";
        rolePrefix = "🤖 AI:";
        codeBg = "#353834";
        codeTextColor = "#ffffff";
        break;
    case MessageType::Tool:
        cardBg = "#FFF8E1";   // 浅黄
        cardBorder = "#FF9800"; // 橙色
        rolePrefix = "🔧 工具调用:";
        codeBg = "#9B9B9B";
        codeTextColor = "#ffffff";
        break;

    case MessageType::sk:
        cardBg = "#FFF8E1";   // 浅黄
        cardBorder = "#FF9800"; // 橙色

        codeBg = "#9B9B9B";
        codeTextColor = "#000";
        break;
    }
    QString roleHtml = QString("<div style='color: #000000; font-weight: bold; margin-bottom: 6px; font-size: 14px;'>%1</div>").arg(rolePrefix);


    QString trimmedText = text.trimmed();
    trimmedText.replace("```","");
    QString wrapped = "```" + trimmedText + "```\n";
    wrapped.replace("\n\n\n\n", "\n\n");
    wrapped.replace("\n\n\n", "\n\n");

    QString finalHtml = roleHtml;
    QStringList parts = wrapped.split("```");
    for (int i = 0; i < parts.size(); ++i) {
        if (i % 2 == 0) {
            QString escaped = parts[i].toHtmlEscaped();
            escaped.replace("\n", "<br>");
            finalHtml += escaped;
        } else {
            // 代码块颜色：工具和AI使用相同深色，用户使用蓝色系


            finalHtml += QString(
                             "<pre style='background:%1; color:%2; padding:8px 10px; border-radius:6px; "
                             "font-family:Consolas, monospace; font-size:12px; white-space:pre-wrap; "
                             "word-wrap:break-word; margin:0;'>"
                             "<code>%3</code></pre>"
                             ).arg(codeBg, codeTextColor, parts[i].toHtmlEscaped());
        }
    }

    return QString(
               "<div style='margin-bottom: 8px; "
               "background: %1; "
               "border-left: 5px solid %2; "
               "border-radius: 6px; "
               "padding: 6px 10px; "
               "box-shadow: 0 1px 2px rgba(0,0,0,0.03);'>"
               "  %3"
               "</div>"
               ).arg(cardBg, cardBorder, finalHtml);
}
QString AppWindow::buildMessageHtml2(const QString &text, MessageType type,const QString &toolname)
{
    // 根据类型设置样式
    QString cardBg, cardBorder, rolePrefix,codeBg,codeTextColor,roleHtml;
    switch (type) {
    case MessageType::User:
        cardBg = "#E8F5E9";
        cardBorder = "#4CAF50";
        rolePrefix = "👤 用户:";
        codeBg = "#ED90BC";
        codeTextColor = "#000"; //文本颜色
        break;
    case MessageType::AI:
        cardBg = "#EEF2F6";
        cardBorder = "#9C27B0";
        rolePrefix = "📝 正文:";
        codeBg = "#353834";
        codeTextColor = "#ffffff";
        break;
    case MessageType::Tool:
        cardBg = "#FFF8E1";   // 浅黄
        cardBorder = "#FF9800"; // 橙色
        rolePrefix = "🔧 调用工具("+toolname.trimmed()+"):";
        codeBg = "#9B9B9B";
        codeTextColor = "#000";
        break;
    case MessageType::sk:
        cardBg = "#FFF8E1";   // 浅黄
        cardBorder = "#FF9800"; // 橙色
        rolePrefix = "💬 思考:";
        codeBg = "#35DBFD";
        codeTextColor = "#000";
        break;
    }



    QString trimmedText = text.trimmed();
    trimmedText.replace("```","");

    trimmedText.replace("\n\n\n\n", "\n\n");
    trimmedText.replace("\n\n\n", "\n\n");
    roleHtml = QString("<div style='color: #000000; font-weight: bold; margin-bottom: 6px; font-size: 14px;'>%1</div>\n%2").arg(rolePrefix,trimmedText);
    QString finalHtml =  QString(
                             "<pre style='background:%1; color:%2; padding:8px 10px; border-radius:6px; "
                             "font-family:Consolas, monospace; font-size:12px; white-space:pre-wrap; "
                             "word-wrap:break-word; margin:0;'>"
                             "<code>%3</code></pre><br>"
                             ).arg(codeBg, codeTextColor,roleHtml);


    return QString(
               "<div style='margin-bottom: 8px; "
               "background: %1; "
               "border-left: 5px solid %2; "
               "border-radius: 6px; "
               "padding: 6px 10px; "
               "box-shadow: 0 1px 2px rgba(0,0,0,0.03);'>"
               "  %3"
               "</div>"
               ).arg(cardBg, cardBorder, finalHtml);
}

// ==================== AppWindow.cpp 实现 ====================

void AppWindow::Ai_posts_stream(const QString &model,std::function<void (const QString &)> callback)
{

    m_stream = std::make_unique<StreamSession>();
    m_stream->model = model;
    m_stream->callback = callback;
    m_stream->toolHandlers = m_toolHandlers; // 复制全局工具处理器

    // 验证模型
    bool found = false;
    for (const auto &m : std::as_const(ai_ui->modelList)) {
        if (m.name == model) {
            sxw["model"] = m.name;
            found = true;
            break;
        }
    }
    if (!found) {
        if (callback) callback("未找到模型：" + model);
        m_stream.reset();
        return;
    }


    tryNextStreamEndpoint(m_stream.get());
}

void AppWindow::tryNextStreamEndpoint(StreamSession *s)
{
    if (s->userAborted) {
        if (s->callback) s->callback("用户中断");
        finishStreamSession(s, false);
        return;
    }

    const ModelData* curModel = nullptr;
    for (const auto &m : std::as_const(ai_ui->modelList)) {
        if (m.name == s->model) {
            curModel = &m;
            break;
        }
    }
    if (!curModel) {
        if (s->callback) s->callback("模型未找到");
        finishStreamSession(s, false);
        return;
    }

    int totalInterfaces = curModel->enabledInterfaceIndices.size();
    if (s->interfaceIndex >= totalInterfaces) {
        QString errMsg = "所有接口均失败";
        if (!m_err.isEmpty()) errMsg += "\n" + m_err;
        if (s->callback) s->callback(errMsg);
        finishStreamSession(s, false);
        return;
    }

    int ifaceIdx = curModel->enabledInterfaceIndices[s->interfaceIndex];
    auto &iface = ai_ui->globalInterfaces[ifaceIdx];
    int keyCount = iface.keys.size();

    if (s->keyIndex >= keyCount) {
        s->interfaceIndex++;
        s->keyIndex = 0;
        tryNextStreamEndpoint(s);
        return;
    }

    s->currentUrl = iface.url;
    s->currentKey = iface.keys[s->keyIndex].key;

    // 创建流式请求
    startStreamRequest(s, s->currentUrl, s->currentKey);
}

void AppWindow::startStreamRequest(StreamSession *s, const QString &url, const QString &key)
{
    // 构建请求体
    QJsonObject obj;
    obj["completion_tokens"] =m_completion_tokens;
    obj["prompt_tokens"] =m_prompt_tokens;
    obj["prompt_tokens_details"] =m_prompt_tokens_details;
    sxw["usage"]=obj;
    W_file(m_dir + "/ai对话.json", QJsonDocument(sxw).toJson());
    sxw.remove("usage");
    init_system(addmsg);  // 确保 sxw 正确
    addmsg.clear();
    QJsonObject requestObj = sxw;
    requestObj["stream"] = true;
    QByteArray jsonData = QJsonDocument(requestObj).toJson(QJsonDocument::Compact);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + key.toUtf8());

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    s->reply = manager->post(request, jsonData);
    s->reply->ignoreSslErrors();

    connect(s->reply, &QNetworkReply::readyRead, this, &AppWindow::onStreamReadyRead);
    connect(s->reply, &QNetworkReply::finished, this, &AppWindow::onStreamFinished);

    // 初始化 UI 流式显示

}
// 滚动条值变化槽
void AppWindow::onScrollChanged(int value)
{
    if (m_programScroll) return;   // 程序触发的滚动，不修改 zdgd
    QScrollBar *bar = chatTextEdit->verticalScrollBar();
    if (value < bar->maximum()) {
        zdgd = false;   // 用户向上滚动，关闭自动滚动
    } else {
        zdgd = true;    // 用户滚到底部，重新开启自动滚动
    }
}

// 程序触发的滚动到底部（不影响 zdgd 判断）
void AppWindow::scrollToBottom()
{
    m_programScroll = true;
    QScrollBar *bar = chatTextEdit->verticalScrollBar();
    bar->setValue(bar->maximum());
    m_programScroll = false;
}
void AppWindow::startStreamUI(StreamSession *s, const QString &title)
{
    QTextCursor cursor(chatTextEdit->document());
    cursor.movePosition(QTextCursor::End);
    s->aiReplyStartPos = cursor.position();
    cursor.insertHtml(QString("<b style='color:#2C3E50;'>%1</b><br>").arg(title.toHtmlEscaped()));

}

void AppWindow::appendReasoningChunk(StreamSession *s, const QString &chunk)
{
    QTextCursor c(chatTextEdit->document());
    c.movePosition(QTextCursor::End);
    QString escaped = chunk.toHtmlEscaped().replace("\n", "<br>");
    c.insertHtml(QString("<span style='color:#E26BF1; font-size:0.7em;'>%1</span>").arg(escaped));
    if(zdgd)//自动往下滚动显示
    {
        scrollToBottom();
    }
}

void AppWindow::appendContentChunk(StreamSession *s, const QString &chunk)
{
    QTextCursor c(chatTextEdit->document());
    c.movePosition(QTextCursor::End);
    QString escaped = chunk.toHtmlEscaped().replace("\n", "<br>");
    c.insertHtml(QString("<span style='color:#212121;'>%1</span>").arg(escaped));
    if(zdgd)//自动往下滚动显示
    {
       scrollToBottom();
    }
}

void AppWindow::appendToolCallChunk(StreamSession *s, const QString &info)
{
    QTextCursor c(chatTextEdit->document());
    c.movePosition(QTextCursor::End);
    c.insertHtml(QString("<div style='color:#1565C0; font-family:monospace; margin:4px 0;'>%1</div>")
                     .arg(info.toHtmlEscaped()));
    if(zdgd)//自动往下滚动显示
    {
        scrollToBottom();
    }
}

void AppWindow::onStreamReadyRead()
{
    if (!m_stream || !m_stream->reply) return;

    QByteArray data = m_stream->reply->readAll();
    m_stream->buffer.append(data);

    // 先检查是否有 JSON 错误（非 data: 开头的错误响应）
    QJsonParseError jsonErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &jsonErr);
    if (jsonErr.error == QJsonParseError::NoError) {
        QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            QJsonObject errObj = obj["error"].toObject();
            QString errText = errObj["message"].toString();
            if (!errText.isEmpty()) {
                errText += "\n" + errObj["metadata"].toObject()["raw"].toString();
                m_err += "\n" + errText;
            }
            return;
        }
    }

    // 按 SSE 规范拆分事件 (以 \n\n 分隔)
    int idx;
    while ((idx = m_stream->buffer.indexOf("\n\n")) != -1) {
        QByteArray event = m_stream->buffer.left(idx);
        m_stream->buffer.remove(0, idx + 2);
        parseSSE(m_stream.get(), event.trimmed());
    }
}

void AppWindow::parseSSE(StreamSession *s, const QByteArray &line)
{
    if (!line.startsWith("data: ")) return;
    QByteArray data = line.mid(6);
    if (data == "[DONE]") return; // 某些 API 用 [DONE] 结束，但我们依靠 finish_reason
    m_ok = true;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return;

    QJsonObject obj = doc.object();
    QJsonObject usage = obj["usage"].toObject();
    if(usage.contains("completion_tokens")){

        int completion_tokens =usage["completion_tokens"].toInt();
        int prompt_tokens =usage["prompt_tokens"].toInt();
        int prompt_tokens_details =usage["prompt_tokens_details"].toInt();

        m_completion_tokens +=completion_tokens;
        m_prompt_tokens +=prompt_tokens;
        m_prompt_tokens_details +=prompt_tokens_details;

        m_completion_tokens2 +=completion_tokens;
        m_prompt_tokens2 +=prompt_tokens;
        m_prompt_tokens_details2 +=prompt_tokens_details;


        m_completion_tokens3 +=completion_tokens;
        m_prompt_tokens3 +=prompt_tokens;
        m_prompt_tokens_details3 +=prompt_tokens_details;


    }
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) return;
    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();

    if (delta.contains("reasoning_content")) {
        QString chunk = delta["reasoning_content"].toString();
        if (!chunk.isEmpty()) {

            if(s->accumulatedReasoning.isEmpty()){
                startStreamUI(s,"💬 思考:");
                s->accumulatedReasoning.reserve(64*1024);//预分配 64k
                liu_text.reserve(64);
            }
            s->accumulatedReasoning.append(chunk);
            liu_text.append(chunk);
            if(liu_text.size()>40){
                appendReasoningChunk(s, liu_text);
                liu_text.clear();
                liu_text.reserve(64);
            }
        }
    }

    // 处理正文内容
    QString chunk = delta["content"].toString();
    if (!chunk.isEmpty()) {

        if(s->accumulatedContent.isEmpty())
        {
            s->accumulatedContent.reserve(64*1024);
            liu_text.clear();
            liu_text.reserve(64);
            if(!s->accumulatedReasoning.isEmpty())
            {
                if(s->aiReplyStartPos!=-1){
                    QTextCursor c(chatTextEdit->document());
                    c.setPosition(s->aiReplyStartPos);
                    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
                    c.removeSelectedText();
                    s->aiReplyStartPos=-1;
                }
                addMessage2(s->accumulatedReasoning,MessageType::sk);
                s->accumulatedReasoning.clear();
            }
            startStreamUI(s,"📝 正文:");
        }
        s->accumulatedContent.append(chunk) ;
        liu_text.append(chunk);
        if(liu_text.size()>40){
            appendContentChunk(s, liu_text);
            liu_text.clear();
            liu_text.reserve(64);
        }

    }

    // 处理工具调用
    if (delta.contains("tool_calls")) {
        QJsonArray calls = delta["tool_calls"].toArray();
        for (const QJsonValue &v : std::as_const(calls)) {
            QJsonObject call = v.toObject();
            int index = call["index"].toInt();
            QJsonObject newFunc = call["function"].toObject();
            if (!s->toolCallsMap.contains(index)) {

                s->toolCallsMap[index] = call;
                if(!s->dyc){
                    s->dyc =true;
                    liu_text.clear();
                    liu_text.reserve(64);
                    if(!s->accumulatedReasoning.isEmpty()){
                        if(s->aiReplyStartPos!=-1){
                            QTextCursor c(chatTextEdit->document());
                            c.setPosition(s->aiReplyStartPos);
                            c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
                            c.removeSelectedText();
                            s->aiReplyStartPos=-1;
                        }
                        addMessage2(s->accumulatedReasoning,MessageType::sk);
                        s->accumulatedReasoning.clear();
                    }
                    if(!s->accumulatedContent.isEmpty()){

                        if(s->aiReplyStartPos!=-1){
                            QTextCursor c(chatTextEdit->document());
                            c.setPosition(s->aiReplyStartPos);
                            c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
                            c.removeSelectedText();
                            s->aiReplyStartPos=-1;
                        }


                        addMessage2(s->accumulatedContent,MessageType::AI);
                    }
                    startStreamUI(s,"🔧 调用工具("+newFunc["name"].toString()+")");
                }

            } else {
                QJsonObject existing = s->toolCallsMap[index];
                QJsonObject existingFunc = existing["function"].toObject();
                QString existingArgs = existingFunc["arguments"].toString();


                QString newArgs = newFunc["arguments"].toString();
                if (!newArgs.isEmpty()) {
                    existingArgs += newArgs;
                }

                liu_text.append(newArgs);
                if(liu_text.size()>40){
                    appendToolCallChunk(s, liu_text);
                    liu_text.clear();
                    liu_text.reserve(64);
                }
                existingFunc["arguments"] = existingArgs;
                if (!newFunc["name"].isNull() && !newFunc["name"].toString().isEmpty()) {
                    existingFunc["name"] = newFunc["name"];
                }
                existing["function"] = existingFunc;
                if (!call["id"].isNull() && !call["id"].toString().isEmpty()) {
                    existing["id"] = call["id"];
                }
                s->toolCallsMap[index] = existing;
            }
            // 实时显示工具调用名称（只显示一次）
            static QSet<int> shownToolIndices; // 可以放到会话里
            if (!shownToolIndices.contains(index)) {
                QString toolName = call["function"].toObject()["name"].toString();
                if (!toolName.isEmpty()) {
                    shownToolIndices.insert(index);
                }
            }
        }
    }
    /*
    // 检查是否结束
    QString finish = choice["finish_reason"].toString();
    if (finish == "stop" || finish == "tool_calls") {

        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = s->accumulatedContent;

        if (!s->toolCallsMap.isEmpty()) {
            QJsonArray toolCallsArray;
            QList<int> keys = s->toolCallsMap.keys();
            std::sort(keys.begin(), keys.end());
            for (int idx : std::as_const(keys)) {
                toolCallsArray.append(s->toolCallsMap[idx]);
            }
            assistantMsg["tool_calls"] = toolCallsArray;
        }
        QJsonArray msgs = sxw["messages"].toArray();
        msgs.append(assistantMsg);
        sxw["messages"] = msgs;
        s->toolCallPending = (finish == "tool_calls");
        s->finished = true;

        if(!s->accumulatedContent.isEmpty()){
            QTextCursor c(chatTextEdit->document());
            c.setPosition(s->aiReplyStartPos);
            c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            addMessage2(s->accumulatedContent,MessageType::AI);
        }
    }
*/
}

void AppWindow::onStreamFinished()
{
    if (!m_stream) return;
    auto *s = m_stream.get();
    if (!s->reply) return;

    bool success = (s->reply->error() == QNetworkReply::NoError);
    QString error = success ? QString() : s->reply->errorString();

    // 处理剩余 buffer
    if (!s->buffer.isEmpty()) {
        parseSSE(s, s->buffer);
        s->buffer.clear();
    }

    s->reply->deleteLater();
    s->reply = nullptr;
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = s->accumulatedContent;
    QJsonArray toolCallsArray;
    if (!s->toolCallsMap.isEmpty()) {

        QList<int> keys = s->toolCallsMap.keys();
        std::sort(keys.begin(), keys.end());
        for (int idx : std::as_const(keys)) {
            toolCallsArray.append(s->toolCallsMap[idx]);
        }
        assistantMsg["tool_calls"] = toolCallsArray;
    }
    QJsonArray msgs = sxw["messages"].toArray();
    msgs.append(assistantMsg);
    sxw["messages"] = msgs;



    if(!s->accumulatedContent.isEmpty()){
        if(s->aiReplyStartPos!=-1){
            QTextCursor c(chatTextEdit->document());
            c.setPosition(s->aiReplyStartPos);
            c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            s->aiReplyStartPos=-1;
        }

        addMessage2(QString("%1\n输入:%2  补全:%3  缓存:%4").arg(s->accumulatedContent, formatTokens(m_prompt_tokens3), formatTokens(m_completion_tokens3),
                                                                 formatTokens(m_prompt_tokens_details3)),MessageType::AI);
        m_prompt_tokens3 = 0;
        m_completion_tokens3 =0;
        m_prompt_tokens_details3 = 0;

    }

    if (s->userAborted) {

        if (!s->accumulatedContent.isEmpty() || !s->toolCallsMap.isEmpty()) {
            QJsonObject assistantMsg;
            assistantMsg["role"] = "assistant";
            assistantMsg["content"] = s->accumulatedContent;  // 中断时已有的正文
            if (!s->toolCallsMap.isEmpty()) {
                QJsonArray toolCallsArray;
                assistantMsg["tool_calls"] = toolCallsArray;
            }
            QJsonArray msgs = sxw["messages"].toArray();
            msgs.append(assistantMsg);


            sxw["messages"] = msgs;
        }

        if (s->callback) s->callback("用户中断");
        finishStreamSession(s, false);
        return;
    }

    // 错误处理与重试逻辑
    if (!success) {
        if (error.contains("token") && s->retryCount < 3) {
            s->retryCount++;
            // 回退最后一条消息（可能是无效的）
            QJsonArray msgs = sxw["messages"].toArray();
            if (msgs.size() > 1) {
                msgs.removeLast();
                sxw["messages"] = msgs;
            }
            startStreamRequest(s, s->currentUrl, s->currentKey);
            return;
        }

        // 切换密钥或接口
        s->retryCount = 0;
        const ModelData* curModel = nullptr;
        for (const auto &m : std::as_const(ai_ui->modelList)) {
            if (m.name == s->model) {
                curModel = &m;
                break;
            }
        }
        if (curModel) {
            int ifaceIdx = curModel->enabledInterfaceIndices[s->interfaceIndex];
            auto &iface = ai_ui->globalInterfaces[ifaceIdx];
            int keyCount = iface.keys.size();
            if (s->keyIndex + 1 < keyCount) {
                s->keyIndex++;
            } else {
                s->interfaceIndex++;
                s->keyIndex = 0;
            }
            tryNextStreamEndpoint(s);
            return;
        }
        if (s->callback) s->callback(error);
        finishStreamSession(s, false);
        return;
    }

    // 成功：如果还有工具调用待处理，执行工具并继续
    if (toolCallsArray.size()!=0) {
        s->toolCallPending = false;
        QJsonArray msgs = sxw["messages"].toArray();
        if (!msgs.isEmpty()) {
            QJsonObject last = msgs.last().toObject();

            if (last["role"].toString() == "assistant" && last.contains("tool_calls")) {
                const QJsonArray toolCalls = last["tool_calls"].toArray();
                QString res,tool_name;
                for (const QJsonValue &v : toolCalls) {
                    QJsonObject call = v.toObject();
                    QString id = call["id"].toString();
                    QJsonObject func = call["function"].toObject();
                    QString name = func["name"].toString();
                    QString args = func["arguments"].toString();

                    // 查找注册的工具处理器，否则调用默认 tools_fun
                    QString result;
                    if (s->toolHandlers.contains(name)) {
                        result = s->toolHandlers[name](name, args);
                    } else {
                        result = tools_fun(name, args, sxw["model"].toString());
                    }
                    if (result.isEmpty()) result = "工具调用完成，无返回值";
                    res += QString("调用工具：%1\n参数：%2\n返回：%3\n\n").arg(name, args, result);
                    tool_name +=name+" ";

                    QJsonObject toolMsg;
                    toolMsg["role"] = "tool";
                    toolMsg["content"] = result;
                    toolMsg["tool_call_id"] = id;
                    toolMsg["name"] = name;
                    msgs.append(toolMsg);
                }
                if(s->aiReplyStartPos!=-1){
                    QTextCursor c(chatTextEdit->document());
                    c.setPosition(s->aiReplyStartPos);
                    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
                    c.removeSelectedText();
                    s->aiReplyStartPos=-1;
                }
                addMessage2(res, MessageType::Tool,tool_name);
                sxw["messages"] = msgs;
            }
        }
        // 继续请求，将工具结果送回模型
        s->accumulatedReasoning.clear();
        s->accumulatedContent.clear();
        s->toolCallsMap.clear();
        startStreamRequest(s, s->currentUrl, s->currentKey);
        return;
    }
    if(!addmsg.isEmpty())
    {
        s->accumulatedReasoning.clear();
        s->accumulatedContent.clear();
        s->toolCallsMap.clear();
        startStreamRequest(s, s->currentUrl, s->currentKey);
        return;
    }
    if (s->callback) s->callback(QString());
    finishStreamSession(s, true);
}

void AppWindow::finishStreamSession(StreamSession *s, bool success)
{
    Q_UNUSED(success);
    m_stream.reset();
}
void AppWindow::onSendMessage_stream(const QString &text)
{

    if (m_dir.isEmpty()) {
        QMessageBox::warning(this, "", "请先打开一个文件夹，因为本 AI 对话会涉及操作文件");
        return;
    }
    zdgd = true;
    sendBtn->setText("中断");

    if (!sxw.contains("messages") || !sxw["messages"].isArray()) {
        QJsonArray msgs;
        if (!g_system.isEmpty()) {
            QJsonObject systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = subTextReplace(g_system, "{{文件目录}}", listDirectoryEntries(m_dir));
            msgs.append(systemMsg);
        }
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = text;
        msgs.append(userMsg);
        sxw["messages"] = msgs;
        addMessage2(text, MessageType::User);
    } else {
        init_system(text);
    }

    ai_ui->trimToolResponses(sxw, 5, 64);
    ai_ui->trimContextByMessageCount(sxw, 128);
    QString model = modelCombo->currentText();
    sxw["model"] = model;
    sxw["tools"] = m_fun;
    sxw.remove("usage");
    m_completion_tokens2 =0;
    m_prompt_tokens2 =0;
    m_prompt_tokens_details2 =0;
    m_completion_tokens3 =0;
    m_prompt_tokens3 =0;
    m_prompt_tokens_details3 =0;

    m_ok = false;
    Ai_posts_stream(model, [this](const QString &err) {
        if(!m_ok)
        {
            addMessage2("错误：所有接口都未返回数据" , MessageType::AI);
        }
        QJsonArray msgs = sxw["messages"].toArray();
        for (int i = 0; i < msgs.size(); ++i) {
            QJsonObject msg = msgs[i].toObject();
            if (msg["role"].toString() == "system") {
                msg["content"] = "";
                msgs[i] = msg;
                sxw["messages"]=msgs;
                break; // 通常只有一个 system，找到就停
            }
        }
        sxw.remove("tools");
        if (!err.isEmpty()) {
            QTextCursor c(chatTextEdit->document());
            c.movePosition(QTextCursor::End);
            c.insertHtml("<br><br>");
            addMessage2("错误：" + err, MessageType::AI);
        }
        QJsonObject obj;
        obj["completion_tokens"] =m_completion_tokens;
        obj["prompt_tokens"] =m_prompt_tokens;
        obj["prompt_tokens_details"] =m_prompt_tokens_details;
        sxw["usage"]=obj;
        W_file(m_dir + "/ai对话.json", QJsonDocument(sxw).toJson());
        sendBtn->setText("发送");
    });
}