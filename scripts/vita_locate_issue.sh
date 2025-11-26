#!/usr/bin/env bash
set -euo pipefail

ROOTDIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOTDIR"

DUMP_SCRIPT="./dump_psp2core.sh"
# Prefer the unstripped ELF produced by the build (contains DWARF).
VELF_UNPACKED="${ROOTDIR}/cmake-build-psv/moonlight_vita"
VELF_PKG="${ROOTDIR}/cmake-build-psv/moonlight.velf"
if [ -f "$VELF_UNPACKED" ]; then
    SYMFILE="$VELF_UNPACKED"
elif [ -f "$VELF_PKG" ]; then
    SYMFILE="$VELF_PKG"
else
    SYMFILE=""
fi

VELF_PKG="${ROOTDIR}/cmake-build-psv/moonlight.velf"
OUTDIR="dump-analysis"
# Ensure single output dir (overwrite)
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"

echo "[vita_locate_issue] Output dir: $OUTDIR"

command -v arm-vita-eabi-nm >/dev/null 2>&1 || { echo "arm-vita-eabi-nm missing in PATH"; exit 1; }
command -v arm-vita-eabi-objdump >/dev/null 2>&1 || { echo "arm-vita-eabi-objdump missing in PATH"; exit 1; }
command -v arm-vita-eabi-addr2line >/dev/null 2>&1 || { echo "arm-vita-eabi-addr2line missing in PATH"; exit 1; }

echo "Running dump script: $DUMP_SCRIPT"
echo
# Run the existing dump script and capture output (overwrite previous)
"$DUMP_SCRIPT" 2>&1 | tee "$OUTDIR/dump_raw.txt" || true

# Find most recent psp2 dump file if present. New dump_psp2core.sh may preserve the
# remote filename (psp2core-*.psp2dmp) when KEEP_PSP2DMP=1 or use the temporary
# name psp2core_tmp.psp2dmp otherwise. Accept both.
LATEST_DUMP=$(ls -1t psp2core-*.psp2dmp 2>/dev/null | head -n1 || true)
if [ -z "$LATEST_DUMP" ] && [ -f "psp2core_tmp.psp2dmp" ]; then
    LATEST_DUMP="psp2core_tmp.psp2dmp"
fi

if [ -n "$LATEST_DUMP" ]; then
    echo "Latest psp2 dump: $LATEST_DUMP" | tee -a "$OUTDIR/dump_raw.txt"
else
    echo "No psp2 dump file found in cwd." | tee -a "$OUTDIR/dump_raw.txt"
fi

if [ -z "$SYMFILE" ]; then
    echo "No unstripped ELF found; will fall back to packaged VELF if present." | tee -a "$OUTDIR/dump_raw.txt"
    SYMFILE=""
else
    echo "Using unstripped ELF for symbols: $SYMFILE" | tee -a "$OUTDIR/dump_raw.txt"
fi

# Which velf/elf file to use for addr2line/objdump attempts. Prefer unstripped ELF (SYMFILE) then packaged VELF.
VELF="${SYMFILE:-$VELF_PKG}"
if [ -n "$VELF" ]; then
    echo "Symbols target (addr2line/objdump): $VELF" | tee -a "$OUTDIR/dump_raw.txt"
fi

echo "Analyzing dump output to locate PCs and LRs..." | tee -a "$OUTDIR/report.txt"

# Parse addresses from dump into addresses.txt using Python
python3 - "$OUTDIR/dump_raw.txt" "$OUTDIR/addresses.txt" <<'PY'
import sys,re
infile=sys.argv[1]
outfile=sys.argv[2]
data=open(infile,'r',errors='ignore').read().splitlines()
records=[]
cur_thread='unknown'
thread_block=False
pc_re=re.compile(r"PC:\s*(0x[0-9a-fA-F]+)(?:.*\+\s*(0x[0-9a-fA-F]+)\s*=>)?")
lr_re=re.compile(r"LR:\s*(0x[0-9a-fA-F]+)(?:.*\+\s*(0x[0-9a-fA-F]+)\s*=>)?")
sp_re=re.compile(r"SP:\s*(0x[0-9a-fA-F]+)")
for line in data:
    if '=== THREADS ===' in line:
        thread_block=True
        continue
    if thread_block:
        s=line.strip()
        # new thread name lines are indented but not starting with ID/Stop/Status/PC/LR/SP
        if s and not any(s.startswith(x) for x in ('ID:','Stop reason:','Status:','PC:','LR:','SP:')):
            cur_thread=s
        m=pc_re.search(line)
        if m:
            records.append((cur_thread,'PC',m.group(1),m.group(2) or ''))
        m=lr_re.search(line)
        if m:
            records.append((cur_thread,'LR',m.group(1),m.group(2) or ''))
        m=sp_re.search(line)
        if m:
            # record SP values so analysis can compute SP+offset when pointer occurrences are found
            records.append((cur_thread,'SP',m.group(1),''))
open(outfile,'w').write('\n'.join('\t'.join(x) for x in records))
print('Parsed',len(records),'address records')
PY

if [ ! -s "$OUTDIR/addresses.txt" ]; then
    echo "No PC/LR addresses parsed from dump. See $OUTDIR/dump_raw.txt" | tee -a "$OUTDIR/report.txt"
    exit 0
fi

echo "Using velf: $VELF" | tee -a "$OUTDIR/report.txt"

# Precompute nm list sorted
arm-vita-eabi-nm --defined-only "${SYMFILE:-$VELF_PKG}" | sed -n '1,200000p' > "$OUTDIR/nm_all.txt" || true
# Also export a reliable symbol list from readelf (FUNC symbols with VMA)
arm-vita-eabi-readelf -sW "$VELF" | awk '$4=="FUNC" && $2!="0" {printf("0x%s %s\n", $2, $NF)}' > "$OUTDIR/readelf_func_symbols.txt" || true
# Dump full objdump (disassembly + source if available) so Python can parse calls reliably
# This step can be slow (~minutes, large file). To skip it set SKIP_OBJDUMP=1 in the environment.
if [ "${SKIP_OBJDUMP:-0}" = "1" ]; then
    echo "SKIP_OBJDUMP=1 set; skipping heavy objdump step" | tee -a "$OUTDIR/report.txt"
else
    echo "Generating objdump (may take several minutes)..." | tee -a "$OUTDIR/report.txt"
    arm-vita-eabi-objdump -d -S "$VELF" > "$OUTDIR/objdump_all.txt" 2>/dev/null || true
fi
# Determine module base VMA from ELF (first PT_LOAD) so we can map offsets
VELF_PTH=$(readelf -l "$VELF" 2>/dev/null | awk '/Program Headers/{p=1;next} p{print}' ) || true
ELF_MODULE_BASE=0x0
if readelf -l "$VELF" >/dev/null 2>&1; then
    # prefer the p_vaddr of the first PT_LOAD (program header) as module base
    # pick the first large hex (>=8 hex digits) which corresponds to the virtual address
    ELF_MODULE_BASE_HEX=$(readelf -l "$VELF" | awk '/Program Headers/{p=1; next} p && /LOAD/ {for(i=1;i<=NF;i++) if ($i ~ /^0x[0-9a-fA-F]{8,}$/) {print $i; exit}}' | sed -n '1p') || true
    if [ -n "$ELF_MODULE_BASE_HEX" ]; then
        if [[ "$ELF_MODULE_BASE_HEX" =~ 0x[0-9a-fA-F]+ ]]; then
            ELF_MODULE_BASE=$ELF_MODULE_BASE_HEX
        else
            ELF_MODULE_BASE=0x$(printf '%x' "$ELF_MODULE_BASE_HEX")
        fi
    fi
fi
echo "ELF module base VMA: $ELF_MODULE_BASE" | tee -a "$OUTDIR/report.txt"

 # Get .text VMA and size
TEXT_VMA=0x0
TEXT_SIZE=0x0
if readelf -S "$VELF" >/dev/null 2>&1; then
    # find the .text line and extract the first two 8+ hex fields (Addr and Size)
    txt_line=$(readelf -S "$VELF" | sed -n '1,200p' | grep '\.text' | head -n1 || true)
    if [ -n "$txt_line" ]; then
        # extract 2 hex groups (Addr and Size) - support variable spacing
        addr_hex=$(echo "$txt_line" | sed -nE 's/.* ([0-9a-fA-F]{8,}) .*/\1/p') || true
        size_hex=$(echo "$txt_line" | sed -nE 's/.* [0-9a-fA-F]{8,} ([0-9a-fA-F]+) .*/\1/p') || true
        if [ -n "$addr_hex" ]; then
            TEXT_VMA=0x$addr_hex
        fi
        if [ -n "$size_hex" ]; then
            TEXT_SIZE=0x$size_hex
        fi
    fi
fi
echo "Text section VMA: $TEXT_VMA  Size: $TEXT_SIZE" | tee -a "$OUTDIR/report.txt"

# Analyze addresses: nearest symbols and disassembly
python3 - "$SYMFILE" "$ELF_MODULE_BASE" "$TEXT_VMA" "$TEXT_SIZE" "$OUTDIR/nm_all.txt" "$OUTDIR/addresses.txt" "$OUTDIR" "$LATEST_DUMP" "$OUTDIR/objdump_all.txt" <<'PY'
import sys, subprocess, re, os
import struct

def try_cmd(cmd, timeout=8):
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, timeout=timeout)
        return out.decode('utf-8', errors='ignore')
    except subprocess.CalledProcessError as e:
        return e.output.decode('utf-8', errors='ignore') if e.output else ''
    except Exception:
        return ''

VELF = sys.argv[1] if sys.argv[1] else None
ELF_MODULE_BASE = int(sys.argv[2], 0)
TEXT_VMA = int(sys.argv[3], 0)
TEXT_SIZE = int(sys.argv[4], 0)
nmfile = sys.argv[5]
addfile = sys.argv[6]
OUTDIR = sys.argv[7]
LATEST_DUMP = sys.argv[8] if len(sys.argv) > 8 else ''
OBJDUMP = sys.argv[9] if len(sys.argv) > 9 else ''
REPORT = os.path.join(OUTDIR, 'report.txt')

def parse_nm():
    arr = []
    try:
        for l in open(nmfile, 'r', errors='ignore'):
            parts = l.strip().split()
            if not parts: continue
            try:
                addr = int(parts[0], 16)
            except Exception:
                continue
            name = ' '.join(parts[2:]) if len(parts) >= 3 else (parts[1] if len(parts) >= 2 else '')
            arr.append((addr, name))
    except Exception:
        pass
    arr.sort()
    return arr

nm = parse_nm()

# read SP values per thread from addresses file
sp_by_thread = {}
try:
    for l in open(open(sys.argv[6],'r').name,'r',errors='ignore'):
        pass
except Exception:
    pass

# We'll populate sp_by_thread while iterating the address file below instead (keeps thread context)

# readelf-based function symbols (VMA addresses)
re_syms = []
try:
    for l in open(nmfile.replace('nm_all.txt', 'readelf_func_symbols.txt'), 'r', errors='ignore'):
        p = l.strip().split(None, 1)
        if len(p) != 2: continue
        addr = int(p[0], 16)
        name = p[1]
        re_syms.append((addr, name))
except Exception:
    re_syms = []
re_syms.sort()

def nearest_text(addr):
    src = []
    if re_syms:
        src = [(a, n) for (a, n) in re_syms if a >= TEXT_VMA and a < TEXT_VMA + TEXT_SIZE]
    else:
        src = [(a, n) for (a, n) in nm if a >= TEXT_VMA and a < TEXT_VMA + TEXT_SIZE]
    src.sort()
    below = [s for s in src if s[0] <= addr]
    above = [s for s in src if s[0] > addr]
    return (below[-5:] if below else []), (above[:5] if above else [])

def fmt_hex(x):
    return hex(x) if isinstance(x, int) else str(x)

raw = ''
try:
    raw = open(os.path.join(OUTDIR, 'dump_raw.txt'), 'r', errors='ignore').read()
    # strip common ANSI color escape sequences so regexes match reliably
    raw = re.sub(r'\x1b\[[0-9;]*m', '', raw)
except Exception:
    raw = ''

# load objdump text if available
objdump_text = ''
if OBJDUMP and os.path.exists(OBJDUMP):
    try:
        objdump_text = open(OBJDUMP, 'r', errors='ignore').read()
    except Exception:
        objdump_text = ''

# extract module mappings like: 0x81aca0ab (moonlight_vita@1 + 0xa8f0ab
module_bases = {}
for m in re.finditer(r'(0x[0-9a-fA-F]+) \(([^@\s]+)@(\d+) \+ (0x[0-9a-fA-F]+)', raw):
    addr_s, modname, modid, off_s = m.group(1), m.group(2), m.group(3), m.group(4)
    try:
        addr_i = int(addr_s, 16)
        off_i = int(off_s, 16)
        base = addr_i - off_i
        key = f"{modname}@{modid}"
        if key not in module_bases:
            module_bases[key] = base
    except Exception:
        pass

# Parse 'STACK CONTENTS AROUND SP' blocks to index stack values -> addresses
stack_value_map = {}  # value(int) -> list of addresses where it appears
for blk in re.finditer(r'STACK CONTENTS AROUND SP:\n((?:[ \t]*0x[0-9a-fA-F]+:\s*0x[0-9a-fA-F]+\n)+)', raw):
    body = blk.group(1)
    for line in body.splitlines():
        m = re.search(r'([0-9a-fA-Fx]+):\s*(0x[0-9a-fA-F]+)', line)
        if m:
            try:
                addr = int(m.group(1), 16)
                val = int(m.group(2), 16)
                stack_value_map.setdefault(val, []).append(addr)
            except Exception:
                pass

with open(REPORT, 'a') as rep:
    rep.write('\nDetailed address analysis:\n')
    if module_bases:
        rep.write('\nDetected module mappings from dump:\n')
        for k, v in module_bases.items():
            rep.write(f'  {k} -> {fmt_hex(v)}\n')

    for line in open(addfile, 'r'):
        fields = line.strip().split('\t')
        if len(fields) < 3:
            continue
        thread, kind, addr = fields[0], fields[1], fields[2]
        # skip spurious lines that come from dump text (e.g. disassembly headers like 'DISASSEMBLY' or 'SP:')
        if ':' in thread or thread.upper().startswith('DISASSEMBLY'):
            continue
        # ensure addr looks like a hex address
        if not re.match(r'^0x[0-9a-fA-F]+$', addr):
            continue
        off = fields[3] if len(fields) > 3 else ''
        addr_val = int(addr, 16)
        off_val = int(off, 16) if off else None

        # capture SP values per thread for SP-relative heuristics
        if kind == 'SP':
            sp_by_thread[thread] = addr_val
            # also report it
            with open(REPORT, 'a') as rep:
                rep.write(f'\nCaptured SP for thread {thread}: {fmt_hex(addr_val)}\n')
            continue

        # determine module assignment
        assigned = None
        if off_val:
            # prefer reported module if any in raw
            for m in re.finditer(re.escape(hex(addr_val)) + r'.*?\(([^@\s]+)@(\d+) \+ (0x[0-9a-fA-F]+)', raw, re.DOTALL):
                modname = m.group(1); modid = m.group(2)
                assigned = (f"{modname}@{modid}", module_bases.get(f"{modname}@{modid}"))
                break
        if not assigned and module_bases:
            # heuristic: find module whose base makes offset small
            for k, base in module_bases.items():
                off_c = addr_val - base
                if 0 <= off_c < 0x2000000:
                    assigned = (k, base)
                    break

        rep.write('\n' + '='*60 + '\n')
        rep.write(f'Thread: {thread}  Kind: {kind}\n')
        rep.write(f'  RuntimeAddr: {fmt_hex(addr_val)}\n')

        if assigned:
            modname, base = assigned
            rep.write(f'  Assigned module: {modname} base={fmt_hex(base)}\n')
            offset = addr_val - base
            rep.write(f'  Module offset: {fmt_hex(offset)}\n')
            if 'moonlight_vita' in modname:
                if VELF:
                    # map to local ELF using ELF_MODULE_BASE + offset
                    velf_addr = ELF_MODULE_BASE + offset
                    rep.write(f'  Mapping into local ELF velf_addr={fmt_hex(velf_addr)}\n')
                    # try addr2line with addr, addr-1, addr+1
                    success=False
                    for a in (velf_addr, velf_addr-1, velf_addr+1):
                        out = try_cmd(['arm-vita-eabi-addr2line','-f','-C','-e', VELF, hex(a)], timeout=6)
                        rep.write(f'  addr2line({fmt_hex(a)}):\n')
                        rep.write(out + '\n')
                        if out and '??' not in out:
                            success=True
                            break
                    if not success:
                        # fallback: objdump around address (±0x80)
                        start = max(velf_addr-0x80, 0)
                        stop = velf_addr+0x80
                        rep.write('  addr2line failed to resolve source; objdump -D -S around address:\n')
                        out = try_cmd(['arm-vita-eabi-objdump','-D','-S','--start-address='+hex(start),'--stop-address='+hex(stop), VELF], timeout=8)
                        rep.write(out + '\n')
                else:
                    rep.write('  moonlight_vita module assigned but no local ELF available to resolve DWARF.\n')
            else:
                # no local ELF available — try nm_all for nearest symbol
                rep.write('  No local ELF for this module. Nearest global symbols:\n')
                # search nm file for nearest lower symbol
                nearest = None
                try:
                    for l in open(nmfile,'r',errors='ignore'):
                        parts = l.strip().split()
                        if not parts: continue
                        try:
                            a = int(parts[0],16)
                        except Exception:
                            continue
                        name = ' '.join(parts[2:]) if len(parts)>=3 else (parts[1] if len(parts)>=2 else '')
                        if a <= addr_val:
                            nearest = (a,name)
                        else:
                            break
                except Exception:
                    nearest = None
                if nearest:
                    rep.write(f'    nearest <= addr: {fmt_hex(nearest[0])} {nearest[1]}\n')
        else:
            rep.write('  No module assigned by dump heuristics.\n')
            # try to detect if address is inside known main ELF section
            if TEXT_VMA and TEXT_SIZE and (TEXT_VMA <= addr_val < TEXT_VMA + TEXT_SIZE):
                rep.write('  Address lies within main ELF .text. Attempt addr2line (with ±1 fallback):\n')
                success=False
                for a in (addr_val, addr_val-1, addr_val+1):
                    out = try_cmd(['arm-vita-eabi-addr2line','-f','-C','-e', VELF, hex(a)], timeout=6) if VELF else ''
                    rep.write(f'    addr2line({fmt_hex(a)}):\n')
                    rep.write(out + '\n')
                    if out and '??' not in out:
                        success=True
                        break
                if not success and VELF:
                    start = max(addr_val-0x80, 0)
                    stop = addr_val+0x80
                    rep.write('    addr2line failed; objdump -D -S around address:\n')
                    out = try_cmd(['arm-vita-eabi-objdump','-D','-S','--start-address='+hex(start),'--stop-address='+hex(stop), VELF], timeout=8)
                    rep.write(out + '\n')
            else:
                rep.write('  Address not in main ELF .text. Check stack/heap for this value.\n')
                # find occurrences in raw and show surrounding lines
                if raw:
                    for m in re.finditer(re.escape(fmt_hex(addr_val)), raw):
                        start = max(0, m.start()-120)
                        end = min(len(raw), m.end()+120)
                        rep.write('\n  Context around occurrence in dump_raw:\n')
                        rep.write(raw[start:end] + '\n')
        # stack scan: look for this value in stack contents and show the line
        if raw:
            locs = [m.start() for m in re.finditer(re.escape(fmt_hex(addr_val)), raw)]
            if locs:
                rep.write('\n  Found occurrences in raw dump (first 5):\n')
                for idx,pos in enumerate(locs[:5]):
                    start = max(0, pos-200)
                    end = min(len(raw), pos+200)
                    snippet = raw[start:end]
                    rep.write(snippet + '\n---\n')
                    # try to compute offset relative to a nearby address label or SP
                    # search backwards from pos for a memory-line label like '0x81abcd00:' or '81abcd00:'
                    label_match = None
                    for m in re.finditer(r'(0x[0-9a-fA-F]{8,}|[0-9a-fA-F]{8,})[:\s]', snippet[:120]):
                        label_match = m.group(1)
                    computed = None
                    if label_match:
                        try:
                            base = int(label_match, 16)
                            offset = addr_val - base
                            rep.write(f'  Computed offset relative to nearby dump label {hex(base)}: {hex(offset)}\n')
                            computed = ('label', base, offset)
                        except Exception:
                            pass
                    # if we have SP for this thread, compute SP-relative offset as well
                    if thread in sp_by_thread:
                        spv = sp_by_thread[thread]
                        off_sp = addr_val - spv
                        rep.write(f'  Offset relative to SP ({fmt_hex(spv)}): {fmt_hex(off_sp)}\n')
                        computed = ('sp', spv, off_sp)
        rep.write('\n')

    rep.write('\nEnd of detailed analysis.\n')
    # Heuristic: detect indirect-call targets in nearby disassembly that point outside ELF
    rep.write('\nSummary heuristics and probable diagnosis:\n')
    # search for blx/bl/bx targets in the disassembly sections we emitted earlier
    # we will look for patterns like 'blx\s+(0x[0-9a-fA-F]+)' in the full objdump output contained in dump_raw
    if raw:
        rep.write('\nSearching dump raw for indirect-call targets (blx/bl/bx)...\n')
        call_targets = set()
        # collect from both the raw dump and local objdump
        for m in re.finditer(r'\bblx?\s+((?:0x)?[0-9a-fA-F]+)', raw):
            try:
                call_targets.add(int(m.group(1),0))
            except Exception:
                pass
        for m in re.finditer(r'\bbx\s+((?:0x)?[0-9a-fA-F]+)', raw):
            try:
                call_targets.add(int(m.group(1),0))
            except Exception:
                pass
        # also inspect objdump text directly for calls
        if objdump_text:
            for m in re.finditer(r'\bblx?\s+((?:0x)?[0-9a-fA-F]+)', objdump_text):
                try:
                    call_targets.add(int(m.group(1),0))
                except Exception:
                    pass
            for m in re.finditer(r'\bbx\s+((?:0x)?[0-9a-fA-F]+)', objdump_text):
                try:
                    call_targets.add(int(m.group(1),0))
                except Exception:
                    pass
        if call_targets:
            rep.write(f'  Found {len(call_targets)} indirect-call targets in dump raw.\n')
            for t in sorted(call_targets):
                # determine whether target lies inside main ELF .text or in detected modules
                in_text = (TEXT_VMA and TEXT_SIZE and (TEXT_VMA <= t < TEXT_VMA + TEXT_SIZE))
                assigned_mod = None
                for k, base in module_bases.items():
                    if base is None: continue
                    off_c = t - base
                    if 0 <= off_c < 0x2000000:
                        assigned_mod = (k, base, off_c)
                        break
                rep.write(f'    target {fmt_hex(t)} -> in_text={in_text} assigned_module={assigned_mod}\n')
                # if target not in .text, search its occurrences in raw dump (stack etc.) and try to compute SP offsets
                if not in_text:
                    # match textual hex forms both with 0x and without (objdump may omit '0x')
                    forms_pattern = re.escape(fmt_hex(t)) + '|' + re.escape(fmt_hex(t)[2:])
                    occs = [m.start() for m in re.finditer(forms_pattern, raw)]
                    if occs:
                        rep.write(f'      Occurrences in dump_raw (first 5):\n')
                        for pos in occs[:5]:
                            start = max(0, pos-200)
                            end = min(len(raw), pos+200)
                            snippet = raw[start:end]
                            rep.write(snippet + '\n---\n')
                            # attempt to find nearest address label in snippet and compute offsets
                            label=None
                            for m in re.finditer(r'(0x[0-9a-fA-F]{8,}|[0-9a-fA-F]{8,})[:\s]', snippet):
                                label = m.group(1)
                            if label:
                                try:
                                    base=int(label,16)
                                    rep.write(f'        nearby dump label: {hex(base)} -> offset {hex(t-base)}\n')
                                except Exception:
                                    pass
                            # if any SP recorded, compute SP-relative offsets for all known threads
                            if sp_by_thread:
                                for thr, spv in sp_by_thread.items():
                                    rep.write(f'        offset relative to SP of thread {thr} ({fmt_hex(spv)}): {fmt_hex(t-spv)}\n')
                    else:
                        rep.write('      No occurrences of this pointer found in dump_raw.\n')
                        # try binary little-endian search in the raw .psp2dmp file if available
                        if LATEST_DUMP and os.path.exists(LATEST_DUMP):
                            try:
                                with open(LATEST_DUMP, 'rb') as f:
                                    bdata = f.read()
                                le = struct.pack('<I', t)
                                occs_bin = []
                                pos = bdata.find(le)
                                while pos != -1:
                                    occs_bin.append(pos)
                                    pos = bdata.find(le, pos+1)
                                if occs_bin:
                                    rep.write(f'      Found {len(occs_bin)} little-endian occurrences of {fmt_hex(t)} in dump file {LATEST_DUMP}:\n')
                                    for off in occs_bin[:10]:
                                        start = max(0, off-32)
                                        end = min(len(bdata), off+32)
                                        snippet = bdata[start:end]
                                        rep.write(f'        file offset 0x{off:x}: {snippet.hex()}\n')
                                else:
                                    rep.write(f'      No little-endian occurrences of {fmt_hex(t)} in dump file {LATEST_DUMP}.\n')
                            except Exception as e:
                                rep.write(f'      Binary dump search failed: {e}\n')
                        # check parsed stack contents map for this target value
                        if t in stack_value_map:
                            rep.write(f'      Found target {fmt_hex(t)} in parsed STACK CONTENTS at addresses:\n')
                            for a in stack_value_map[t]:
                                rep.write(f'        stack addr {hex(a)}')
                                if sp_by_thread:
                                    for thr, spv in sp_by_thread.items():
                                        rep.write(f'  (SP of {thr} {fmt_hex(spv)} -> offset {fmt_hex(a-spv)})')
                                rep.write('\n')
        else:
            rep.write('  No indirect-call instructions found in dump raw.\n')

    # Additionally: attempt to correlate indirect-call targets with the instruction that performed the call and any ldr [sp,#N]
    rep.write('\nAttempting to correlate indirect-call targets with nearby "ldr [sp,#N]" instructions in local objdump (if available)...\n')
    if VELF:
        for m in re.finditer(r'\bblx?\s+((?:0x)?[0-9a-fA-F]+)', raw):
            try:
                t=int(m.group(1),0)
            except Exception:
                continue
            # For each target, try to find the call site in the local ELF by searching objdump around velf mappings
            # We'll scan readelf_func_symbols to find candidates near call sites (best effort)
            try:
                # find candidate functions in readelf symbols close to any .text symbol
                cand_funcs = [addr for (addr,name) in re_syms]
            except Exception:
                cand_funcs = []
            # perform objdump search in a wider window to catch ldr instructions
            # we will try a few windows across the text if ELF_MODULE_BASE is set
            if ELF_MODULE_BASE:
                try:
                    # dump a moderate region of .text for searching
                    start = TEXT_VMA
                    stop = TEXT_VMA + TEXT_SIZE
                    out = try_cmd(['arm-vita-eabi-objdump','-d','-S','--start-address='+hex(start),'--stop-address='+hex(stop), VELF], timeout=12)
                except Exception:
                    out = ''
                if out:
                    # look for lines that call our target
                    lines = out.splitlines()
                    for idx, line in enumerate(lines):
                        cm = re.search(r'^\s*([0-9a-fA-F]+):\s+[0-9a-fA-F]+\s+\S+\s+(?:blx|bl)\s+((?:0x)?[0-9a-fA-F]+)', line)
                        if not cm:
                            continue
                        try:
                            call_addr = int(cm.group(1),16)
                            call_target = int(cm.group(2),0)
                        except Exception:
                            continue
                        if call_target != t:
                            continue
                        rep.write(f'  Found call instruction at {hex(call_addr)} calling target {hex(t)}\n')
                        # attempt to map this VELF call_addr to a runtime address using detected module_bases
                        runtime_candidates = []
                        for modkey, base in module_bases.items():
                            if base is None: continue
                            # only consider moonlight_vita modules for mapping into our VELF
                            if not modkey.startswith('moonlight_vita'):
                                continue
                            # runtime = module_base + (call_addr - ELF_MODULE_BASE)
                            try:
                                runtime_addr = base + (call_addr - ELF_MODULE_BASE)
                                runtime_candidates.append((modkey, base, runtime_addr))
                            except Exception:
                                continue
                        for modkey, base, runtime_addr in runtime_candidates:
                            rep.write(f'    Mapped call site to runtime address {hex(runtime_addr)} in module {modkey} (module base {hex(base)})\n')
                            # show any nearby context in the textual dump for that runtime address
                            if raw:
                                pat = re.escape(hex(runtime_addr))
                                for m2 in re.finditer(pat, raw):
                                    s=max(0, m2.start()-120); e=min(len(raw), m2.end()+120)
                                    rep.write('      Context around mapped call-site occurrence in dump_raw:\n')
                                    rep.write(raw[s:e] + '\n')
                        # search backwards for ldr from sp within previous 40 lines to find origin pointer loads
                        lookback = max(0, idx-40)
                        for l in lines[lookback:idx]:
                            lm = re.search(r'^\s*([0-9a-fA-F]+):\s+[0-9a-fA-F]+\s+\S+\s+ldr[^,]*,\s*\[sp(?:,|\s)#?(-?0x[0-9a-fA-F]+|-?\d+)\]', l)
                            if not lm:
                                continue
                            try:
                                ldr_addr = int(lm.group(1),16)
                            except Exception:
                                continue
                            off_str = lm.group(2)
                            try:
                                off_val = int(off_str,0)
                            except Exception:
                                try:
                                    off_val = int(off_str.replace('0x',''),16)
                                except Exception:
                                    continue
                            rep.write(f'    Candidate ldr from sp at {hex(ldr_addr)} offset {hex(off_val)}\n')
                            # if we have any SP recorded, compute exact memory address and search stack map
                            if sp_by_thread:
                                for thr, spv in sp_by_thread.items():
                                    origin_addr = spv + off_val
                                    rep.write(f'      This would read memory at {hex(origin_addr)} (SP of {thr} {fmt_hex(spv)} + {hex(off_val)})\n')
                                    # check if that origin_addr exists in parsed STACK CONTENTS
                                    if origin_addr in stack_value_map:
                                        rep.write(f'      Found value at this stack address in parsed STACK CONTENTS; occurrences: {stack_value_map[origin_addr]}\n')
                                    # also check if the loaded value (call target) exists in stack map
                                    if t in stack_value_map:
                                        rep.write(f'      The call target {fmt_hex(t)} appears in STACK CONTENTS at: {stack_value_map[t]}\n')
                                    # map the ldr instruction to source
                                    out_line = try_cmd(['arm-vita-eabi-addr2line','-f','-C','-e', VELF, hex(ldr_addr)], timeout=6)
                                    rep.write(f'      addr2line(ldr instruction {hex(ldr_addr)}):\n{out_line}\n')
                            else:
                                rep.write('      No SP value captured; cannot compute exact memory address for this ldr.\n')
                        # once we've reported for this call, break from scanning calls for this target
                        break

    # Final human-friendly diagnosis
    rep.write('\nFinal probable diagnosis:\n')
    # If any call target was outside .text and present on stack: strong evidence of corrupted function pointer
    corrupted_evidence=False
    try:
        for m in re.finditer(r'\bblx?\s+((?:0x)?[0-9a-fA-F]+)', raw):
            try:
                t=int(m.group(1),0)
            except Exception:
                continue
            if not (TEXT_VMA and TEXT_SIZE and (TEXT_VMA <= t < TEXT_VMA + TEXT_SIZE)):
                # check if value appears in dump (stack) — try textual both with/without 0x
                hex_forms = [hex(t), hex(t)[2:]]
                found_text = any(re.search(re.escape(hf), raw) for hf in hex_forms)
                if not found_text and LATEST_DUMP and os.path.exists(LATEST_DUMP):
                    # binary LE fallback
                    try:
                        with open(LATEST_DUMP, 'rb') as f:
                            bdata = f.read()
                        if bdata.find(struct.pack('<I', t)) != -1:
                            found_text = True
                    except Exception:
                        pass
                if found_text:
                    corrupted_evidence=True
                    break
    except Exception:
        corrupted_evidence=False

    if corrupted_evidence:
        rep.write('  Probable cause: indirect call target points to address outside main ELF (.text) and that value appears in the process memory (stack/heap) in the dump. This strongly suggests a corrupted function pointer or vtable entry leading to a call into non-executable/data memory (prefetch abort).\n')
    else:
        rep.write('  Probable cause: could not find definitive evidence of a corrupted function pointer in the dump. LR maps to a std::time_get function; PC lies outside main ELF .text. Investigation should focus on call sites that compute function pointers and on nearby heap/stack contents.\n')
PY

echo
echo "Report saved to $OUTDIR/report.txt"
echo "Raw dump saved to $OUTDIR/dump_raw.txt"

exit 0
