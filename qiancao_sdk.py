import json
import qq_api
import random
import string
from typing import Optional, Union, Dict, List, Any


class QQApi:
    API_OUTLOG = 1 
    API_SEND_MESSAGES = 2
    API_SEND_MESSAGES_ARK = 3
    API_DELETE_MESSAGES = 4
    API_GENERATE_SHARE_LINK = 5
    API_RESPOND_INTERACTION = 6
    API_BOT_LIST = 7
    API_GET_USER_OPENID=8
    API_GET_USER_NAME=9
    API_HTTP=10
    
    API_ID_GET_USER_ID=11;
    API_ID_HTMLIMG1=12;
    API_ID_HTMLIMG2=13;
    API_ID_DS=14;
    API_ID_AI=15;
    API_ID_GET_MEMBER=16;
    API_ID_GET_MEMBER_LIST=17;    
    API_ID_GET_GROUPS_INFO = 18          # 获取群信息
    API_ID_GET_GROUPS_BOT_STATE = 19     # 获取机器人在群内的状态
    API_ID_SET_JOIN_REQUEST = 20         # 处理加群请求
    API_ID_GET_JOIN_REQUEST_LIST = 21    # 获取加群请求列表
    API_ID_SET_MUTE_G = 22               # 设置群禁言（批量）
    API_ID_GET_MUTE_LIST_G = 23          # 获取群禁言列表
    API_ID_REMOV_MEMBER =24;             #//批量移除成员
    API_ID_GET_GROUP_BLCKLIST=25;        #//获取群黑名单列表
    API_ID_GROUP_BLCKLIST=26;            #//修改黑名单列表 
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
        # API_SEND_MESSAGES: _1=type, _2=openid, _3=text,  _4=msgid, _5=is_wakeup, _7=None, _8=None
        return self._callback(self.API_SEND_MESSAGES,appid,
                              type_, openid, text, msgid,
                              "true" if is_wakeup else "false", None, None)

    async def send_message_async(self, appid: int, type_: int, openid: str, text: str, 
                                 msgid: str = "", is_wakeup: bool = False) -> Dict:
        """
        伪异步发送普通消息（新版插件请使用此方法）。
        """
        return await asyncio.to_thread(
            self.send_message,
            appid, type_, openid, text, msgid, is_wakeup
        )

    async def send_messageEx_async(self, msg: qq_api.MessageEvent, text: str, is_wakeup: bool = False) -> Dict:
        """
        伪异步发送消息（传入 MessageEvent 对象，新版插件请使用此方法）。
        """
        return await asyncio.to_thread(
            self.send_messageEx,
            msg, text, is_wakeup
        )
        
    def send_msgEx(self,msg: qq_api.MessageEvent, text: str, is_wakeup: bool = False) -> Dict:
        return self._callback(self.API_SEND_MESSAGES,msg.appid,
                              msg.type, msg.groupid, text,msg.msgid,
                              "true" if is_wakeup else "false", "true", None)
                              
    def send_msg(self,appid: int, type_: int, openid: str, text: str,msgid: str = "", is_wakeup: bool = False) -> Dict:
        """
        发送普通消息。不返回结果 
        :param type_: 消息类型，0=群聊，1=频道，2=私聊，3=频道私聊
        :param openid: 接收者的 openid
        :param text: 消息内容
        :param message_reference: 引用消息ID（可选）
        :param msgid: 消息ID，空字符串表示主动模式
        :param is_wakeup: 是否为私聊的唤醒消息（与 msgid 互斥，仅私聊有效）
        """
        # API_SEND_MESSAGES: _1=type, _2=openid, _3=text,  _4=msgid, _5=is_wakeup, _7=None, _8=None
        return self._callback(self.API_SEND_MESSAGES,appid,
                              type_, openid, text, msgid,
                              "true" if is_wakeup else "false", "true", None) 
                              
    def send_ark(self, appid: int,type_: int, openid: str, ark: Union[Dict, str],
                 msgid: str = "", is_wakeup: bool = False) -> Dict:
        """
        发送 ARK 卡片消息。
        :param type_: 消息类型，0=群聊，1=频道，2=私聊，3=频道私聊
        :param openid: 接收者 openid
        :param ark: ARK 数据（字典或 JSON 字符串）
        :param msgid: 消息ID，空字符串表示主动模式
        :param is_wakeup: 是否为私聊的唤醒消息（与 msgid 互斥，仅私聊有效）
        """
        ark_str = ark if isinstance(ark, str) else json.dumps(ark, ensure_ascii=False)
        # API_SEND_MESSAGES_ARK: _1=type, _2=openid, _3=ark, _4=msgid, _5=is_wakeup, _6=None, _7=None, _8=None
        return self._callback(self.API_SEND_MESSAGES_ARK,appid,type_, openid, ark_str, msgid,
                              "true" if is_wakeup else "false")

    def delete_message(self,appid: int, type_: int, openid: str, msgid: str) -> Dict:
        """
        删除消息。
        :param type_: 消息类型，0=群聊，1=频道，2=私聊，3=频道私聊
        :param openid: 会话对象ID
        :param msgid: 要删除的消息ID
        """
        # API_DELETE_MESSAGES: _1=type, _2=openid, _3=msgid
        return self._callback(self.API_DELETE_MESSAGES,appid, type_, openid, msgid)

    def generate_share_link(self,appid:int, callback_data: str) -> Dict:
        """
        生成分享链接。
        :param callback_data: 回调数据
        """
        # API_GENERATE_SHARE_LINK: _1=callback_data
        return self._callback(self.API_GENERATE_SHARE_LINK,appid, callback_data)

    def respond_interaction(self, appid: int,interaction_id: str, code: int, data: str) -> Dict:
        """
        响应交互事件。
        :param interaction_id: 交互ID
        :param code: 响应码（如 0 表示成功）
        :param data: 响应数据（JSON 字符串）
        """
        # API_RESPOND_INTERACTION: _1=interaction_id, _2=code, _3=data
        return self._callback(self.API_RESPOND_INTERACTION,appid,interaction_id, str(code), data)
        
    def botlist(self) -> List[Dict[str, Any]]:
        """
        获取 Bot 列表，每个字典包含以下字段（由 C++ 提供）：
        - appid, name, qq, avatarPath, received, send, online, id, union_openid, startup_time
        - online_duration: 已格式化的在线时长字符串，例如 "2天3小时5分钟"
        """
        raw = self._callback(self.API_BOT_LIST, 0)
        
        # 如果已经是列表，直接返回
        if isinstance(raw, list):
            return raw
        
        # 如果是字符串，尝试解析为 JSON
        if isinstance(raw, str):
            try:
                parsed = json.loads(raw)
                if isinstance(parsed, list):
                    return parsed
                else:
                    # 解析结果不是列表，按需处理，这里返回空列表
                    return []
            except json.JSONDecodeError:
                # 解析失败，可记录日志，返回空列表
                return []
        
        # 其他类型，返回空列表
        return []

    def get_openid(self,appid: int ,user_id:int) -> Dict:
        return self._callback(self.API_GET_USER_OPENID, appid, str(user_id))
        
    def get_user_name(self,appid:int ,user_id:int) -> Dict:
        return self._callback(self.API_GET_USER_NAME, appid, str(user_id))

    def http_request(self, url: str, method: str = "GET", headers: dict = None, body: bytes = None, timeout: int = 30) -> dict:
        headers_json = json.dumps(headers or {})
        body_b64 = base64.b64encode(body).decode('ascii') if body is not None else ""
        return self._callback(self.API_HTTP, 0, url, method.upper(), headers_json, body_b64, str(timeout))
        
     # ---------- 补充的 API 封装 ----------
    def get_user_id(self, appid: int, user: str) -> Dict:
        """
        根据用户整数ID获取用户内部ID（或用户信息）
        :param appid: Bot appid
        :param user: 32字节那个
        :return: 整数id
        """
        return self._callback(self.API_ID_GET_USER_ID, appid, str(user_id))

    def htmlimg1(self, text: str, width: int) -> Dict:
        """
        将HTML文本渲染为图片（方式1）
        :param text: HTML文本
        :param width: 图片宽度（或其它整型参数）
        """
        return self._callback(self.API_ID_HTMLIMG1, 0, text, str(width))

    def htmlimg2(self, text: str, width: int, height: int, extra: int = 0) -> Dict:
        """
        将HTML文本渲染为图片（方式2）
        :param text: HTML文本
        :param width: 宽度
        :param height: 高度
        :param extra: 额外参数，默认为0（http请求api超时时间）
        """
        return self._callback(self.API_ID_HTMLIMG2, 0, text, str(width), str(height), str(extra))

    def add_timer(self, appid: int, remark: str, time_str: str, execute_count: int, code: str) -> Dict:
        """
        添加定时任务
        :param appid: Bot appid
        :param remark: 备注（参数1）
        :param time_str: 定时时间（参数2）
        :param execute_count: 执行次数，超出销毁（参数3）
        :param code: Python代码（参数4）
        """
        return self._callback(self.API_ID_DS, appid, remark, time_str, str(execute_count), code)

    def ai_chat(self,model: str, content: str, timeout: int = 30) -> Dict:
        """
        AI对话 本api禁止 外部插件使用 只允许框架内部 py代码使用
        :param appid: Bot appid
        :param model: 模型名称（参数1）
        :param content: 提交内容（参数2）
        :param timeout: 超时时间（ms），对应C++的_3
        """
        return self._callback(self.API_ID_AI, 0, model, content, str(timeout))   
        
    def get_member(self,appid: int,  openid : str, uset : str) -> Dict:
        """
        查询 某个用户 在群 昵称 身份
        :param appid: Bot appid
        :param openid: 群id（参数1）
        :param uset: 用户id（参数2）
        """
        return self._callback(self.API_ID_GET_MEMBER, appid, openid, uset)     
    def get_member_list(self,appid: int,  openid : str, cursor : str) -> Dict:
        """
        获取指定群成员列表 返回json 这个是未开放的api 保留
        :param appid: Bot appid
        :param openid: 群id（参数1）
        :param cursor: 每次30个 传这个继续获取下一个
        """
        return self._callback(self.API_ID_GET_MEMBER_LIST, appid, openid, cursor)    
    
    def get_groups_info(self, appid: int, group_openid: str) -> Dict:
        """
        获取指定群的基本信息。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return: 群信息（JSON 字符串或字典，由 C++ 返回）
        """
        return self._callback(self.API_ID_GET_GROUPS_INFO, appid, group_openid)

    def get_groups_bot_state(self, appid: int, group_openid: str) -> Dict:
        """
        获取机器人在指定群内的状态（如是否被禁言等）。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return: 状态信息（JSON 字符串）
        """
        return self._callback(self.API_ID_GET_GROUPS_BOT_STATE, appid, group_openid)

    def set_join_request(self, appid: int, group_openid: str, user_openid: str,
                         approve: bool, request_id: str = "",
                         reject_reason: str = "", blacklist: bool = False) -> Dict:
        """
        处理加群请求（同意/拒绝）。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :param user_openid: 申请用户的 openid
        :param approve: True=同意，False=拒绝
        :param request_id: 请求 ID（可选，从请求列表中获取）
        :param reject_reason: 拒绝理由（可选）
        :param blacklist: 是否拉黑该用户（可选，默认 False）
        :return: 操作结果（JSON 字符串）
        """
        return self._callback(self.API_ID_SET_JOIN_REQUEST, appid,
                              group_openid, user_openid,
                              "true" if approve else "false",
                              request_id, reject_reason,
                              "true" if blacklist else "false")

    def get_join_request_list(self, appid: int, group_openid: str) -> Dict:
        """
        获取指定群的加群请求列表。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return: {"list":[{"join_request_id":"xxx","risk_tips":"","union_openid":"xxx","member_openid":"xxx","username":"๑҉环绕᭄ꦿ໌້ᮨ","apply_at":"2026-08-10T23:06:21+08:00","apply_source":"self_apply","invited_by":"","bot":false,"verify_info":{"method":"admin_review_qa","verify_message":"","review_qa_list":[{"question":"1","answer":"1"}]}}],"next_cursor":"1785949822131345"}#注意 union_openid 部分机器人可能是空 但是优先使用
        """
        return self._callback(self.API_ID_GET_JOIN_REQUEST_LIST, appid, group_openid)

    def set_mute_g(self, appid: int, group_openid: str, members: Union[List[Dict], str]) -> Dict:
        """
        设置群内成员禁言（批量）。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :param members: 禁言设置列表（JSON 数组字符串 或 列表对象），
                        每个元素包含 user_openid 和 duration_seconds 等字段。
        :return: 操作结果（JSON 字符串）
        """
        if isinstance(members, list):
            members_json = json.dumps(members, ensure_ascii=False)
        else:
            members_json = members  # 直接当做字符串传入
        return self._callback(self.API_ID_SET_MUTE_G, appid, group_openid, members_json)

    def get_mute_list_g(self, appid: int, group_openid: str) -> Dict:
        """
        获取指定群的禁言成员列表。
        :param appid: Bot appid
        :param group_openid: 群 openid
        :return:  {"global_rule":{"mode":"none","schedule_rules":[],"recurring_rules":[]},"members":[{"member_openid":"486EFB457F0FF264FCB457418D962B83","mute_expire_at":"2026-08-11T11:02:12+08:00","username":"云猫猫💐","union_openid":"486EFB457F0FF264FCB457418D962B83"}]} #注意 union_openid 部分机器人可能是空 但是优先使用
        """
        return self._callback(self.API_ID_GET_MUTE_LIST_G, appid, group_openid)
        
    def remov_members(self, appid: int, group_openid: str,user_list : str,add_to_member_blacklist : bool) -> Dict:
        """
        批量移除成员
        :param appid: Bot appid
        :param group_openid: 群 openid
        :param user_list ,被踢出id  英文逗号分割 如a,b,c
        :param add_to_member_blacklist 添加到黑名单
        """
        return self._callback(self.API_ID_REMOV_MEMBER, appid, group_openid,user_list,"true" if add_to_member_blacklist else "false",)    
        
    def get_grout_blacklist(self, appid: int, group_openid: str,cursor : str) -> Dict:
        """
        查询群黑名单
        :param appid: Bot appid
        :param group_openid: 群 openid
        :param cursor ,查询下标 第一次可空
        """
        return self._callback(self.API_ID_GET_GROUP_BLCKLIST, appid, group_openid,cursor)   
        
    def get_grout_blacklist(self, appid: int, group_openid: str,user_list : str,op:bool) -> Dict:
        """
        设置群黑名单
        :param appid: Bot appid
        :param group_openid: 群 openid
        :param user_list ,用户id列表 英文逗号分割
        :param op ,True 为添加
        """
        return self._callback(self.API_ID_GROUP_BLCKLIST, appid, group_openid,user_list,"true" if op else "false",) 

class ButtonGroup:
    def __init__(self):
        # 初始化一行，每行结构为 {"buttons": []}
        self.rows = [{"buttons": []}]
        self.current_row = 0   # 当前行索引

    def _random_id(self, length=8):
        """生成随机ID（字母数字混合）"""
        return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

    def add(self,
            name: str,                         # 按钮显示文本
            data: str,                         # 按钮携带的数据（回调时返回）
            action_type: int = 2,              # 动作类型：0=链接,1=回调,2=发送（默认2）
            btn_id: str = None,                # 按钮唯一ID，不传则自动生成
            enter: bool = False,               # 是否立即发送（点击后立即执行）
            reply: bool = False,               # 是否引用原消息
            color: int = 1,                    # 按钮颜色样式，默认1
            permission_type: int = 2,          # 权限类型：0=部分人,1=管理员,2=全部（默认2）
            specify_users: list = None,        # 指定可用用户列表（permission_type=0时有效）
            visited_label: str = "visited",    # 回调后按钮显示的文本（默认"visited"）
            unsupport_tip: str = None,         # 不支持时的提示文本（回调code4弹窗）
            modal_content: str = None,         # 确认框内容（最多40字符）
            modal_confirm: str = None,         # 确认按钮文本（最多4字符）
            modal_cancel: str = None,          # 取消按钮文本（最多4字符）
            subscribe_id: int = None,          # 订阅模板ID（整数）
            custom_subscribe_id: str = None):  # 自定义订阅模板ID（字符串）
        """
        在当前行追加一个按钮，完成后不移动列（直接追加）。
        若想换行，请调用 newrow() 方法。
        """
        # 确保当前行存在（防御）
        while self.current_row >= len(self.rows):
            self.rows.append({"buttons": []})

        # 获取当前行的按钮列表
        buttons = self.rows[self.current_row]["buttons"]

        # 构造按钮字典
        btn = {
            "id": btn_id or self._random_id(),
            "action": {
                "type": action_type,
                "data": data,
                "enter": enter,
                "permission": {
                    "type": permission_type
                }
            },
            "render_data": {
                "label": name,
                "style": color,
                "visited_label": visited_label or "visited"
            }
        }

        # 可选字段
        if specify_users:
            btn["action"]["permission"]["specify_user_ids"] = specify_users

        if reply:
            btn["action"]["reply"] = True

        if unsupport_tip:
            btn["action"]["unsupport_tips"] = unsupport_tip

        if modal_content:
            btn["action"]["modal"] = {"content": modal_content}
            if modal_confirm:
                btn["action"]["modal"]["confirm_text"] = modal_confirm
            if modal_cancel:
                btn["action"]["modal"]["cancel_text"] = modal_cancel

        if subscribe_id:
            btn["action"]["subscribe_data"] = {
                "template_ids": [{"template_id": subscribe_id}]
            }
            if custom_subscribe_id:
                btn["action"]["subscribe_data"]["template_ids"][0]["custom_template_id"] = custom_subscribe_id

        # 追加到当前行
        buttons.append(btn)

    def newrow(self):
        """换行：新增一行，后续 add 将添加到新行"""
        self.rows.append({"buttons": []})
        self.current_row += 1

    def to_json(self, indent=None) -> str:
        """输出符合标准格式的 JSON 字符串，顶层包含 content.rows"""
        return "#b:#"+json.dumps({"content": {"rows": self.rows}},
                          ensure_ascii=False,
                          indent=indent,
                          separators=(',', ':') if indent is None else None)+"#b:#"