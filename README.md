# 纯白铃 以下皆为AI生成 看看就好了

一款基于 Qt 开发的 QQ 机器人管理平台，支持多账号托管、插件系统和规则配置。

## 功能特性

### 多账号管理
- 支持同时管理多个 QQ 机器人账号
- 账号卡片式展示，实时显示在线状态
- 自动重连和心跳保活机制

### 消息处理
- 支持群聊、私聊、频道消息
- 消息类型：文字、图片、语音、视频、文件
- 消息引用回复功能
- 消息撤回支持

### 插件系统
- Python 插件支持（使用 pybind11 集成）
- JS 插件支持 适配少量单js 云崽插件
- DLL 插件加载
- 插件依赖管理
- 插件热加载/卸载

### 规则配置
- 机器人规则配置（优先级、触发条件）
- 关键词匹配（支持 AC 自动机高效匹配）
- 文本替换规则
- 按钮配置编辑器
- 黑名单/禁言词管理

### 沙盒环境
- Python 代码在线编辑和执行
- 交互式调试输出
- 插件快速测试

### 其他功能
- 日志系统（分级、过滤）
- 统计面板（收发消息计数）
- 图片服务器（支持 token 验证）
- 自动更新检查

## 技术架构

- **GUI**: Qt5/Qt6
- **数据库**: LMDB（用户数据、日志存储）
- **网络**: WinHTTP
- **脚本**: Python（插件系统）
- **编译**: CMake + MSVC

## 目录结构

```
├── db/                 # 数据库模块
│   ├── botdb.cpp/h    # 机器人数据存储
│   ├── logdb.cpp/h    # 日志数据库
│   ├── lmdbkv.cpp/h   # KV 存储封装
│   └── image-server.cpp # 图片服务器
├── qt/                 # 核心功能模块
│   ├── api.cpp        # QQ API 接口
│   ├── qqbotclient.cpp # 机器人客户端
│   ├── global.cpp     # 全局函数
│   ├── python_api.cpp # Python 绑定
│   └── ...
├── ui/                 # 界面模块
│   ├── mainwindow.cpp # 主窗口
│   ├── chatpage.cpp   # 聊天页面
│   ├── accountpage.cpp # 账号管理
│   ├── pluginpage.cpp # 插件管理
│   └── ...
└── libs/               # 第三方库
    └── LMDB/          # LMDB 头文件
```

## 构建要求

- Windows 10/11
- Visual Studio 2019+
- Qt 5.15+ 或 Qt 6
- Python 3.8+（用于插件）
- CMake 3.20+

## 快速开始

### 编译 我也不知道你问问ai

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### 运行

编译完成后运行 `qiancao.exe`，首次启动需要：
1. 添加机器人账号（需要 appid 和配置）
2. 配置插件目录
3. 启动机器人连接

## 配置说明

### 账号配置
- App ID：机器人应用 ID
- 类型：群聊/私聊/频道
- 权限： intents 权限配置

### 图片服务器
- 本地模式：设置 IP 和端口
- 远程模式：配置远程服务器地址
- Token 验证：可选启用

## 常见问题

**Q: 机器人无法连接？**
A: 检查网络代理、心跳间隔、access_token 是否过期

**Q: 插件加载失败？**
A: 检查 Python 环境、依赖是否完整、插件语法错误

**Q: 图片上传失败？**
A: 检查图片服务器配置、token 是否有效

## 相关链接

- Qt: https://www.qt.io/
- pybind11: https://github.com/pybind/pybind11
- LMDB: https://www.symas.com/lmdb

---

*最后更新：2026*