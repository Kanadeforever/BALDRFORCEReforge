#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# 给刚接触编程的维护者：这个脚本只“读取和检查”，不会修改 BaldrForce.exe。
# 它做的事情可以理解成逐项对答案：先算文件 SHA256，再检查每一条补丁原位置
# 是否还是我们确认过的日文字节，最后检查 ASI 是否仍然是 32 位 DLL。
# 任何一项 FAIL 都表示“当前包和已经确认的基线不一致”，此时不要继续发布。
# 下面每个辅助函数都只负责一个很小的任务，例如读 16/32 位整数、把 RVA 换成
# 文件偏移、解析 PE 头等。这样以后修改某一项验证时，不需要碰其它检查。
# -----------------------------------------------------------------------------
from __future__ import annotations
import argparse, csv, hashlib, re, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_EXE_SHA256 = '5b65ecb1512b0cbf72cacdb5e981aa04f9c569698c6cab2f8877abf2d2cd42c5'
EXPECTED_UPDATE_SHA256 = '7f5f66e03d23869aeb2c8de771f4d34907478a85cfa8982e9da9c64612c4114e'
EXPECTED_CHAPTER_SHA256 = '565e48a1a272feb9c99223f0a5dac8e3a450dea4a5ce05e8e81e8e400b9e960d'
EXPECTED_UPDATE_SIZE = 23984401
EXPECTED_CHAPTER_SIZE = 8129004
SIGS = {
    'renderer.parse': (bytes.fromhex('80 F9 80 72 36 80 F9 9F 77 31 8B 4C 24 14'), 0x489726),
    'renderer.width': (bytes.fromhex('8A 44 24 14 3C 80 72 10 3C 9F 77 0C'), 0x489852),
    'renderer.late':  (bytes.fromhex('8B 44 24 44 83 F8 08 0F 87'), 0x489897),
    'script.op0B':   (bytes.fromhex('8B 41 08 56 8B 90 14 01 00 00 8B 70 0C 3B D6 72 06 33 C0 5E C2 08 00'), 0x4ACC20),
    'glyph.drawA':    (bytes.fromhex('8B B5 8C 00 00 00 66 8B 9D 9C 00 00 00 8B 4D FC'), 0x415840),
    'glyph.drawB':    (bytes.fromhex('8B B5 8C 00 00 00 8B 4D FC 8B 9D 9C 00 00 00'), 0x415CF1),
    'resource.open':   (bytes.fromhex('53 55 56 57 8B F1 E8 F5 00 00 00 8B 5C 24 18 8B 6C 24 14'), 0x4A9D40),
    # test9 的 0/0 根因已经确认：当时误用了旧 46MB 汉化映像里引用 0x53400C 的签名。
    # clean 2003 同一函数地址实际引用 0x53501C / 0x535028 / 0x535020。
    'glyph.lookup.dbcs.clean': (bytes.fromhex('A1 1C 50 53 00 56 8D 0C C5 00 00 00 00 2B C8 8B 04 8D 28 50 53 00 8D 0C 8D 20 50 53 00'), 0x4155B0),
    'glyph.lookup.single.clean': (bytes.fromhex('A1 1C 50 53 00 8D 0C C5 00 00 00 00 2B C8 8B 44 24 04'), 0x415710),
}
# test12 不再在 0x4155B0/0x415710 函数入口装 JMP。test10 的实机 TRACE 已证明：
# 到达 lookup 时字符已经被 clean Shift-JIS 规则切错。因此 test12 要验证并复刻旧汉化真正
# 修改的五个位置：三个调用者的 A0->FF DBCS 路由，以及两个 lookup 函数内部的状态写入点。
#
# 这里保存的是“clean 2003 磁盘文件中必须看到的原字节”。运行时 ASI 只有五处全部匹配时
# 才会开始写补丁，因此验证器也必须逐处确认，防止把旧汉化或其它版本当成 clean 基线。
OLD_LOCALIZATION_PATCH_BYTES = {
    'lowlevel.route1 clean cmp A0': (0x4154AD, bytes.fromhex('3C A0')),
    'lowlevel.route2 clean cmp A0': (0x415B2D, bytes.fromhex('3C A0')),
    'lowlevel.route3 clean cmp A0': (0x4170BF, bytes.fromhex('3C A0')),
    'lowlevel.dbcs lookup clean body': (
        0x4155E2,
        bytes.fromhex('66 3D FD 81 73 14 25 FF FF 00 00 2D 40 81 00 00')
    ),
    'lowlevel.single lookup clean body': (
        0x41572D,
        bytes.fromhex('3C 7F 77 12 25 FF 00 00 00')
    ),
}
# v1.0.4-crashfix1 继续只把用户全量二进制对比确认与原版不同的 9 个 loose 资源编译进 ASI；这里同时验证 source/assets 原件和 ASI 内嵌字节。
EXPECTED_EMBEDDED_ASSETS = {
    'BMP/Hell/Font.grp': (1036825, '391e8b1eebb4304bb3426a564f248e347e564d5c9e9f20a169f05b7522a49d87'),
    'BMP/Hell/MenuMsg.grp': (277855, '2ee492082f3bb677178a20086beed9693b397e9709bf682a10a266f9b7e75bf6'),
    'BMP/Hell/hell_Menu03.grp': (94201, 'd4232bc336894b9dc4ff8b081ab7bf609f31e102a8b48b9310d3cf3263d01df1'),
    'Dat/Cpu/genha.cpu': (2648, '00a9a0ec65c96d61ce2b54ac327ae5f2dda2ac8c893ac026764ec036ccd7186b'),
    'Dat/Cpu/kaira.cpu': (1496, '830640626d5abdb27574109cdcae4ae251093cba85c1418495a26e3948b3db38'),
    'Dat/Cpu/zako_s_t01.cpu': (638, '25a9f7d9c0ca9a4d234480b27cb1abdd95ff2517680c863b5568c4d6d144fb87'),
    'Dat/Waza/neko05.waz': (32534, '5b44d6725fe6c8d73e6a05c23a1e2da64a6e5bce340bbe0c29ef29853d5a5256'),
    'Dat/Waza/TOORU.WAZ': (349982, '7f3b5b46284c4727cc49627c43ff9d1fb4b34534f4921106a084122f7916825c'),
    'Dat/Waza/YAGISAWA.WAZ': (42194, 'ce25737bc3469600503ef957140e9020b5747be4b46d17830b19757268d082ca'),
}
EXPECTED_DAT = {
    'CGInfo.DAT': (29520, '7606170270208a8c4cf3474ca07d36959c22183d37141ecd7fa151d6939e77f6'),
    'ChapterFlowInfo.DAT': (9672, '73dda6d298ce684eaee78011e160cc4b0c976671146c5193f7c575fcd8f49618'),
    'DatabaseInfo.DAT': (69972, 'e6f37b331a00605b406a13fa488026cf9888885f135adf81844a9875baa455f6'),
    'EquipmentInfo.DAT': (20400, 'd23f82886ed90b94a786b10ab8c32814dcabfba3ff0e6700d0d9007bc5bc2a14'),
    'FlgInfo.DAT': (7680, '076ae289b45c1563d265eef278a44b8b20c6b356b1b7ff6bcdaa900bd161e871'),
    'FlowInfo.DAT': (54768, '1e8597d5fc2c7714b4c67d2e9662a8a8ce5781cc9eeb232197b496a246605894'),
    'ItemInfo.DAT': (14600, 'c7d5fd68fb9e159c376a22b4afb6057b72a35d72e26d256a7977e6e4e92c6631'),
    'ReplayInfo.DAT': (3072, '6905a45871e486eb116f88e19ff658e15e1fa14f066f7a520015bfa94608cf03'),
    'SceneInfo.DAT': (11152, 'd97dbf904ef8bf8cd3103d48847adaa00491cf7de9b76b60ff2fbff8454ca509'),
    'VisualInfo.DAT': (177984, 'cc942f2fc0ebdb22be61f8910477861a19578e807dc0232ff1930603846b4ca9'),
}

def sha256(p: Path) -> str:
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024), b''): h.update(b)
    return h.hexdigest()
def u16(b,o): return struct.unpack_from('<H',b,o)[0]
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def parse_pe(data: bytes):
    if data[:2] != b'MZ': raise ValueError('MZ missing')
    pe=u32(data,0x3c)
    if data[pe:pe+4] != b'PE\0\0': raise ValueError('PE missing')
    machine=u16(data,pe+4); nsec=u16(data,pe+6); chars=u16(data,pe+22)
    opt=pe+24; magic=u16(data,opt); optsz=u16(data,pe+20); image_base=u32(data,opt+28) if magic==0x10b else 0
    dd=opt+(96 if magic==0x10b else 112); dirs=[(u32(data,dd+i*8),u32(data,dd+i*8+4)) for i in range(16)]
    secs=[]; sh=opt+optsz
    for i in range(nsec):
        o=sh+i*40; name=data[o:o+8].split(b'\0',1)[0].decode('ascii','replace')
        secs.append((name,u32(data,o+12),u32(data,o+8),u32(data,o+20),u32(data,o+16)))
    return {'machine':machine,'magic':magic,'chars':chars,'image_base':image_base,'dirs':dirs,'secs':secs}
def rva_to_off(pe,rva):
    for _,va,vs,raw,rawsz in pe['secs']:
        if va <= rva < va+max(vs,rawsz):
            d=rva-va
            if d>=rawsz: raise ValueError(f'RVA 0x{rva:X} no file bytes')
            return raw+d
    raise ValueError(f'RVA 0x{rva:X} unmapped')
def off_to_rva(pe,off):
    for _,va,vs,raw,rawsz in pe['secs']:
        if raw<=off<raw+rawsz:return va+(off-raw)
    return off
def fnv32(x: bytes):
    h=2166136261
    for c in x:h=((h^c)*16777619)&0xffffffff
    return h
def check(label,ok,detail=''):
    print(f"[{'PASS' if ok else 'FAIL'}] {label}"+(f': {detail}' if detail else ''));return bool(ok)

def parse_ui(path):
    rx=re.compile(r'^\s*\{0x([0-9A-Fa-f]+)UL,(\d+),(\d+),0x([0-9A-Fa-f]+)UL,\{([^}]*)\}\},',re.M)
    rows=[]; txt=path.read_text('utf-8')
    for m in rx.finditer(txt):
        vals=[int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)',m.group(5))]
        rows.append((int(m.group(1),16),int(m.group(2)),int(m.group(3)),int(m.group(4),16),bytes(vals[:int(m.group(3))])))
    return rows

def parse_norm(path):
    rx=re.compile(r'^\s*\{0x([0-9A-Fa-f]+)UL,(\d+),0x([0-9A-Fa-f]+)UL,\{([^}]*)\}\},',re.M)
    rows=[]; txt=path.read_text('utf-8')
    for m in rx.finditer(txt):
        ln=int(m.group(2)); vals=[int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)',m.group(4))]
        rows.append((int(m.group(1),16),ln,int(m.group(3),16),bytes(vals[:ln])))
    return rows

def parse_legacy(path):
    rx=re.compile(r'^\s*\{0x([0-9A-Fa-f]+)UL,(\d+),(\d+),(\d+),0x([0-9A-Fa-f]+)UL,\{([^}]*)\}\},',re.M)
    rows=[]; txt=path.read_text('utf-8')
    for m in rx.finditer(txt):
        dl=int(m.group(3)); vals=[int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)',m.group(6))]
        rows.append((int(m.group(1),16),int(m.group(2)),dl,int(m.group(4)),int(m.group(5),16),bytes(vals[:dl])))
    return rows

def pac_entries(data: bytes):
    if data[:4] != b'PACw': raise ValueError('PACw missing')
    count=u32(data,4)
    out={}
    for i in range(count):
        e=12+i*76
        name=data[e:e+64].split(b'\0',1)[0].decode('ascii','replace')
        off,dec,enc=struct.unpack_from('<III',data,e+64)
        if off+enc>len(data): raise ValueError(f'PAC entry out of bounds: {name}')
        out[name.lower()] = (dec,enc,data[off:off+enc])
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('exe',type=Path);a=ap.parse_args();ok=True
    exe=a.exe.read_bytes();pe=parse_pe(exe);got=sha256(a.exe)
    ok &= check('clean EXE SHA256',got==EXPECTED_EXE_SHA256,got)
    ok &= check('clean EXE PE32 i386',pe['machine']==0x14c and pe['magic']==0x10b)
    ui=parse_ui(ROOT/'source/ui_patch_data_gbk.h')
    legacy=parse_legacy(ROOT/'source/legacy_static_patch_data_gbk.h')
    norm=parse_norm(ROOT/'source/encoding_normalization_gbk.h')
    late=parse_legacy(ROOT/'source/late_static_patch_data_gbk.h')
    ok &= check('merged UI table count',len(ui)==211,str(len(ui)))
    ok &= check('WideSweep enabled real-text count',len(legacy)==662,str(len(legacy)))
    ok &= check('encoding-only normalization count',len(norm)==168,str(len(norm)))
    ok &= check('late verified static count',len(late)==144,str(len(late)))
    bad=[]
    for rva,sl,dl,hv,dst in ui:
        off=rva_to_off(pe,rva);src=exe[off:off+sl]
        if fnv32(src)!=hv or exe[off+sl]!=0 or dl>sl:bad.append(hex(rva))
    ok &= check('211 UI source/hash/slot verification',not bad,','.join(bad[:8]))
    bad=[]; pad=[]
    for rva,sl,dl,wl,hv,dst in legacy:
        off=rva_to_off(pe,rva);src=exe[off:off+sl]
        if fnv32(src)!=hv or exe[off+sl]!=0 or wl!=max(sl,dl)+1:bad.append(hex(rva));continue
        if dl>sl and any(exe[off+sl:off+wl]):pad.append(hex(rva))
    ok &= check('662 WideSweep source/hash verification',not bad,','.join(bad[:8]))
    ok &= check('oversize translations have verified zero padding',not pad,','.join(pad[:8]))
    latebad=[]; latepad=[]
    for rva,sl,dl,wl,hv,dst in late:
        off=rva_to_off(pe,rva);src=exe[off:off+sl]
        if fnv32(src)!=hv or exe[off+sl]!=0 or wl!=max(sl,dl)+1:latebad.append(hex(rva));continue
        if dl>sl and any(exe[off+sl:off+wl]):latepad.append(hex(rva))
    ok &= check('144 late static source/hash verification',not latebad,','.join(latebad[:8]))
    ok &= check('late static oversize rows have zero padding',not latepad,','.join(latepad[:8]))
    nbad=[]
    for rva,ln,hv,dst in norm:
        off=rva_to_off(pe,rva);src=exe[off:off+ln]
        if fnv32(src)!=hv or exe[off+ln]!=0 or len(dst)!=ln:nbad.append(hex(rva))
        else:
            try:
                if src.decode('cp932')!=dst.decode('gbk'):nbad.append(hex(rva))
            except UnicodeError:nbad.append(hex(rva))
    ok &= check('168 encoding-only source/hash/Unicode parity verification',not nbad,','.join(nbad[:8]))
    raw=list(csv.DictReader((ROOT/'data/battle_system_aligned_mapping_raw_871.csv').open(encoding='utf-8-sig')))
    man=list(csv.DictReader((ROOT/'data/battle_system_patch_manifest.csv').open(encoding='utf-8-sig')))
    en=sum(x['status']=='enabled_text' for x in man);ex=sum(x['status']=='excluded_structural_false_positive' for x in man)
    ok &= check('raw WideSweep candidate count',len(raw)==871,str(len(raw)))
    ok &= check('manifest count/status',len(man)==871 and en==662 and ex==209,f'{len(man)} / enabled={en} / excluded={ex}')
    # Verify all raw source strings truly match the clean EXE; exclusion is semantic/structural, not bad source extraction.
    rawbad=[]
    for i,r in enumerate(raw):
        jb=r['japanese'].encode('cp932');off=rva_to_off(pe,int(r['orig_rva'],16))
        if exe[off:off+len(jb)]!=jb or exe[off+len(jb)]!=0:rawbad.append(str(i))
    ok &= check('all 871 raw source byte alignments match clean EXE',not rawbad,','.join(rawbad[:8]))
    for name,(sig,expected_va) in SIGS.items():
        poss=[];start=0
        while True:
            i=exe.find(sig,start)
            if i<0:break
            poss.append(i);start=i+1
        vas=[pe['image_base']+off_to_rva(pe,o) for o in poss]
        ok &= check(f'{name} signature unique/address',len(vas)==1 and vas[0]==expected_va,','.join(f'0x{x:08X}' for x in vas) or 'none')
    # 逐项检查 test12 五个“旧汉化等价补丁”的 clean 原字节。
    # 注意：这是对磁盘基线做静态检查；真正运行时仍会在写入前再次一次性验证五处。
    for label,(va,expected) in OLD_LOCALIZATION_PATCH_BYTES.items():
        rva=va-pe['image_base'];off=rva_to_off(pe,rva);got_bytes=exe[off:off+len(expected)]
        ok &= check(label,got_bytes==expected,got_bytes.hex(' ').upper())
    for label,p,sz,hv in [('Update.pac',ROOT/'release/Update.pac',EXPECTED_UPDATE_SIZE,EXPECTED_UPDATE_SHA256),('Chapter.pac',ROOT/'release/Chapter.pac',EXPECTED_CHAPTER_SIZE,EXPECTED_CHAPTER_SHA256)]:
        ok &= check(f'{label} exists',p.exists())
        if p.exists():
            ok &= check(f'{label} size',p.stat().st_size==sz,str(p.stat().st_size));g=sha256(p);ok &= check(f'{label} SHA256',g==hv,g)
    upd=ROOT/'release/Update.pac'
    if upd.exists():
        entries=pac_entries(upd.read_bytes())
        for name,(esz,ehash) in EXPECTED_DAT.items():
            row=entries.get(name.lower())
            good=bool(row and row[0]==row[1]==esz and hashlib.sha256(row[2]).hexdigest()==ehash)
            ok &= check(f'localized DAT oracle {name}',good,('missing' if not row else f'{row[0]}/{row[1]}'))
    # v1.0.4 恢复 BaldrForceCN.ini，但只允许字体和日志两个安全设置。
    # loose 资源仍全部内嵌，不允许重新出现 BaldrForceCN_data 目录。
    release_files=sorted(str(x.relative_to(ROOT/'release')).replace('\\','/') for x in (ROOT/'release').rglob('*') if x.is_file())
    ok &= check('release contains ASI + INI + Update.pac + Chapter.pac only',
                release_files==['BaldrForceCN.asi','BaldrForceCN.ini','Chapter.pac','Update.pac'],','.join(release_files))
    ok &= check('BaldrForceCN.ini restored in release',(ROOT/'release/BaldrForceCN.ini').exists())
    ok &= check('BaldrForceCN_data removed from release',not (ROOT/'release/BaldrForceCN_data').exists())

    # Verify every source asset against the old-VFS oracle before checking the linked ASI.
    asset_payloads={}
    for rel,(esz,ehash) in EXPECTED_EMBEDDED_ASSETS.items():
        apath=ROOT/'source/assets'/Path(rel)
        exists=apath.exists()
        ok &= check(f'embedded source asset exists: {rel}',exists)
        if exists:
            payload=apath.read_bytes();asset_payloads[rel]=payload
            good=len(payload)==esz and hashlib.sha256(payload).hexdigest()==ehash
            ok &= check(f'embedded source asset size/hash: {rel}',good,f'{len(payload)} / {hashlib.sha256(payload).hexdigest()}')

    asi=ROOT/'release/BaldrForceCN.asi';ok &= check('BaldrForceCN.asi exists',asi.exists())
    if asi.exists():
        ad=asi.read_bytes();ape=parse_pe(ad);ok &= check('ASI PE32 i386 DLL',ape['machine']==0x14c and ape['magic']==0x10b and bool(ape['chars']&0x2000))
        irva,isz=ape['dirs'][1];ok &= check('ASI import directory empty',irva==0 and isz==0,f'RVA=0x{irva:X}, size={isz}');print('[INFO] ASI SHA256:',sha256(asi))
        # Because embedded_assets.S uses .incbin, each original file should appear byte-for-byte inside the ASI.
        # count==1 catches both a missing asset and accidental duplicate embedding.
        for rel,payload in asset_payloads.items():
            count=ad.count(payload)
            ok &= check(f'ASI contains exact embedded asset once: {rel}',count==1,str(count))
    source=(ROOT/'source/BaldrForceCN.c').read_text('utf-8')
    ok &= check('v1.0.4-crashfix1 version marker','BaldrForceCN 中文运行时 ASI v1.0.4-crashfix1' in source)
    ok &= check('old localization global DBCS rule','if(a<0x80||a>0xFE)return 0;' in source and 'old-localization global 0x80..0xFE DBCS renderer active' in source)
    ok &= check('exact script resolver present','lookup_ja_script' in source and 'exact BFET script identity/offset resolver active' in source)
    ok &= check('opcode-0x0B source-count/runtime-count separation',
                '*(DWORD*)(state+0x114)=cur+count' in source and
                '*(DWORD*)(dest-4)=outn+1U' in source and
                'g_script_runtime_count_updated' in source and
                'if(count>limit-cur)' in source)
    ok &= check('merged static patch call present','apply_legacy_static_gbk_patches();' in source)
    ok &= check('encoding normalization call present','apply_encoding_normalization_patches();' in source)
    ok &= check('late static patch call present','apply_late_static_gbk_patches();' in source)
    ok &= check('DAT overlay initialization/hook present','init_resource_overlays()' in source and 'install_resource_overlay_hook()' in source and 'Hook_EngineOpenResource' in source)
    ok &= check('embedded loose-asset redirect present',
                'lookup_embedded_asset' in source and 'open_embedded_asset_as_temp_file' in source and
                'Dat\\Waza\\TOORU.WAZ' in source and 'Bmp\\Hell\\MenuMsg.grp' in source and 'Dat\\Cpu\\genha.cpu' in source and 'Dat\\Waza\\YAGISAWA.WAZ' in source)
    ok &= check('INI exposes only safe font/log settings while architecture stays fixed',
                'load_runtime_config' in source and 'GetPrivateProfileStringW' in source and
                'FontFace' in source and 'EnableLog' in source and
                'g_enable_font_hook = 1' in source and 'g_enable_wctomb_hook = 1' in source and
                'g_force_charset = GB2312_CHARSET' in source and 'g_context_bytes = 128' in source)
    ok &= check('Unicode GBK glyph fallback present',
                'query_gbk_outline' in source and 'GetGlyphOutlineW' in source and
                'make_safe_blank_glyph' in source and '不回退 CP932' in source)
    ok &= check('after.bin runtime-hit log present',
                '[命中] after.bin 直接 GBK 脚本文本已执行（运行时已确认）' in source and
                'g_after_bin_direct_runtime_hits' in source)
    asm=(ROOT/'source/embedded_assets.S').read_text('utf-8')
    ok &= check('embedded assembly manifest present',
                '.incbin "source/assets/Dat/Waza/TOORU.WAZ"' in asm and
                '.incbin "source/assets/BMP/Hell/MenuMsg.grp"' in asm and
                '.incbin "source/assets/BMP/Hell/Font.grp"' in asm and
                '.incbin "source/assets/Dat/Cpu/genha.cpu"' in asm and
                '.incbin "source/assets/Dat/Waza/YAGISAWA.WAZ"' in asm)
    ok &= check('old .zeas normal glyph placement parity','voff=tm.tmAscent-gm.gmptGlyphOrigin.y' in source and 'rr=(LONG)size-voff-(LONG)y' in source)
    ok &= check('old .zeas low-level 1bpp mask path present','RendererGetGBKMask' in source and 'install_gbk_draw_mask_hooks' in source)
    # test12 regression guard: restoring only the lookup state is not enough. test10 captured
    # CF | E0C2 | ED20 | CD | B8 for GBK “相马透”, proving the caller had already split the
    # bytes with Shift-JIS rules.  Therefore the formal source must contain all three legacy
    # A0->FF routing sites AND both lookup-body state-write sites.
    ok &= check('low-level old-localization routing/state patch present',
                'install_lowlevel_oldlocalization_patches' in source and
                'g_lowlevel_current_code' in source and
                'base+0x000154ADUL' in source and
                'base+0x00015B2DUL' in source and
                'base+0x000170BFUL' in source and
                'base+0x000155E2UL' in source and
                'base+0x0001572DUL' in source and
                'RendererGetGBKMask(current_code' in source)
    ok &= check('test10 entry-capture workaround removed',
                'install_lowlevel_code_capture_hooks' not in source and
                'make_lowlevel_code_capture_stub' not in source)
    ok &= check('limited low-level glyph trace present',
                'g_gbk_mask_builds<12' in source and
                '[TRACE] synchronized low-level mask #' in source)
    # The log is required to follow BaldrForceCN.asi.  Check that make_paths queries the DLL
    # module handle instead of using NULL for the log directory.
    ok &= check('log path follows ASI module',
                'pGetModuleFileNameW(g_self_module,g_asi_dir' in source and
                'w_copy(g_log_path,g_asi_dir' in source)
    banned=['CARRIER_','carrier_map','build_carrier'];found=[x for x in banned if x in source]
    ok &= check('formal source has no carrier implementation',not found,','.join(found))
    print('RESULT:','PASS' if ok else 'FAIL');return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
