# -*- coding: utf-8 -*-
"""扫描 include 与真实文件名的大小写一致性。
Windows 不区分大小写，Linux 区分 —— 这类问题只在 Linux 编译时暴露。"""
import os
import re
import sys

ROOT = r"C:\Users\Airuan\Documents\QTCode\qiancao"
SKIP_DIRS = {"build", ".git", ".workbuddy", "_qatest"}

byname = {}
allfiles = []
for dirpath, dirnames, filenames in os.walk(ROOT):
    dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
    for fn in filenames:
        allfiles.append(os.path.join(dirpath, fn))
        byname.setdefault(fn, set()).add(fn)

lowmap = {}
for k in byname:
    lowmap.setdefault(k.lower(), set()).add(k)

inc_re = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]')
exts = (".c", ".cpp", ".h", ".hpp", ".cc", ".cxx")

bad = []
checked = 0
for path in allfiles:
    if not path.lower().endswith(exts):
        continue
    checked += 1
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for ln, line in enumerate(f, 1):
            m = inc_re.match(line)
            if not m:
                continue
            raw = m.group(2)
            base = os.path.basename(raw)
            if base in byname:
                continue                       # 完全一致
            hit = lowmap.get(base.lower())
            if not hit:
                continue                       # 系统/Qt 头，不是本仓库的
            bad.append((os.path.relpath(path, ROOT), ln, raw, sorted(hit)[0]))

print("=== 大小写不一致的 include（Linux 会报找不到文件）===")
if not bad:
    print("(无)")
for p, ln, raw, real in bad:
    print("%s:%d  #include \"%s\"  ->  实际文件是 %s" % (p, ln, raw, real))
print("")
print("不一致处数 = %d" % len(bad))
print("扫描源文件数 = %d / 全仓文件数 = %d" % (checked, len(allfiles)))
