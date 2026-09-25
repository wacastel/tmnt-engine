#!/usr/bin/env python3
"""Import the user's exact Teenage Mutant Ninja Turtles directory or ZIP into ignored Assets."""
from pathlib import Path
import argparse,hashlib,json,os,tempfile,zipfile,zlib
ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/'Verification/rom-identity.json'

def load_manifest():
 data=json.loads(MANIFEST.read_text());files=data['files']
 if len(files)!=16 or len({f['name'] for f in files})!=16:raise ValueError('Invalid media identity inventory')
 if sum(f['size'] for f in files)!=data['totalBytes']:raise ValueError('Invalid total media size')
 for f in files:
  if Path(f['name']).name!=f['name']:raise ValueError('Unsafe media filename')
 if (ROOT/'Sources/Native/tmnt_media_identity.h').read_text()!=identity_header(data):raise ValueError('Native media identities differ from manifest')
 return data

def identity_header(data):
 text='/* Generated from Verification/rom-identity.json; contains identities only. */\n#ifndef TMNT_MEDIA_IDENTITY_H\n#define TMNT_MEDIA_IDENTITY_H\n#include <stddef.h>\n#include <stdint.h>\n'
 text+='typedef struct { const char *name; uint64_t size; const char *sha256; } TMNTMediaIdentity;\nstatic const TMNTMediaIdentity TMNT_MEDIA_FILES[] = {\n'
 for f in data['files']:text+='    {"'+f['name']+'", '+str(f['size'])+', "'+f['sha256']+'"},\n'
 return text+'};\nstatic const size_t TMNT_MEDIA_FILE_COUNT = sizeof(TMNT_MEDIA_FILES) / sizeof(TMNT_MEDIA_FILES[0]);\n#endif\n'

def validate(data,identity):
 name=identity['name']
 if len(data)!=identity['size']:raise ValueError(f'{name}: expected {identity["size"]} bytes, found {len(data)}')
 for algorithm,key in ((hashlib.sha256,'sha256'),(hashlib.sha1,'sha1')):
  if algorithm(data).hexdigest()!=identity[key]:raise ValueError(f'{name}: {key.upper()} mismatch')
 if f'{zlib.crc32(data):08x}'!=identity['crc32']:raise ValueError(f'{name}: CRC32 mismatch')
 return data

def read_source(source,manifest):
 source=Path(source).expanduser().resolve();result={}
 if source.is_dir():
  for f in manifest['files']:result[f['name']]=validate((source/f['name']).read_bytes(),f)
 elif source.is_file() and zipfile.is_zipfile(source):
  with zipfile.ZipFile(source) as archive:
   names=archive.namelist()
   for f in manifest['files']:
    if names.count(f['name'])!=1:raise ValueError(f'{f["name"]}: missing or duplicate ZIP entry')
    result[f['name']]=validate(archive.read(f['name']),f)
 else:raise ValueError('Source must be an Teenage Mutant Ninja Turtles ROM directory or ZIP')
 return result

def write_verified(files,destination):
 destination=Path(destination).expanduser().resolve();destination.mkdir(parents=True,exist_ok=True)
 # Verify every input before replacing any destination file. Each replacement
 # is atomic; malformed input cannot leave a partially imported accepted set.
 for name,data in files.items():
  with tempfile.NamedTemporaryFile(prefix='.rom-import-',dir=destination,delete=False) as temporary:
   temporary.write(data);temporary_path=Path(temporary.name)
  try:os.replace(temporary_path,destination/name)
  finally:temporary_path.unlink(missing_ok=True)
 return destination

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--destination',type=Path,default=ROOT/'Assets');a=p.parse_args()
 try:
  manifest=load_manifest();files=read_source(a.source,manifest);destination=write_verified(files,a.destination)
  read_source(destination,manifest)
 except (OSError,ValueError,zipfile.BadZipFile) as error:raise SystemExit('ROM import failed: '+str(error))
 print(f'Imported and verified {len(files)} original ROM files ({manifest["totalBytes"]} bytes) into {destination}')
if __name__=='__main__':main()
