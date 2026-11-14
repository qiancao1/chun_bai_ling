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

#ifndef LMDBKV_H
#define LMDBKV_H

// ─────────────────────────────────────────────────────────────────────────────
// 兼容头。LmdbKV 的实现已经在 2026-11-13 从 LMDB 换成 SQLite（见 db/kvdb.h）。
//
// 保留这个名字，是为了让下面这些地方一行都不用改：
//   core/global.h（`extern LmdbKV *aidb / dsdb / accdb`）
//   main.cpp、ai/ailog.h、ai/nickreviewwidget.cpp
// 以及全部 `LmdbKV *p` / `new LmdbKV(path)` / `p->put/get/remove/close` 调用。
//
// ⚠ cache_db 已经不在这一族里了（2026-11-13）：上传缓存换成纯内存的 MemKV
//   （db/memkv.h），类型是 `MemKV *`，不再走 KV 库。见 core/global.h 的声明处。
//
// 现在这个头**不再拉 lmdb.h** —— 用 LMDB 的只剩 db/botdb.*、db/vectormemory.h。
// 如果哪天想彻底删掉 LMDB，把 db/kvdb.cpp 末尾那段「旧 data.mdb 迁移」一起删掉即可。
// ─────────────────────────────────────────────────────────────────────────────

#include "kvdb.h"

using LmdbKV = KvDB;

#endif // LMDBKV_H
