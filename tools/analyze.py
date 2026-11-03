"""Control-flow discovery for OS-9 program modules.

Produces a set of decoded instructions reachable from the entry point and
other code pointers, plus a listing for inspection.
"""
import struct
import sys
from m68k import decode, DecodeError, CC
from os9mod import Module

MODULE_BASE = 0x00100000   # default; per-module bases come from game.json

# os9 calls that never return
NORETURN_OS9 = {0x06, 0x1e}  # F$Exit, F$RTE


class Analysis:
    def __init__(self, mod, extra_entries=(), extra_tables=(), a5=True):
        self.mod = mod
        self.base = mod.base
        self.code_lo = self.base + 0x48
        self.code_hi = self.base + mod.idataoff
        self.insns = {}
        self.funcs = set()
        self.labels = set()
        self.errors = {}
        self.jumptables = {}   # insn addr -> list of targets
        self.indirect = set()  # unresolved indirect jumps/calls
        self.code_ptrs = set()
        self.extra_tables = list(extra_tables)
        if a5 == 'auto':
            a5 = uses_a5_table(mod)
        self.uses_a5 = bool(a5)
        self.a5table = a5_jumptable(mod) if a5 else {}
        # Microware-style programs: 'jmp abs.l' stubs in static storage, called
        # with jsr d16(a6)
        self.a6table = {k: v for k, v in a6_jumptable(mod).items() if self.in_code(v)}
        self.pcrel_calls = {}  # jsr/jmp d8(pc,dn.l) with dn loaded by move.l #imm -> target
        work = [self.base + mod.execoff]
        for t in set(self.a5table.values()) | set(self.a6table.values()):
            work.append(t)
            self.funcs.add(t)
        self.funcs.add(self.base + mod.execoff)
        if mod.excptoff:
            work.append(self.base + mod.excptoff)
            self.funcs.add(self.base + mod.excptoff)
        for e in extra_entries:
            work.append(e)
            self.funcs.add(e)
        self.work = work
        self.run()
        # code pointers in initialized data (relocated by the code irefs)
        self.code_ptrs |= idata_code_ptrs(mod, self.a6table)
        # function pointers taken with lea/pea x(pc): accept those that start
        # with a function prologue (link / movem to the stack / stack check)
        while True:
            added = False
            for p in sorted(self.code_ptrs):
                if p in self.insns or p in self.errors or p & 1:
                    continue
                try:
                    i = decode(self.mod.data, self.base, p)
                except DecodeError:
                    continue
                if i.op == 'link' or (i.op == 'movem' and i.dst is not None and i.dst.kind == 'predec')                         or self.stack_check_prologue(p):
                    self.add(p, func=True)
                    added = True
            if not added:
                added = self.scan_gaps()
            if not added:
                break
            self.run()
        self.names = macsbug_names(mod, self.funcs)

    def in_code(self, a):
        return self.code_lo <= a < self.code_hi

    def add(self, a, func=False):
        if not self.in_code(a) or a & 1:
            return
        if func:
            self.funcs.add(a)
        self.labels.add(a)
        if a not in self.insns:
            self.work.append(a)

    def run(self):
        while self.work:
            a = self.work.pop()
            self.trace(a)

    def trace(self, a):
        areg = {}   # address registers known to hold lea x(pc) along this path
        while True:
            if a in self.insns or a in self.errors:
                return
            if not self.in_code(a):
                self.errors[a] = 'outside code'
                return
            try:
                i = decode(self.mod.data, self.base, a)
            except DecodeError as e:
                self.errors[a] = str(e)
                return
            self.insns[a] = i
            nxt = a + i.length
            op = i.op
            # code pointers via lea/pea pc-relative
            for ea in (i.src, i.dst):
                if ea is not None and ea.kind == 'pcdisp' and op in ('lea', 'pea'):
                    if self.in_code(ea.addr):
                        self.code_ptrs.add(ea.addr)
            track_areg(areg, i)
            if op in ('bra',):
                self.add(i.target)
                return
            if op == 'b' or op == 'db':
                self.add(i.target)
                self.labels.add(nxt)
                a = nxt
                continue
            if op == 'bsr':
                self.add(i.target, func=True)
                self.labels.add(nxt)
                a = nxt
                continue
            if op in ('jsr', 'jmp'):
                ea = i.src
                if ea.kind == 'pcdisp':
                    self.add(ea.addr, func=(op == 'jsr'))
                elif ea.kind == 'disp' and ea.reg == 5 and ea.disp in self.a5table:
                    ea.addr = self.a5table[ea.disp]
                    self.add(ea.addr, func=True)
                elif ea.kind == 'disp' and ea.reg == 6 and ea.disp in self.a6table:
                    ea.addr = self.a6table[ea.disp]
                    self.add(ea.addr, func=True)
                elif ea.kind == 'pcindex':
                    t = self.pcrel_const(i)
                    if t is not None:
                        self.pcrel_calls[a] = t
                        self.add(t, func=(op == 'jsr'))
                    else:
                        self.jumptable(i)
                elif ea.kind == 'ind' and ea.reg in areg:
                    # lea f(pc),aN ; jsr (aN): the call stays dynamic, this
                    # only makes sure f gets recompiled
                    self.add(areg[ea.reg], func=(op == 'jsr'))
                    self.indirect.add(a)
                else:
                    self.indirect.add(a)
                if op == 'jsr':
                    areg.pop(0, None)
                    areg.pop(1, None)
                if op == 'jmp':
                    return
                self.labels.add(nxt)
                a = nxt
                continue
            if op in ('rts', 'rte', 'rtr', 'illegal', 'stop', 'reset'):
                return
            if op == 'os9':
                self.labels.add(nxt)
                # a blocking call returns to the run loop between fields and
                # executes its trap again, so its own address is an entry point
                self.labels.add(a)
                if i.extra == 0x09:
                    # F$Icpt: the intercept routine is usually loaded with lea x(pc),a0
                    for back in range(2, 24, 2):
                        p = self.insns.get(a - back)
                        if p is not None and p.op == 'lea' and p.dst.reg == 0 and p.src.kind == 'pcdisp':
                            self.add(p.src.addr, func=True)
                            break
                if i.extra in NORETURN_OS9:
                    return
            if op == 'trap':
                self.labels.add(nxt)
            if op in ('linea', 'linef'):
                return
            a = nxt

    def scan_gaps(self):
        """Functions nothing references directly (library code, or code only
        reached through pointers computed at run time): accept a gap address
        that starts with link a5/a6, or that follows a return/jump and starts
        with movem to the stack or the stack check."""
        d = self.mod.data
        covered = set()
        for a, i in self.insns.items():
            covered.update(range(a, a + i.length))
        ends = {a + i.length for a, i in self.insns.items() if i.op in ('rts', 'rte', 'bra', 'jmp')}
        added = False
        a = self.code_lo
        while a < self.code_hi - 4:
            if a in covered or a in self.errors:
                a += 2
                continue
            o = a - self.base
            after_end = a in ends or d[o - 2:o] in (bytes([0x4e, 0x75]), bytes([0x4e, 0x73]))
            link = d[o] == 0x4e and d[o + 1] in (0x55, 0x56)
            movem = d[o:o + 2] == bytes([0x48, 0xe7])
            if link or self.stack_check_prologue(a) or (after_end and movem):
                try:
                    decode(d, self.base, a)
                except DecodeError:
                    a += 2
                    continue
                self.add(a, func=True)
                self.run()
                for b, i in self.insns.items():
                    if b >= a and b not in covered:
                        covered.update(range(b, b + i.length))
                        if i.op in ('rts', 'rte', 'bra', 'jmp'):
                            ends.add(b + i.length)
                added = True
            a += 2
        return added

    def stack_check_prologue(self, p):
        """move.l d0,-(sp) ; move.l #N,d0 ; jsr stub(a6) / bsr.w  (compiler stack check)"""
        d = self.mod.data
        o = p - self.base
        if d[o:o + 4] != bytes([0x2f, 0x00, 0x20, 0x3c]):
            return False
        if d[o + 8:o + 10] == bytes([0x61, 0x00]):
            return True
        return d[o + 8:o + 10] == bytes([0x4e, 0xae]) and struct.unpack('>h', d[o + 10:o + 12])[0] in self.a6table

    def pcrel_const(self, i):
        """Microware large-model call: move.l #imm,dN ; jsr d8(pc,dN.l)"""
        ea = i.src
        if not ea.xlong:
            return None
        for back in (6, 2):
            p = self.insns.get(i.addr - back)
            if p is None or p.addr + p.length != i.addr:
                continue
            if p.src is None or p.src.kind != 'imm' or p.size != 'l':
                continue
            if ea.xreg < 8 and p.op in ('move', 'moveq') and p.dst.kind == 'dn' and p.dst.reg == ea.xreg:
                return (ea.addr + p.src.value) & 0xffffffff
            if ea.xreg >= 8 and p.op == 'movea' and p.dst.kind == 'an' and p.dst.reg == ea.xreg - 8:
                return (ea.addr + p.src.value) & 0xffffffff
        return None

    def jumptable(self, i):
        """Compiler switch idiom:
              cmpi.x #N,d0 ; b(hi|gt) default ; ... add d0,d0
              move.w T(pc,d0),d0 ; jmp T-2(pc,d0.w)
           Table T holds N+1 word offsets relative to the jmp's ea base."""
        prev = self.insns.get(i.addr - 4)
        if prev is None or prev.op != 'move' or prev.src.kind != 'pcindex':
            self.indirect.add(i.addr)
            return
        tbl = prev.src.addr
        base = i.src.addr
        # find the bound
        n = None
        a = prev.addr
        for _ in range(8):
            cands = [x for x in (a - 2, a - 4, a - 6, a - 8) if x in self.insns and
                     self.insns[x].addr + self.insns[x].length == a]
            if not cands:
                break
            a = cands[0]
            p = self.insns[a]
            if p.op == 'cmpi':
                n = p.src.value
                if p.size == 'b' and n & 0x80: n = None
                break
            if p.op == 'cmp' and p.src.kind == 'dn':
                # moveq #N,dK ; cmp.l dK,dX
                b = p.addr
                for _ in range(6):
                    qs = [self.insns[x] for x in (b - 2, b - 4, b - 6, b - 8)
                          if x in self.insns and self.insns[x].addr + self.insns[x].length == b]
                    if not qs:
                        break
                    q = qs[0]
                    b = q.addr
                    if q.op not in ('cmp', 'cmpi', 'tst', 'btst', 'b') and q.dst is not None \
                            and q.dst.kind == 'dn' and q.dst.reg == p.src.reg:
                        if q.op in ('moveq', 'move') and q.src.kind == 'imm':
                            n = q.src.value & 0xffffffff
                        break
                break
        if n is None or n > 512:
            self.indirect.add(i.addr)
            return
        targets = []
        for k in range(n + 1):
            off = self.mod.w(tbl - self.base + 2 * k)
            if off & 0x8000: off -= 0x10000
            t = base + off
            targets.append(t)
            self.add(t)
        self.jumptables[i.addr] = targets
        self.tables_data = getattr(self, 'tables_data', [])
        self.tables_data.append((tbl, 2 * (n + 1)))


def track_areg(areg, i):
    """forget address registers an instruction may change; record lea x(pc),aN"""
    op = i.op
    if op == 'lea' and i.src.kind == 'pcdisp':
        areg[i.dst.reg] = i.src.addr
        return
    for ea in (i.src, i.dst):
        if ea is not None and ea.kind in ('postinc', 'predec'):
            areg.pop(ea.reg, None)
    if op == 'movem' and i.src is not None:
        for k in range(8, 16):
            if i.extra & (1 << k):
                areg.pop(k - 8, None)
    if op in ('exg', 'link', 'unlk') or op == 'os9':
        for e in (i.src, i.dst):
            if e is not None and e.kind == 'an':
                areg.pop(e.reg, None)
    if i.dst is not None and i.dst.kind == 'an':
        areg.pop(i.dst.reg, None)


def a5_jumptable(mod):
    """MPW-style jump table: idata = [tblsize, a5off, entries...]; entries are
    (dc.w seg, jmp abs.l) copied to a5+0x20.  Returns {a5 displacement: target}."""
    doff, b = mod.idata()
    tsize, a5off = struct.unpack('>II', b[0:8])
    tbl = {}
    for k in range((tsize - 0x20) // 8):
        e = b[8 + k * 8: 16 + k * 8]
        if len(e) < 8 or e[2:4] != bytes([0x4e, 0xf9]):
            continue
        tbl[0x20 + k * 8 + 2] = mod.base + struct.unpack('>I', e[4:8])[0]
    return tbl


def a6_jumptable(mod):
    """'jmp abs.l' stubs in idata whose target is relocated by a code iref.
    Static storage is addressed with a6 = data + 0x8000, so the stub at data
    offset X is called as jsr (X - 0x8000)(a6).  Returns {a6 disp: target}."""
    try:
        doff, b = mod.idata()
        code_refs, _ = mod.irefs()
    except Exception:
        return {}
    tbl = {}
    for r in code_refs:
        k = r - 2 - doff
        if 0 <= k and k + 6 <= len(b) and b[k:k + 2] == bytes([0x4e, 0xf9]):
            disp = r - 2 - 0x8000
            if -0x8000 <= disp < 0x8000:
                tbl[disp] = mod.base + struct.unpack('>I', b[k + 2:k + 6])[0]
    return tbl


def idata_code_ptrs(mod, a6table):
    """code addresses stored in initialized data (other than the a6 stubs)"""
    try:
        doff, b = mod.idata()
        code_refs, _ = mod.irefs()
    except Exception:
        return set()
    stubs = {d + 0x8000 + 2 for d in a6table}
    out = set()
    for r in code_refs:
        k = r - doff
        if r in stubs or not (0 <= k and k + 4 <= len(b)):
            continue
        out.add(mod.base + struct.unpack('>I', b[k:k + 4])[0])
    return out


def uses_a5_table(mod):
    """Heuristic: the initialized data is an MPW jump table (every entry a
    'dc.w seg; jmp abs.l' stub) as produced by Apple MPW / compatible
    compilers.  Microware C programs do not have one."""
    try:
        doff, b = mod.idata()
        tsize = struct.unpack('>I', b[0:4])[0]
    except Exception:
        return False
    n = (tsize - 0x20) // 8
    if n < 3 or 8 + n * 8 > len(b):
        return False
    return len(a5_jumptable(mod)) == n


def macsbug_names(mod, funcs):
    """Functions end with rts/jmp followed by a MacsBug symbol: byte 0x80+len, name."""
    import bisect
    d = mod.data
    fl = sorted(funcs)
    names = {}
    for o in range(0x48, mod.idataoff - 2):
        if d[o] & 0x80 and 0x82 <= d[o] <= 0x9f and o >= 2 and d[o-2:o] in (bytes([0x4e, 0x75]), bytes([0x4e, 0xd0]), bytes([0x4e, 0x73])):
            n = d[o] & 0x7f
            nm = d[o + 1:o + 1 + n]
            if all(32 < c < 127 for c in nm):
                a = mod.base + o
                k = bisect.bisect_right(fl, a) - 1
                if k >= 0 and fl[k] not in names:
                    names[fl[k]] = nm.decode()
    return names


def load(path=None, base=None):
    """load a module; defaults to the main program from game.json"""
    if path is None or base is None:
        import config
        m = config.load()['modules'][0]
        path = path or 'extracted/' + config.module_file(m)
        base = base if base is not None else m['base']
    data = open(path, 'rb').read()
    return Module(data, base)


def analyze_module(m):
    """Analysis for a game.json module entry"""
    import config
    mod = load('extracted/' + config.module_file(m), m['base'])
    return Analysis(mod, extra_entries=m['extra_entries'], a5=m['a5_table'])


def listing(an, out):
    mod = an.mod
    addrs = sorted(an.insns)
    prev_end = None
    for a in addrs:
        i = an.insns[a]
        if prev_end is not None and a != prev_end:
            out.write('\n; ---- gap %x-%x\n' % (prev_end, a) if a > prev_end else '; ---- overlap\n')
        if a in an.funcs:
            out.write('\n;================ func_%06x %s\n' % (a, an.names.get(a, '')))
        lab = 'L%06x:' % a if a in an.labels else ''
        raw = mod.data[a - an.base:a - an.base + i.length].hex()
        out.write('%-9s %06x  %-20s %s\n' % (lab, a, raw, i))
        prev_end = a + i.length


if __name__ == '__main__':
    # python tools/analyze.py [module index]  -> build/listing_<file>.asm
    import os
    import config
    cfg = config.load()
    idx = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    m = cfg['modules'][idx]
    an = analyze_module(m)
    os.makedirs('build', exist_ok=True)
    out = 'build/listing_%s.asm' % config.module_file(m)
    with open(out, 'w') as f:
        listing(an, f)
    print('listing written to', out, '(A5 jump table: %s)' % an.uses_a5)
    print('insns', len(an.insns), 'funcs', len(an.funcs), 'errors', len(an.errors), 'indirect', len(an.indirect))
    for a, e in sorted(an.errors.items())[:40]:
        print('err %x %s' % (a, e))
    print('jumptables', len(an.jumptables))
    extra = sorted(p for p in an.code_ptrs if p not in an.insns)
    print('lea/pea code ptrs not decoded:', ' '.join('%x' % x for x in extra))
    print('indirect:', ' '.join('%x' % x for x in sorted(an.indirect)))
    covered = sum(i.length for i in an.insns.values())
    print('code bytes covered %d of %d' % (covered, an.code_hi - an.code_lo))
