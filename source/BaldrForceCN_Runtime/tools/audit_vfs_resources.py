#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BALDR FORCE v1.0.4-crashfix1 旧汉化 VFS 发行资源清单工具。

v1.0.4-crashfix1 不再依靠“扫描中文字符”来猜哪些 loose 文件应该覆盖。用户已经把旧汉化
VFS 中除两个 PAC 外的 311 个 loose 文件，逐个与干净原版同路径文件做了二进制比较：
只有 9 个文件不同，其余 302 个逐字节完全相同。

本工具不会伪装成“重新做了一次 clean 对比”，因为它的输入只有旧 VFS。它做的是：
1. 枚举完整 313 文件 VFS；
2. 2 个 PAC 标记为 release 中的 Update.pac / Chapter.pac；
3. 9 个已确认差异 loose 文件标记为 ASI 内嵌覆盖；
4. 其余 302 个标记为“原版一致，直接使用 clean 文件”；
5. 任何不在固定路径分类中的文件都会进入 REVIEW，让发布检查失败。

原始对比证据：evidence/原版对比_311个loose文件仅9个有差异.png
9 个差异文件原字节：tools/研究工具/旧汉化差异资源_9文件.zip
"""
from __future__ import annotations
import csv, hashlib, sys
from collections import Counter
from pathlib import Path

DIFFERENT_LOOSE_PATHS = {
    "BMP/Hell/Font.grp",
    "BMP/Hell/hell_Menu03.grp",
    "BMP/Hell/MenuMsg.grp",
    "Dat/Cpu/genha.cpu",
    "Dat/Cpu/kaira.cpu",
    "Dat/Cpu/zako_s_t01.cpu",
    "Dat/Waza/neko05.waz",
    "Dat/Waza/TOORU.WAZ",
    "Dat/Waza/YAGISAWA.WAZ",
}

def sha256_file(path: Path) -> str:
    """分块计算 SHA256；即使文件是 20 多 MB 的 PAC，也不会额外一次读进内存。"""
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()

def classify(rel: str) -> tuple[str,str,str,str]:
    """把一个 VFS 相对路径映射为 v1.0.4-crashfix1 的处理方式。"""
    if rel == 'BFE.PAC':
        return ('PAC','外部部署为 release/Update.pac','COVERED','旧汉化 BFE.PAC 原字节作为 Update.pac 使用。')
    if rel == 'Chapter.pac':
        return ('PAC','外部部署为 release/Chapter.pac','COVERED','旧汉化 Chapter.pac 原字节直接部署。')
    if rel in DIFFERENT_LOOSE_PATHS:
        return ('ASI内嵌差异loose资源','编译进 BaldrForceCN.asi，CreateFileA 精确路径桥接','COVERED','用户对 311 个 loose 文件逐项二进制比较，确认该文件与干净原版不同。')

    # 其余路径的 PASS 依据不是下面的“类型”，而是用户已经确认它与 clean 原版逐字节相同。
    # 类型只用于让接档者一眼看懂 302 个 PASS 文件分别属于哪类资源。
    if rel.startswith('BMP/Hell/'):
        group='HELLMODE资源（原版一致）'
    elif rel.startswith('Dat/Cpu/'):
        group='CPU/AI（原版一致）'
    elif rel.startswith('Dat/Waza/'):
        group='WAZ（原版一致）'
    elif rel == 'Dat/Fnt16x16.fnt':
        group='字体'
    elif rel.startswith('Dat/BatVoice/'):
        group='战斗语音路由'
    elif rel.startswith('BMP/Map/'):
        group='地图数据'
    elif rel.startswith('BMP/Meka/'):
        group='机体图形/动画'
    elif rel.startswith('BMP/BatParts/'):
        group='战斗部件图'
    elif rel.startswith('Se/Voice/'):
        group='语音WAV'
    else:
        return ('未分类','未知','REVIEW','不在 v1.0.4-crashfix1 冻结路径分类中，需要人工复核。')
    return (group,'回退 clean 2003 原文件','PASS','用户全量二进制比较确认：该旧汉化 loose 文件与干净原版同路径文件逐字节完全相同。')

def main() -> int:
    if len(sys.argv)!=3:
        print('用法: python audit_vfs_resources.py <recovered_vfs> <项目根目录>')
        return 2
    vfs=Path(sys.argv[1]).resolve(); root=Path(sys.argv[2]).resolve()
    if not vfs.is_dir():
        print(f'[失败] recovered_vfs 不存在: {vfs}')
        return 2
    rows=[]
    for path in sorted(p for p in vfs.rglob('*') if p.is_file()):
        rel=path.relative_to(vfs).as_posix()
        group,handling,result,rationale=classify(rel)
        rows.append({'path':rel,'size':path.stat().st_size,'sha256':sha256_file(path),'group':group,'current_handling':handling,'audit_result':result,'rationale':rationale})
    out=root/'data'; out.mkdir(parents=True,exist_ok=True)
    csv_path=out/'旧汉化VFS资源审计.csv'
    with csv_path.open('w',newline='',encoding='utf-8-sig') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    results=Counter(r['audit_result'] for r in rows); groups=Counter(r['group'] for r in rows)
    lines=['BALDR FORCE 旧汉化 VFS v1.0.4-crashfix1 发行资源审计摘要','================================================',f'VFS files: {len(rows)}',f'VFS bytes: {sum(int(r["size"]) for r in rows)}',f'COVERED: {results["COVERED"]}',f'PASS: {results["PASS"]}',f'REVIEW: {results["REVIEW"]}','','Frozen comparison result:','  311 loose files compared against clean original by the user.','  9 files differ -> embedded in ASI.','  302 files are byte-identical -> use clean original.','','Groups:']
    for name in sorted(groups): lines.append(f'  {name}: {groups[name]}')
    lines += ['','Expected final state: 11 COVERED (2 PAC + 9 loose), 302 PASS, 0 REVIEW.']
    (out/'旧汉化资源审计摘要.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(f'[完成] {csv_path}'); print(f'[完成] {out/"旧汉化资源审计摘要.txt"}'); print(f'[结果] COVERED={results["COVERED"]} PASS={results["PASS"]} REVIEW={results["REVIEW"]}')
    return 0 if len(rows)==313 and results['COVERED']==11 and results['PASS']==302 and results['REVIEW']==0 else 1
if __name__=='__main__': raise SystemExit(main())
