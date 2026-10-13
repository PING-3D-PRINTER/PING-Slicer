# -*- coding: utf-8 -*-
"""crash_symbolize.py — 把 PING Slicer 的 crash log 位址解成「函式 + 原始碼行號」。

用途：Windows 版閃退時 %APPDATA%\\PingSlicer\\log\\crash_*.log 會記下錯誤位址，
但 log 裡印的符號名是「最近的匯出符號」＝幾乎都是騙人的（hid_write、BRepExtrema…）。
真正有用的是 `0x1:0x325DFFD` 這種 `section:offset`，配上同一顆 build 的 PDB 就能解回行號。

前提：**PDB 必須來自同一次 build**（不同 build 的 RVA 完全對不上，會解出無關的函式）。
      CI 產物在 GitHub Actions 該次 run 的 `PDB` artifact（Debug_PDB_*.7z，約 145MB → 解開 1.8GB）：
          gh run list --limit 20
          gh run download <run-id> -n PDB -D <目錄>
          7z x Debug_PDB_*.7z
      驗證同版：PDB 檔的時間戳應與安裝目錄 OrcaSlicer.dll 一致。

用法：
    python crash_symbolize.py <OrcaSlicer.pdb> --log <crash_xxx.log>
    python crash_symbolize.py <OrcaSlicer.pdb> 325DFFD 2805F27        # 直接給 section 1 的 offset
    python crash_symbolize.py <OrcaSlicer.pdb> 1:325DFFD 1:2805F27    # 指定 section

每個位址印三種資訊（有才印）：
    第一行   公開符號 +偏移、檔案:行號（行號是真的；**公開符號名在編譯器合併函式時是假的**）
    -> 函式   從該 obj 的模組符號找出**真正包住這個位址的函式**
    -> lambda 函式名裡有 `<lambda_…>` 時，查出那個 lambda **定義在哪個檔第幾行**

為什麼要後兩行（2026-09-19 報價 smoke 暖機閃退實錄）：崩點第一行解成
`?Copy@?$wxVector@H@@… +0x1C4  wx\\event.h:1550`——公開符號是編譯器合併後的假名，
行號只說「在某個 CallAfter 的 lambda 裡」。當時兩份 crash log 因此被判成「同樣的崩潰」，
其實是**兩個不同的 lambda**（Tab.cpp:1304 與 GUI_App.cpp:1220）。有了 -> 兩行就一眼分得出來。

本檔自己解 MSF/PDB，不依賴 WinDbg／DIA SDK／LLVM——那台機器上一個都沒有。
2026-08-28 首次使用：解出 T033 閃退 = GLGizmoMove.cpp:250（instances[-1] 垃圾指標）。
"""
import bisect
import mmap
import re
import struct
import sys

S_PUB32 = 0x110E
DBI_HEADER_SIZE = 64

# 模組符號流裡的函式記錄（有 _ID 的是 /DEBUG:FASTLINK 以外的新格式，兩種都會出現）
S_LPROC32, S_GPROC32, S_LPROC32_ID, S_GPROC32_ID = 0x110F, 0x1110, 0x1146, 0x1147
PROC_KINDS = (S_LPROC32, S_GPROC32, S_LPROC32_ID, S_GPROC32_ID)

# 型別（TPI＝stream 2）與 ID（IPI＝stream 4）記錄
LF_CLASS, LF_STRUCTURE = 0x1504, 0x1505
LF_STRING_ID, LF_UDT_SRC_LINE, LF_UDT_MOD_SRC_LINE = 0x1605, 0x1606, 0x1607

LAMBDA_RE = re.compile(r"<lambda_([0-9a-f]{32})>")


class Pdb:
    def __init__(self, path):
        self._f = open(path, "rb")
        self.mm = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)
        if not self.mm[:24].startswith(b"Microsoft C/C++ MSF 7.00"):
            raise SystemExit("不是 MSF 7.00 格式的 PDB：%s" % path)

        (self.block_size, _free, _nblocks, dir_bytes,
         _unk, block_map_addr) = struct.unpack_from("<6I", self.mm, 32)

        n_dir_blocks = (dir_bytes + self.block_size - 1) // self.block_size
        dir_ids = struct.unpack_from("<%dI" % n_dir_blocks, self.mm, block_map_addr * self.block_size)
        directory = self._read_blocks(dir_ids, dir_bytes)

        n = struct.unpack_from("<I", directory, 0)[0]
        self.sizes = list(struct.unpack_from("<%dI" % n, directory, 4))
        pos = 4 + 4 * n
        self.blocks = []
        for size in self.sizes:
            if size == 0xFFFFFFFF:
                size = 0
            nb = (size + self.block_size - 1) // self.block_size
            self.blocks.append(struct.unpack_from("<%dI" % nb, directory, pos))
            pos += 4 * nb

        self._parse_dbi()
        self._parse_publics()
        self._parse_names()

    # ---------- MSF ----------
    def _read_blocks(self, ids, size):
        out = bytearray()
        for b in ids:
            out += self.mm[b * self.block_size:(b + 1) * self.block_size]
            if len(out) >= size:
                break
        return bytes(out[:size])

    def stream(self, idx):
        size = self.sizes[idx]
        if size == 0xFFFFFFFF:
            size = 0
        return self._read_blocks(self.blocks[idx], size)

    # ---------- DBI：模組表 + section contribution ----------
    def _parse_dbi(self):
        dbi = self.stream(3)
        # 欄位序：vsig, vhdr, age, gsi, buildno, psi, dllver, symrec, dllrbld, modinfo_size, …
        hdr = struct.unpack_from("<iIIHHHHHHiiiiiIiiHHI", dbi, 0)
        self.sym_record_stream = hdr[7]
        modinfo_size, seccontrib_size = hdr[9], hdr[10]

        # ModInfo：名稱固定在 record 起點 +64，整筆 4-byte 對齊
        self.mods = []
        p, end = DBI_HEADER_SIZE, DBI_HEADER_SIZE + modinfo_size
        while p < end:
            _flags, symstream, symbytes, c11, c13, _nsrc = struct.unpack_from("<HhIIIH", dbi, p + 32)
            q = p + 64
            e = dbi.index(b"\0", q); name = dbi[q:e].decode("utf-8", "replace"); q = e + 1
            e = dbi.index(b"\0", q); obj = dbi[q:e].decode("utf-8", "replace"); q = e + 1
            self.mods.append(dict(symstream=symstream, symbytes=symbytes, c11=c11, c13=c13,
                                  name=name, obj=obj))
            p = q + ((4 - (q % 4)) % 4)

        # SectionContribution：(section, offset) → 模組
        self.contribs = []
        p = DBI_HEADER_SIZE + modinfo_size + 4          # +4 跳過 version
        end = DBI_HEADER_SIZE + modinfo_size + seccontrib_size
        while p + 28 <= end:
            sec, _p1, off, size, _ch, modidx, _p2, _d, _r = struct.unpack_from("<HHiiIHHII", dbi, p)
            self.contribs.append((sec, off, size, modidx))
            p += 28
        self.contribs.sort()

    def find_module(self, sec, off):
        i = bisect.bisect_right(self.contribs, (sec, off, 1 << 40, 1 << 20)) - 1
        if i < 0:
            return None
        s, o, size, modidx = self.contribs[i]
        return modidx if (s == sec and o <= off < o + size) else None

    # ---------- 公開符號（函式名） ----------
    def _parse_publics(self):
        sym = self.stream(self.sym_record_stream)
        self._sym = sym
        self.pubs = {}
        off, n = 0, len(sym)
        while off + 4 <= n:
            ln, kind = struct.unpack_from("<HH", sym, off)
            if ln < 2:
                break
            if kind == S_PUB32:
                _flags, o, seg = struct.unpack_from("<IIH", sym, off + 4)
                self.pubs.setdefault(seg, []).append((o, off + 14))
            off += ln + 2
        for seg in self.pubs:
            self.pubs[seg].sort()

    def symbol(self, sec, off):
        arr = self.pubs.get(sec)
        if not arr:
            return None
        i = bisect.bisect_right(arr, (off, 1 << 62)) - 1
        if i < 0:
            return None
        o, p = arr[i]
        e = self._sym.index(b"\0", p)
        return self._sym[p:e].decode("utf-8", "replace"), off - o

    # ---------- /names 字串表（檔名） ----------
    def _parse_names(self):
        info = self.stream(1)
        p = 12 + 16                                     # ver/sig/age + GUID
        strsize, = struct.unpack_from("<I", info, p); p += 4
        strbuf = info[p:p + strsize]; p += strsize
        cnt, _cap = struct.unpack_from("<II", info, p); p += 8
        for _ in range(2):                              # present / deleted bit vectors
            words, = struct.unpack_from("<I", info, p); p += 4 + 4 * words
        named = {}
        for _ in range(cnt):
            ni, si = struct.unpack_from("<II", info, p); p += 8
            e = strbuf.index(b"\0", ni)
            named[strbuf[ni:e].decode()] = si
        self._names = self.stream(named["/names"])

    def _name_at(self, off):
        base = 12                                       # sig + ver + size
        e = self._names.index(b"\0", base + off)
        return self._names[base + off:e].decode("utf-8", "replace")

    # ---------- 行號（C13 line info） ----------
    def line(self, sec, off):
        modidx = self.find_module(sec, off)
        if modidx is None:
            return None
        m = self.mods[modidx]
        if m["symstream"] < 0:
            return dict(mod=m["name"])
        ms = self.stream(m["symstream"])
        start = m["symbytes"] + m["c11"]
        c13 = ms[start:start + m["c13"]]

        files, best, p = {}, None, 0
        while p + 8 <= len(c13):
            kind, size = struct.unpack_from("<II", c13, p); p += 8
            data = c13[p:p + size]
            if kind == 0xF4:                            # DEBUG_S_FILECHKSMS
                q = 0
                while q + 8 <= len(data):
                    nameoff, cbsz, _ctype = struct.unpack_from("<IBB", data, q)
                    files[q] = nameoff
                    q += 6 + cbsz
                    q += (4 - q % 4) % 4
            elif kind == 0xF2:                          # DEBUG_S_LINES
                loff, lseg, _flags, lcode = struct.unpack_from("<IHHI", data, 0)
                if lseg == sec and loff <= off < loff + lcode:
                    q = 12
                    while q + 12 <= len(data):
                        fidx, nlines, blocksz = struct.unpack_from("<III", data, q)
                        for i in range(nlines):
                            lo, lf = struct.unpack_from("<II", data, q + 12 + 8 * i)
                            addr = loff + lo
                            if addr <= off and (best is None or addr > best[0]):
                                best = (addr, lf & 0xFFFFFF, fidx)
                        q += blocksz
            p += size
            p += (4 - p % 4) % 4

        res = dict(mod=m["name"])
        if best:
            res.update(line=best[1], file=self._name_at(files.get(best[2], 0)))
        return res

    # ---------- 真正包住位址的函式（模組符號流的 S_*PROC32） ----------
    def proc(self, sec, off):
        """回 (函式名, 函式內偏移)。公開符號遇到編譯器合併（ICF）會給假名，這裡給的是真的。
        沒有模組符號（例：wx 的 obj 沒帶除錯資訊）就回 None。"""
        modidx = self.find_module(sec, off)
        if modidx is None:
            return None
        m = self.mods[modidx]
        if m["symstream"] < 0:
            return None
        ms = self.stream(m["symstream"])
        p, end = 4, m["symbytes"]                       # +4 跳過 CV signature
        while p + 4 <= end:
            ln, kind = struct.unpack_from("<HH", ms, p)
            if kind in PROC_KINDS:
                # PROCSYM32：pParent, pEnd, pNext, len, DbgStart, DbgEnd, typind, off, seg, flags, name
                pend, = struct.unpack_from("<I", ms, p + 8)
                plen, = struct.unpack_from("<I", ms, p + 16)
                poff, pseg = struct.unpack_from("<IH", ms, p + 32)
                if pseg == sec and poff <= off < poff + plen:
                    e = ms.index(b"\0", p + 39)
                    return ms[p + 39:e].decode("utf-8", "replace"), off - poff
                if pend > p:                            # 不是它 ⇒ 整個函式的子記錄一次跳過
                    p = pend
                    ln, _ = struct.unpack_from("<HH", ms, p)
            p += ln + 2
        return None

    # ---------- lambda 定義在哪（TPI 類別記錄 ＋ IPI 的 UDT 原始碼行） ----------
    @staticmethod
    def _records(s):
        hdr_size, ti_begin = struct.unpack_from("<II", s, 4)
        rec_bytes, = struct.unpack_from("<I", s, 16)
        p, end = hdr_size, hdr_size + rec_bytes
        while p < end:
            ln, kind = struct.unpack_from("<HH", s, p)
            yield ti_begin, kind, p + 4, p + 2 + ln
            ti_begin += 1
            p += ln + 2

    def lambda_sources(self, hashes):
        """{lambda hash: [(完整型別名, 檔案, 行號), …]}。只在真的遇到 lambda 時才掃（TPI 很大）。"""
        if not hashes:
            return {}
        want = {h: h.encode() for h in hashes}
        tpi = self.stream(2)
        types = {}                                      # 型別索引 → (hash, 名稱)
        for ti, kind, a, b in self._records(tpi):
            if kind not in (LF_CLASS, LF_STRUCTURE):
                continue
            rec = tpi[a:b]
            hit = next((h for h, hb in want.items() if hb in rec), None)
            if hit is None:
                continue
            v, = struct.unpack_from("<H", rec, 16)      # 數值葉（大小）之後才是名稱
            q = 18 if v < 0x8000 else 18 + {0x8000: 1, 0x8001: 2, 0x8002: 2, 0x8003: 4,
                                               0x8004: 4, 0x8009: 8, 0x800A: 8}.get(v, 4)
            e = rec.index(b"\0", q)
            types[ti] = (hit, rec[q:e].decode("utf-8", "replace"))

        ipi = self.stream(4)
        ipi_recs = list(self._records(ipi))
        ipi_begin = ipi_recs[0][0] if ipi_recs else 0
        out = {}
        for _ti, kind, a, _b in ipi_recs:
            if kind not in (LF_UDT_SRC_LINE, LF_UDT_MOD_SRC_LINE):
                continue
            udt, src, line = struct.unpack_from("<III", ipi, a)
            if udt not in types:
                continue
            h, name = types[udt]
            if kind == LF_UDT_MOD_SRC_LINE:
                path = self._name_at(src)               # /names 偏移
            else:
                _t, sk, sa, sb = ipi_recs[src - ipi_begin]
                e = ipi.index(b"\0", sa + 4)
                path = ipi[sa + 4:e].decode("utf-8", "replace") if sk == LF_STRING_ID else "?"
            # 同一個 lambda 會有「lambda 本體」與「包它的 functor 模板」兩筆，本體那筆才指向定義處
            out.setdefault(h, []).append((name, path, line))
        return out


def addresses_from_log(path, module="OrcaSlicer.dll"):
    """撈出 crash log 裡屬於指定模組的 `section:offset`，依出現順序（＝由上而下的呼叫堆疊）。

    「Show CallStack」那段印的是絕對位址＋假符號名（解到最近的匯出符號，幾乎都無關），
    真正可用的是「Fault address」與「Logical Address」段的 `0xF829EFFD 0x1:0x325DFFD <模組路徑>`。
    其他模組（KERNEL32／ntdll／ping-slicer.exe）的 offset 拿這顆 PDB 解會得到無關的函式，故過濾掉。
    """
    out, seen = [], set()
    pat = re.compile(r"0x[0-9A-Fa-f]+\s+0x([0-9A-Fa-f]+):0x([0-9A-Fa-f]+)\s+(\S.*)$")
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for row in fh:
            m = pat.search(row.rstrip())
            if not m or module.lower() not in m.group(3).lower():
                continue
            key = (int(m.group(1), 16), int(m.group(2), 16))
            if key not in seen:
                seen.add(key)
                out.append(key)
    return out


def main():
    # 主控台多半是 cp950：符號名裡偶有它編不了的字，寧可印成 ? 也不要整支中斷
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    pdb_path, rest = args[0], args[1:]

    targets = []
    if rest and rest[0] == "--log":
        targets = addresses_from_log(rest[1])
    else:
        for a in rest:
            sec, _, off = a.rpartition(":")
            targets.append((int(sec, 16) if sec else 1, int(off, 16)))
    if not targets:
        raise SystemExit("沒有要解析的位址。")

    pdb = Pdb(pdb_path)
    rows = []
    for sec, off in targets:
        rows.append((sec, off, pdb.symbol(sec, off), pdb.line(sec, off), pdb.proc(sec, off)))

    hashes = {h for *_x, pr in rows if pr for h in LAMBDA_RE.findall(pr[0])}
    lambdas = pdb.lambda_sources(hashes)

    for sec, off, sym, ln, pr in rows:
        where = ""
        if ln and ln.get("line"):
            where = "  %s:%d" % (ln["file"], ln["line"])
        elif ln:
            where = "  [%s]" % ln["mod"]
        print("0x%X:0x%08X  %s%s" % (
            sec, off,
            ("%s +0x%X" % sym) if sym else "<no symbol>",
            where))
        if pr:
            print("            -> 函式：%s +0x%X" % pr)
            for h in LAMBDA_RE.findall(pr[0]):
                for name, path, line in lambdas.get(h, []):
                    if name.endswith("<lambda_%s>" % h):   # 只印 lambda 本體（不印包它的 functor 模板）
                        print("            -> lambda 定義於：%s:%d  （%s）" % (path, line, name))


if __name__ == "__main__":
    main()
