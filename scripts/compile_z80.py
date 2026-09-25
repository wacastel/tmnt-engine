#!/usr/bin/env python3
"""Translate pinned Teenage Mutant Ninja Turtles Z80 operations offline into fixed-PC C++.

The pinned FBNeo core supplies arithmetic, memory, interrupt and cycle semantics.
Its opcode interpreter, opcode tables and Spectrum contention decoder are removed.
"""
from pathlib import Path
import argparse, hashlib, json, re, tarfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA = '6fba9a88000e4a2cc5cc28e44f375e59e07bf22f5e340dc3e65406fc9b248088'
ROM_SHA = '523733bc14494f806ad3bb32479db6e853883dc9dd4517aef6dc4c2792d36a61'
DRIVER_SHA = 'fee3c97e4f607eda1c2193ca20f67fbd376b08639e12cef6b579d50f412ff832'
COMMIT = '22e2aebcccf888ba9a041bf6023f3381a4fc86dd'
LIMIT = 0x8000 # Actual pinned FBNeo TMNT ROM fetch boundary.

def sha(data): return hashlib.sha256(data).hexdigest()
def end_body(s, start):
    depth = 0
    for i in range(start, len(s)):
        if s[i] == '{': depth += 1
        elif s[i] == '}':
            depth -= 1
            if depth == 0: return i + 1
    raise ValueError('unterminated body')
def replace_function(s, name, body):
    m = re.search(r'(?m)^[^\n;{}]*\b' + name + r'\([^;{}]*\)\s*\{', s)
    if not m: raise ValueError('missing function ' + name)
    a = s.index('{', m.start())
    return s[:a] + '{\n' + body + '\n}' + s[end_body(s,a):]
def uncomment(s):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', s, flags=re.S)
def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text: path.write_text(text)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--rom', type=Path, default=ROOT/'Assets/963e20.g13')
    args=p.parse_args()
    archive=ROOT/'Tools/ReferenceLab/fbneo-22e2aeb-source.tar.gz'
    with tarfile.open(archive) as tar:
        drivers = [x for x in tar.getmembers() if x.name.endswith('src/burn/drv/konami/d_tmnt.cpp')]
        if len(drivers) != 1 or sha(tar.extractfile(drivers[0]).read()) != DRIVER_SHA:
            raise ValueError('Unpinned TMNT board mapping')
        def original(name):
            entries=[x for x in tar.getmembers() if x.name.endswith('src/cpu/z80/'+name)]
            if len(entries)!=1: raise ValueError('archive source ambiguous '+name)
            return tar.extractfile(entries[0]).read()
        raw=original('z80.cpp')
        headers={n:original(n) for n in ['z80.h','z80daisy.h','z80ctc.h','z80pio.h']}
    if sha(raw)!=SOURCE_SHA: raise ValueError('unpinned Z80 source')
    rom=args.rom.read_bytes()
    if len(rom)!=0x8000 or sha(rom)!=ROM_SHA: raise ValueError('unpinned sound ROM')
    s=raw.decode()
    ops={}
    for m in re.finditer(r'(?m)^OP\((op|cb|dd|ed|fd|xycb),([0-9a-f]{2})\)\s*\{',s):
        a=s.index('{',m.start()); ops[m[1],int(m[2],16)]=s[a+1:end_body(s,a)-1]
    if len(ops)!=1536: raise ValueError('incomplete semantic source')
    ops['op',0xc0]=ops['op',0xc0].replace('RET_COND_SPECTRUM','RET_COND')
    first=s.index('OP(cb,00)')
    head=s[:first]
    # Preserve the complete original license above. Every transformation below is
    # specific to the fixed, noncontended TMNT sound machine.
    note='''\n/* Modified for Teenage Mutant Ninja Turtles: generated fixed-PC native execution.\n * scripts/compile_z80.py removes all runtime instruction decoding and the\n * unused Spectrum contention interpreter. Original semantics and notices kept. */\nextern "C" void tmnt_native_fault(const char *, unsigned, unsigned);\nextern "C" int tmnt_native_failed(void);\nstatic int tmnt_z80_bad();\nstatic int tmnt_z80_unsupported() { tmnt_native_fault("Z80 configuration", 0, 0); return 0; }\n'''
    head=head.replace('#include <stddef.h>','#include <stddef.h>'+note)
    a=head.index('// SpecZ80'); b=head.index('static int Z80lastop;',a)
    head=head[:a]+'''// TMNT has no Spectrum bus contention. Keep diagnostic t-states.\nstatic void eat_cycles(int, int);\nstatic const int m_ula_variant = ULA_VARIANT_NONE;\nstatic const int m_cycles_per_line = 0;\nstatic int m_tstate_counter;\nstatic int m_selected_bank;\nstatic UINT8 store_rwinfo(UINT16, UINT8, UINT16, const char *) { return 0; }\nstatic UINT8 run_script() { return 0; }\n'''+head[b:]
    # Cycle tables remain literal-index timing data, never instruction dispatch.
    a=head.index('static const UINT8 cc_op_msx'); b=head.index('static const UINT8 *cc[6];',a)
    head=head[:a]+head[b:]
    a=head.index('enum ContendedMemoryID'); b=head.index('#define ENTER_HALT',a)
    head=head[:a]+'''#define CC(prefix,opcode) do { eat_cycles(CYCLES_EXEC, cc[Z80_TABLE_##prefix][opcode]); } while (0)\n\n'''+head[b:]
    head=replace_function(head,'ROP','')
    head=re.sub(r'Z80_INLINE UINT8 ROP\(void\)\s*\{\s*\}', '', head)
    head=replace_function(head,'RET_COND_SPECTRUM','')
    head=re.sub(r'static inline void RET_COND_SPECTRUM\(int cond, UINT8 opcode\)\s*\{\s*\}', '', head)
    head=re.sub(r'm_opcode_history\.do_optional\s*=\s*true;', '',head)
    # No peripheral daisy chain is connected on this board. Reject attempts to
    # activate one, while retaining the public context's original ABI.
    head=head.replace('z80daisy_call_reti_device(Z80.daisy)', 'tmnt_z80_unsupported()')
    # IRQ/NMI, lifecycle and context API are inherited from the pinned reference.
    tail=s[s.index('static void take_interrupt(void)'):s.index('/**************************************************************************\n * Contended Memory Functions')]
    a=tail.index('\t\t/* Interrupt mode 0.'); b=tail.index('\n\tWZ=PCD;',a)
    tail=tail[:a]+'''        // TMNT leaves the data bus vector at FF. It is a fixed RST38,
        // not an instruction supplied to a decoder. Reject other vectors.
        if (irq_vector != 0xff) { tmnt_z80_bad(); return; }
        PUSH(pc); PCD = 0x0038;
        eat_cycles(CYCLES_ISR, cc[Z80_TABLE_op][0xff] + cc[Z80_TABLE_ex][0xff]);
    }
'''+tail[b:]
    tail=replace_function(tail,'z80_set_cycle_tables_msx','  tmnt_z80_unsupported();')
    tail=replace_function(tail,'z80_set_cycle_tables','  if (op || cb || ed || xy || xycb || ex) tmnt_z80_unsupported();')
    tail=replace_function(tail,'Z80InitContention','  if (is_on_type || rastercallback) tmnt_z80_unsupported();\n  m_tstate_counter = 0; m_selected_bank = 0;')
    tail=replace_function(tail,'z80_set_spectrum_tape_callback','  if (tape_cb) tmnt_z80_unsupported();')
    tail=replace_function(tail,'Z80SetDaisy','  if (dptr) tmnt_z80_unsupported();')
    tail=re.sub(r'\s*memset\(&m_opcode_history, 0, sizeof\(OPCODE_HISTORY\)\);\s*m_opcode_history.capturing = false;', '',tail)
    for name in ['z80daisy_call_ack_device','z80daisy_reset','z80daisy_update_irq_state']:
        tail=tail.replace(name+'(Z80.daisy)', 'tmnt_z80_unsupported()')
    for name,args_ in [('z80daisy_exit',''),('z80daisy_scan','nAction'),('z80ctc_timer_update','cycles')]:
        tail=tail.replace(name+'('+args_+')', 'tmnt_z80_unsupported()')
    tail=tail.replace('Z80.daisy && z80daisy_has_ctc', 'Z80.daisy')
    a=tail.index('\t\tif (m_ula_variant == ULA_VARIANT_NONE)',tail.index('int Z80Execute'))
    b=tail.index('\n\t} while(',a)
    tail=tail[:a]+'''        if (tmnt_native_failed() || !tmnt_z80_step()) {
            Z80.end_run = 1;
            break;
        }'''+tail[b:]
    tail=tail.replace('while( Z80.ICount > 0 && !Z80.end_run )','while( Z80.ICount > 0 && !Z80.end_run && !tmnt_native_failed() )')
    tail+='''\nstatic void eat_cycles(int, int cycles) { Z80.ICount -= cycles; m_tstate_counter += cycles; }\n'''
    # Expand prefixes and aliases offline. Live ARG reads preserve immediate and
    # displacement data access; only operation/prefix bytes select semantics.
    omitted=[]
    def compile_at(pc):
        checks=[]
        def expand(group,num,cursor,depth=0):
            if depth>24: raise ValueError('prefix depth')
            body=ops[group,num].replace('illegal_1();','').replace('illegal_2();','')
            def alias(m): return '{ '+expand(m[1],int(m[2],16),cursor,depth+1)+' }'
            body=re.sub(r'\b(op|cb|dd|ed|fd|xycb)_([0-9a-f]{2})\(\)',alias,body)
            match=re.search(r'EXEC\((\w+),(ROP|ARG)\(\)\)',body)
            if match:
                c=cursor+(1 if match[1]=='xycb' else 0)
                if c>=LIMIT: raise ValueError('prefix crosses mapped ROM')
                value=rom[c]; checks.append((c,value,match[2]))
                inc='PC++; '+(f'Z80lastop=0x{value:02x}; ' if match[2]=='ROP' else '')
                nested=inc+f'CC({match[1]},0x{value:02x}); '+expand(match[1],value,c+1,depth+1)
                body=body[:match.start()]+'{ '+nested+' }'+body[match.end():]
            return body
        v=rom[pc];checks.append((pc,v,'ROP'));body=expand('op',v,pc+1)
        guard=' || '.join(f'{"cpu_readop_arg" if kind=="ARG" else "cpu_readop"}(0x{a:04x}) != 0x{v:02x}' for a,v,kind in checks)
        return f'case 0x{pc:04x}: {{ if ({guard}) return tmnt_z80_bad(); PC++; Z80lastop=0x{v:02x}; CC(op,0x{v:02x}); {body} return 1; }}\n'
    generated=['/* Generated offline; dispatch keys are fixed program addresses. */\n']
    entries=0
    for start in range(0,LIMIT,0x100):
        generated.append(f'static int tmnt_z80_page_{start:04x}() {{ switch (PCD) {{\n')
        for pc in range(start,start+0x100):
            try: generated.append(compile_at(pc)); entries+=1
            except ValueError as error: omitted.append({'pc':pc,'reason':str(error)})
        generated.append('default: return tmnt_z80_bad(); } }\n')
    generated.append('static int tmnt_z80_step() { switch (PCD >> 8) {\n')
    for start in range(0,LIMIT,0x100): generated.append(f'case 0x{start>>8:02x}: return tmnt_z80_page_{start:04x}();\n')
    generated.append('default: return tmnt_z80_bad(); } }\n')
    dispatch=''.join(generated)
    middle='''\nstatic int tmnt_z80_bad() { tmnt_native_fault("Z80", PCD, cpu_readop(PCD & 0xffff)); Z80.end_run = 1; return 0; }\n#include "generated_z80.inc"\n'''
    out=head+middle+tail
    for pattern in [r'\bROP\(',r'\bEXEC(?:_INLINE)?\(',r'\bPROTOTYPES\(',r'\bOP\(',r'\bswitch\s*\(\s*op\s*\)',r'\b(op|cb|dd|ed|fd|xycb)_[0-9a-f]{2}\(']:
        if re.search(pattern,uncomment(out+dispatch)): raise ValueError('generic decoder survived: '+pattern)
    for name,data in headers.items():
        dest=ROOT/'Sources/CPU/z80'/name
        if not dest.exists() or dest.read_bytes()!=data: dest.write_bytes(data)
    write(ROOT/'Sources/CPU/z80/z80.cpp',out)
    write(ROOT/'Sources/CPU/z80/generated_z80.inc',dispatch)
    report={
        'game':'Teenage Mutant Ninja Turtles (World 4 Players)', 'upstreamCommit':COMMIT,
        'driverSHA256':DRIVER_SHA,'sourceSHA256':SOURCE_SHA,'archiveSHA256':sha(archive.read_bytes()),
        'soundROM':{'filename':args.rom.name,'bytes':len(rom),'sha256':ROM_SHA},
        'mappedROMBytes':LIMIT,'fixedAddressEntries':entries,'excludedEntries':omitted,
        'generatedCoreSHA256':sha(out.encode()),'generatedDispatchSHA256':sha(dispatch.encode()),
        'compilerSHA256':sha(Path(__file__).read_bytes()),
        'opcodePolicy':'Fixed PC entry and complete operation/prefix-byte guards; no runtime decoder or fallback.',
        'operandPolicy':'Original Zet operand and memory callbacks; operands remain live data.',
        'interruptPolicy':'Original FBNeo IRQ/NMI timing, EI/RETN shadows and IM1/IM2 semantics; IM0 admits only original bus vectorFF fixedRST38.',
        'unsupportedConfigurations':['Spectrum contention','MSX/custom timing','daisy-chain peripherals','execution outside mapped fixed ROM (including fetch-mapped sound RAM)' ],
        'build':'Compile z80.cpp (includes generated_z80.inc), no daisy/PIO/CTC implementation required. Bounded256-entry page functions.',
        'validation':'Generation verifies pinned source and ROM identities, but does not measure executed coverage; see separate native/reference acceptance reports.'}
    write(ROOT/'Documentation/z80-translation.json',json.dumps(report,indent=2)+'\n')
    print(json.dumps({'fixedAddressEntries':entries,'excludedEntries':omitted,'generatedBytes':len(out)+len(dispatch)}))
if __name__=='__main__': main()
