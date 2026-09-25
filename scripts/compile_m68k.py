#!/usr/bin/env python3
"""Compile pinned Teenage Mutant Ninja Turtles program images to fixed native 68000 entries.

The upstream Musashi generator runs only offline. The shipped core selects
operations, registers, cycles and indexed-address metadata by fixed image/PC;
it retains FBNeo's original bus, prefetch, interrupt and context interfaces.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'm68kcpu.c': '6f617fc63aa3f154ad0ff8177e78a4e9b57e7897968133f6a6e9fa9f951d31de',
    'm68kcpu.h': 'f429111910321eab4c436226562ecd4ba9df0f02f12740153c4c9682faa0df50',
    'm68kconf.h': '754708be08daa58d3827b3c052bf98321fd03252dcd80fa6615be34139d4c315',
    'm68k.h': '1f492227bfbaf736ee771a71be0b8d82f3f06307aa1f2357582da33362fd7427',
    'm68kmake.c': '7d21f59b826e7cadca7e25780314f5a52eae117c2b43a6b4df69f533aa460da3',
    'm68k_in.c': 'd6a0656f45dfd28d26675e6c12c498701e1dda06d81af32e2b5c8837ee2281ed',
}
DRIVER_SHA = 'fee3c97e4f607eda1c2193ca20f67fbd376b08639e12cef6b579d50f412ff832'
ROMS = {
    '963-x23.j17': (0x20000, '21799f06b3db70be47161d2af3b6b9230a3c7c0d516e0990392c8a829e101a6a'),
    '963-x24.k17': (0x20000, 'f683a19bd7e8174d2e0fc423d9fc77ac27a3899fdf6c4b2f718b78ad268996e7'),
    '963-x21.j15': (0x10000, 'a852ff154d592360b8e699955a66509facad0d0e87f36dd6ba70901c81ad4090'),
    '963-x22.k15': (0x10000, '240d14abc4dc1235e38123cdd6c3e5d1a3c7d1e5671e31fd80949307e3debeaf'),
}
NOTICE = '''/* Generated offline by scripts/compile_m68k.py. Do not edit.
 * Teenage Mutant Ninja Turtles World 4-player fixed 68000 translation.
 * Derived from FBNeo's Musashi 3.32, Copyright Karl Stenerud.
 * Original notices retained in Sources/CPU/m68k and Licenses.
 * Modified for fixed native compilation in September 2026.
 */
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def function_replace(source, name, body):
    pattern = r'((?:INLINE|void|int)\s+\w*\s*' + name + r'\([^;]*?\)\n)\{\n.*?\n\}'
    source, count = re.subn(pattern, lambda m: m[1] + '{\n' + body + '\n}', source, flags=re.S)
    assert count == 1, (name, count)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, default=ROOT / 'build/reference-source/src')
    parser.add_argument('--rom-dir', type=Path, default=ROOT / 'Assets')
    args = parser.parse_args()
    assert sha((args.upstream / 'burn/drv/konami/d_tmnt.cpp').read_bytes()) == DRIVER_SHA, 'Unpinned TMNT board mapping'
    upstream = args.upstream / 'cpu/m68k'
    sources = {}
    for name, digest in PINS.items():
        data = (upstream / name).read_bytes()
        assert sha(data) == digest, 'Unpinned CPU source: ' + name
        sources[name] = data.decode()
    # Pinned d_tmnt.cpp maps a single 0x60000-byte 68000 image. Its ROM
    # loader uses byte-swapped host storage; these are canonical bus byte lanes.
    roms = {}
    for name, (size, digest) in ROMS.items():
        data = (args.rom_dir / name).read_bytes()
        assert len(data) == size and sha(data) == digest, 'Unpinned program ROM: ' + name
        roms[name] = data
    image = bytearray(0x60000)
    image[0:0x40000:2], image[1:0x40000:2] = roms['963-x23.j17'], roms['963-x24.k17']
    image[0x40000::2], image[0x40001::2] = roms['963-x21.j15'], roms['963-x22.k15']
    images = {'main': bytes(image)}

    scratch = ROOT / 'build/m68k-generator'
    scratch.mkdir(parents=True, exist_ok=True)
    subprocess.run(['clang', '-O2', str(upstream / 'm68kmake.c'), '-o', str(scratch / 'm68kmake')], check=True)
    subprocess.run([str(scratch / 'm68kmake'), str(scratch), str(upstream / 'm68k_in.c')], check=True)
    ops = (scratch / 'm68kops.c').read_text()
    bodies = dict(re.findall(r'static void (m68k_op_\w+)\(void\)\n\{\n(.*?)^\}', ops, re.S | re.M))
    assert len(bodies) == 1966
    # The generated upstream mask rows are ordered from broad to specific.
    # Expanding each variable bit offline is identical to its original builder.
    rows = re.findall(r'\{(m68k_op_\w+)\s*,\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+),\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}\}', ops)
    assert len(rows) == 1966
    handlers = [('m68k_op_illegal', 0)] * 65536
    for name, mask, match, cycles, *_ in rows:
        mask, match = int(mask, 16), int(match, 16)
        choices = [match]
        for bit in range(16):
            if not mask & (1 << bit):
                choices += [value | (1 << bit) for value in choices]
        for value in choices:
            handlers[value] = (name, int(cycles))

    header = sources['m68kcpu.h']
    operands = {}
    for match in re.finditer(r'INLINE uint (OPER_\w+)\(void\)\s*\{uint ea\s*=\s*(.*?);\s*return (.*?)\(ea\);\s*\}', header):
        operands[match[1]] = '({ uint tmnt_ea = ' + match[2] + '; ' + match[3] + '(tmnt_ea); })'
    assert len(operands) >= 40
    words = {name: [int.from_bytes(data[i:i + 2], 'big') for i in range(0, len(data), 2)]
             for name, data in images.items()}
    # The sole cheat gate is pinned to the selected game's normal player
    # vulnerability update. Keep source bytes out of the provenance report.
    protection_window_sha = '5225fd7898b9ece0b03bf9eb2fbc766db3ae9797c66a1e84c85f57f66581a882'
    assert sha(images['main'][0x1b69e:0x1b6e0]) == protection_window_sha
    assert words['main'][0x1b6d8 // 2] == 0x0010
    assert words['main'][0x1b6da // 2] == 0x0002
    variants, signatures, mapping = [], {}, {}
    for opcode in sorted(set(words['main'])):
        body = bodies[handlers[opcode][0]]
        for name, expression in operands.items():
            body = body.replace(name + '()', expression)
        body = re.sub(r'\bREG_IR\b', f'0x{opcode:04x}u', body)
        x, y = (opcode >> 9) & 7, opcode & 7
        for name, replacement in [('DX', f'REG_D[{x}]'), ('DY', f'REG_D[{y}]'),
                                  ('AX', f'REG_A[{x}]'), ('AY', f'REG_A[{y}]')]:
            body = re.sub(r'\b' + name + r'\b', replacement, body)
        signature = (body, x if 'EA_AX_' in body else 0, y if 'EA_AY_' in body else 0)
        if signature not in signatures:
            signatures[signature] = len(variants)
            variants.append(signature)
        mapping[opcode] = signatures[signature]

    # Keep the full context ABI expected by Sek, but remove every runtime
    # opcode/index decoder and the unused generic operand wrappers.
    header = re.sub(r'INLINE uint OPER_\w+\(void\)\s*\{uint ea\s*=.*?\}', '', header)
    header = re.sub(r'^#define (?:DX|DY|AX|AY) .*\n', '', header, flags=re.M)
    header = header.replace('CYC_INSTRUCTION[REG_IR]', 'tmnt_m68k_base_cycles')
    header = header.replace('#include "m68k.h"', '#include "m68k.h"\n#include "generated_m68k.h"')
    header = function_replace(header, 'm68ki_get_ea_ix', '    return tmnt_m68k_indexed_ea(An);')
    config = sources['m68kconf.h']
    config = re.sub(r'(#define M68K_EMULATE_(?:010|EC020)\s+)OPT_ON', r'\1OPT_OFF', config)
    cpu_dir = ROOT / 'Sources/CPU/m68k'
    write(cpu_dir / 'm68k.h', sources['m68k.h'])
    write(cpu_dir / 'm68kconf.h', NOTICE + config)
    write(cpu_dir / 'm68kcpu.h', NOTICE + header)
    out = ROOT / 'Sources/Native'
    write(out / 'generated_m68k.h', NOTICE + '''#pragma once
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned int tmnt_m68k_base_cycles;
unsigned int tmnt_m68k_indexed_ea(unsigned int);
int tmnt_native_cpu_index(void);
void tmnt_native_fault(const char *, unsigned int, unsigned int);
int tmnt_native_failed(void);
int tmnt_player_invincible_for(unsigned int);
#ifdef __cplusplus
}
#endif
''')
    chunk = 900
    wanted = {f'generated_m68k_ops_{i:02d}.c' for i in range((len(variants) + chunk - 1) // chunk)}
    for old in out.glob('generated_m68k_ops_*.c'):
        if old.name not in wanted:
            old.unlink()
    for start in range(0, len(variants), chunk):
        code = [NOTICE, '#include "m68kcpu.h"\nextern void m68040_fpu_op0(void);\nextern void m68040_fpu_op1(void);\n']
        for i in range(start, min(start + chunk, len(variants))):
            body, x, y = variants[i]
            code.append(f'#undef AX\n#undef AY\n#define AX REG_A[{x}]\n#define AY REG_A[{y}]\nvoid tmnt_m68k_code_{i}(void)\n{{\n{body}\n}}\n')
        write(out / f'generated_m68k_ops_{start // chunk:02d}.c', ''.join(code))

    table = [NOTICE]
    table += [f'extern void tmnt_m68k_code_{i}(void);\n' for i in range(len(variants))]
    table += ['extern void tmnt_m68k_player_protection(void);\n']
    table += ['typedef struct { void (*run)(void); unsigned short word, cycles; unsigned int index; } TMNTM68KEntry;\n']
    for name, values in words.items():
        table.append(f'static const TMNTM68KEntry tmnt_m68k_{name}[] = {{\n')
        for i in range(0, len(values), 4):
            entries = []
            for offset, word in enumerate(values[i:i + 4], i):
                index = ((word >> 12) << 24) | (0 if word & 0x800 else 0x10000) | (word & 255)
                operation = 'tmnt_m68k_player_protection' if name == 'main' and offset * 2 == 0x1b6d8 else f'tmnt_m68k_code_{mapping[word]}'
                entries.append(f'{{{operation},0x{word:04x},{handlers[word][1]},0x{index:08x}}}')
            table.append('  ' + ','.join(entries) + ',\n')
        table.append('};\n')
    write(out / 'generated_m68k_program.inc', ''.join(table))

    runtime = '''
#include <setjmp.h>
#include "generated_m68k_program.inc"
unsigned int tmnt_m68k_base_cycles;
static jmp_buf tmnt_m68k_fault_trap;
static int tmnt_m68k_fault_trap_active;
static void tmnt_m68k_fail(const char *reason, unsigned int pc, unsigned int detail)
{
    tmnt_native_fault(reason, pc, detail);
    m68ki_cpu.end_run = 1;
    if (tmnt_m68k_fault_trap_active) longjmp(tmnt_m68k_fault_trap, 1);
}
static const TMNTM68KEntry *tmnt_m68k_entry(unsigned int pc)
{
    int cpu = tmnt_native_cpu_index();
    unsigned int mapped = pc & 0xffffff;
    if (cpu == 0 && !(mapped & 1) && mapped < 0x60000)
        return &tmnt_m68k_main[mapped >> 1];
    tmnt_m68k_fail("Untranslated 68000 address", pc, (unsigned int)cpu);
    return NULL;
}
unsigned int tmnt_m68k_indexed_ea(unsigned int base)
{
    unsigned int pc = REG_PC;
    const TMNTM68KEntry *entry = tmnt_m68k_entry(pc);
    if (!entry) return 0;
    unsigned int actual = m68ki_read_imm_16();
    if (actual != entry->word) {
        tmnt_m68k_fail("Changed 68000 indexed extension", pc, actual);
        return 0;
    }
    unsigned int index = REG_DA[entry->index >> 24];
    if (entry->index & 0x10000) index = MAKE_INT_16(index);
    return base + index + MAKE_INT_8(entry->index);
}
'''
    # Preserve the original immediate fetch, read/write and ORI flag/cycle
    # semantics. Only the fixed player vulnerability-bit source is masked.
    protection_body = bodies['m68k_op_ori_8_ai']
    assert protection_body.count('uint src = OPER_I_8();') == 1
    protection_body = protection_body.replace('uint src = OPER_I_8();',
        'uint src = OPER_I_8();\n\tif (tmnt_player_invincible_for(REG_A[0])) src = 0;')
    protection_body = protection_body.replace('EA_AY_AI_8()', 'REG_A[0]')
    runtime += '\nvoid tmnt_m68k_player_protection(void)\n{\n' + protection_body + '\n}\n'
    core = sources['m68kcpu.c'].replace('#include "m68kops.h"\n', '').replace('#include "m68kfpu.c"\n', '')
    core = core.replace('static const char copyright_notice[] =', '__attribute__((used)) static const char copyright_notice[] =')
    core = re.sub(r'^\s*CYC_INSTRUCTION\s*= m68ki_cycles\[\d\];\n', '', core, flags=re.M)
    core = core.replace('\t\tmemset(&m68ki_cycles, 0, 0x10000 * 4);\n', '').replace('\t\tm68ki_build_opcode_table();\n', '')
    # Only the installed 68000 is accepted; preserve its original configuration.
    original_type = re.search(r'case M68K_CPU_TYPE_68000:(.*?)\n\t\tcase M68K_CPU_TYPE_68008:', core, re.S)[1]
    core = function_replace(core, 'm68k_set_cpu_type', '    if(cpu_type == M68K_CPU_TYPE_68000) {' + original_type + '\n    }\n    tmnt_m68k_fail("Unsupported CPU type", 0, cpu_type);')
    core = core.replace('int m68k_execute(int num_cycles)\n{', 'int m68k_execute(int num_cycles)\n{\n    if(tmnt_native_failed()) return num_cycles;\n    if(setjmp(tmnt_m68k_fault_trap)) { tmnt_m68k_fault_trap_active = 0; return m68ki_cpu.initial_cycles - GET_CYCLES(); }\n    tmnt_m68k_fault_trap_active = 1;')
    dispatch = '''const TMNTM68KEntry *entry = tmnt_m68k_entry(REG_PC);
            if (!entry) break;
            REG_IR = m68ki_read_imm_16();
            if (REG_IR != entry->word) tmnt_m68k_fail("Changed 68000 operation", REG_PPC, REG_IR);
            tmnt_m68k_base_cycles = entry->cycles;
            entry->run();
            USE_CYCLES(tmnt_m68k_base_cycles);'''
    old_dispatch = 'REG_IR = m68ki_read_imm_16();\n\t\t\tm68ki_instruction_jump_table[REG_IR]();\n\t\t\tUSE_CYCLES(CYC_INSTRUCTION[REG_IR]);'
    assert core.count(old_dispatch) == 2
    core = core.replace(old_dispatch, dispatch)
    # This game never uses the Mega Drive scheduler. Keep its API as an alias to
    # the same fixed CPU path rather than carrying a second unsupported loop.
    core = function_replace(core, 'm68k_executeMD', '    return m68k_execute(num_cycles);')
    core = core.replace('\t\treturn num_cycles;\n', '\t\ttmnt_m68k_fault_trap_active = 0;\n\t\treturn num_cycles;\n', 1)
    core = core.replace('\treturn m68ki_cpu.initial_cycles - GET_CYCLES();', '\ttmnt_m68k_fault_trap_active = 0;\n\treturn m68ki_cpu.initial_cycles - GET_CYCLES();', 1)
    # Definitions use m68ki_cpu, so insert after the original global core object.
    core = core.replace('m68ki_cpu_core m68ki_cpu = {0};', 'm68ki_cpu_core m68ki_cpu = {0};\n' + runtime)
    assert 'm68ki_instruction_jump_table' not in core and 'm68ki_cycles' not in core
    write(out / 'generated_m68k.c', NOTICE + core)
    report = {
        'game': 'Teenage Mutant Ninja Turtles (World 4 Players)',
        'upstreamCommit': '22e2aebcccf888ba9a041bf6023f3381a4fc86dd',
        'compilerSHA256': sha(Path(__file__).read_bytes()),
        'driverSHA256': DRIVER_SHA,
        'sourceSHA256': PINS, 'programROMs': {name: {'bytes': size, 'sha256': digest} for name, (size, digest) in ROMS.items()},
        'images': {name: {'bytes': len(data), 'sha256': sha(data), 'fixedEntries': len(words[name])}
                   for name, data in images.items()},
        'uniqueOperationWords': len(mapping), 'specializedFunctions': len(variants),
        'sourceRowsDecodedOffline': len(rows), 'interpreterFallback': False,
        'runtimeSelection': 'CPU identity0 and 24-bit-normalized fixed program address000000–05FFFF only; original PC remains untouched for operand reads and branches.',
        'memoryGuards': 'Every executed operation and indexed extension must match its fixed pinned image word. Unadmitted RAM code fails closed.',
        'timing': 'Original FBNeo Musashi 68000 cycles, prefetch, interrupts, callbacks, and Sek context ABI; no Genesis clock multipliers.',
        'ramTemplates': [],
        'playerInvincibility': {
            'cpu': 0, 'pc': '01B6D8', 'operation': 'ORI.B #2,(A0)',
            'windowStart': '01B69E', 'windowEndExclusive': '01B6E0',
            'windowSHA256': protection_window_sha,
            'selectedPlayerBase': '062000 + (player - 1) * 0x50',
            'vulnerableFlagBit': 1, 'originalProtectionTimerOffset': '2D',
            'behavior': 'Mask the original ORI immediate source only for the selected player while the native flag is enabled. Original immediate fetch, memory access, ORI result flags and 16-cycle timing remain. The preceding game instruction clears the vulnerability bit and the following instruction controls visibility; timers, health, lives and stage state are not overwritten.',
        },
        'validation': 'Generation verifies pinned source and ROM identities, but does not measure executed coverage; see separate native/reference acceptance reports.',
        'generatedSHA256': {str(p.relative_to(ROOT)): sha(p.read_bytes())
                            for p in sorted(list(out.glob('generated_m68k*')) + list(cpu_dir.glob('*')))},
    }
    write(ROOT / 'Documentation/m68k-translation.json', json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('uniqueOperationWords', 'specializedFunctions')}))


if __name__ == '__main__':
    main()
