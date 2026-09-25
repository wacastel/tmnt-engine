#!/usr/bin/env python3
"""Build isolated fixed-native or original-interpreter reference libraries."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,os,subprocess,tarfile,time
ROOT=Path(__file__).resolve().parents[1]
def run(args):
 r=subprocess.run([str(x) for x in args],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 if r.returncode:raise RuntimeError(str(args[-3:])+'\n'+r.stdout[-14000:])
 return r.stdout
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--reference',action='store_true');p.add_argument('--clean',action='store_true');p.add_argument('--jobs',type=int,default=6);a=p.parse_args()
 mode='reference' if a.reference else 'native';out=ROOT/'build'/mode;out.mkdir(parents=True,exist_ok=True)
 ref=ROOT/'build/reference-source'
 if not (ref/'src/cpu/m68k/m68kcpu.c').exists():
  ref.mkdir(parents=True,exist_ok=True)
  with tarfile.open(ROOT/'Tools/ReferenceLab/fbneo-22e2aeb-source.tar.gz') as t:t.extractall(ref,filter='data')
 if not a.reference:
  run(['python3',ROOT/'scripts/compile_m68k.py'])
  run(['python3',ROOT/'scripts/compile_z80.py'])
 h=ROOT/'Sources/Hardware';cpu=ref/'src/cpu' if a.reference else ROOT/'Sources/CPU';native=ROOT/'Sources/Native'
 # CPU adapters are unchanged upstream; reference includes only original CPU headers.
 common=['burn','burn_memory','burn_sound','burn_sound_c','load','tiles_generic','tilemap_generic','burn_gun','burn_shift','burn_pal','burn_bitmap','timer','cheat','debug_track']
 files=[h/'burn'/f'{n}.cpp' for n in common]
 files += sorted((h/'burn/drv/konami').glob('k*.cpp'))
 files += [h/'burn/devices'/f'{n}.cpp' for n in ['joyprocess','eeprom']]
 files += [h/'burn/snd/burn_ym2151.cpp',h/'burn/snd/ym2151.c',h/'burn/snd/upd7759.cpp',h/'burn/snd/k007232.cpp',h/'burn/snd/k053260.cpp']
 files += [cpu/'m68000_intf.cpp',cpu/'z80_intf.cpp',native/'tmnt_bridge.cpp',native/'tmnt_media_validate.cpp',native/'tmnt_platform.cpp']
 if a.reference:
  generated=out/'generated';generated.mkdir(exist_ok=True)
  if not (generated/'m68kops.c').exists():
   run(['clang',cpu/'m68k/m68kmake.c','-O2','-o',out/'m68kmake'])
   run([out/'m68kmake',generated,cpu/'m68k/m68k_in.c'])
  files += [cpu/'m68k/m68kcpu.c',generated/'m68kops.c',cpu/'z80/z80.cpp',cpu/'z80/z80daisy.cpp',cpu/'z80/z80ctc.cpp',cpu/'z80/z80pio.cpp']
 else:
  generated=native;files += [native/'generated_m68k.c',*sorted(native.glob('generated_m68k_ops_*.c')),cpu/'z80/z80.cpp']
 dirs=[native,generated,cpu,cpu/'m68k',cpu/'z80',cpu/'i8039',cpu/'mcs51',h/'burn',h/'burn/snd',h/'burn/devices',h/'burn/drv/konami',h/'burner/macos']
 dirs += sorted({f.parent for f in h.rglob('*.h')} | {f.parent for f in cpu.rglob('*.h')})
 flags=['-arch','arm64','-mmacosx-version-min=14.0','-O2','-fwrapv','-fno-strict-aliasing','-fno-common','-Wno-writable-strings','-Wno-ignored-attributes','-Wno-shift-negative-value','-Wno-deprecated-declarations','-Wno-format','-include','wchar.h','-DEMU_M68K','-DEMU_Z80','-DLSB_FIRST','-DINLINE=static inline']
 if a.reference:flags+=['-DTMNT_REFERENCE=1']
 flags += [f'-I{d}' for d in dirs]
 headers=[h/'burn/drv/konami/d_tmnt.cpp',*h.rglob('*.h'),*cpu.rglob('*.h'),*cpu.rglob('*.inc'),*native.glob('*.h'),*native.glob('*.inc')]
 fingerprint=hashlib.sha256((' '.join(flags)+''.join(sha(f) for f in sorted(headers))).encode()).hexdigest()
 def compile_one(f):
  obj=out/(str(f.relative_to(ROOT)).replace('/','_')+'.o');stamp=obj.with_suffix('.stamp')
  key=hashlib.sha256((sha(f)+fingerprint).encode()).hexdigest()
  if a.clean or not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
   fflags=list(flags)
   if not a.reference and ('generated_m68k' in f.name or f.name=='z80.cpp'):fflags+=['-O0']
   # All upstream CPU C files are compiled as C++ by FBNeo; retain linkage/config.
   compiler=['clang','-std=gnu11'] if f.suffix=='.c' else ['clang++','-std=c++11']
   run([*compiler,*fflags,'-c',f,'-o',obj]);stamp.write_text(key)
  return obj
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:objects=list(pool.map(compile_one,files))
 stem='libtmnt_reference' if a.reference else 'libtmnt'
 run(['libtool','-static','-o',out/(stem+'.a'),*objects])
 exports=out/'exports.txt';exports.write_text('_tmnt_*\n')
 run(['clang++','-arch','arm64','-mmacosx-version-min=14.0','-dynamiclib','-Wl,-dead_strip','-Wl,-exported_symbols_list,'+str(exports),'-o',out/(stem+'.dylib'),*objects,'-lz'])
 manifest={'mode':mode,'upstream':'22e2aebcccf888ba9a041bf6023f3381a4fc86dd','sources':{str(f.relative_to(ROOT)):sha(f) for f in files},'headers':{str(f.relative_to(ROOT)):sha(f) for f in sorted(set(headers))},'flags':flags,'librarySHA256':sha(out/(stem+'.dylib')),'archiveSHA256':sha(out/(stem+'.a'))}
 (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(f'{mode}: built {len(files)} translation units; '+manifest['librarySHA256'])
if __name__=='__main__':main()
