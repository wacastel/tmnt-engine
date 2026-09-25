#!/usr/bin/env python3
"""Check the shipping title-sample decoder against an independent Yamaha oracle.

The actual production routine and the original pinned routine are compiled into
arm64 fixtures. Neither ROM bytes nor decoded audio are written to this report.
"""

import argparse
import collections
import hashlib
import json
import math
import platform
import re
import struct
import subprocess
import tarfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'Sources/Hardware/burn/drv/konami/d_tmnt.cpp'
ARCHIVE = ROOT / 'Tools/ReferenceLab/fbneo-22e2aeb-source.tar.gz'
UPSTREAM = '22e2aebcccf888ba9a041bf6023f3381a4fc86dd'
MAME_REVISION = '70642b82cb651a3e6a1b3a470b5637123fb63ab8'
MAME_BASE = 'https://github.com/mamedev/mame/blob/' + MAME_REVISION
FRAME_COUNT = 0x40000
FLAGS = ['-std=c++11', '-arch', 'arm64', '-mmacosx-version-min=14.0',
         '-O2', '-fwrapv', '-fno-strict-aliasing', '-fno-common']

HARNESS = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using UINT8 = uint8_t;
using INT16 = int16_t;
using INT32 = int32_t;
static UINT8 *DrvTempRom;
static INT16 *DrvTitleSample;
__DECODER__
int main() {
    DrvTempRom = static_cast<UINT8 *>(std::malloc(0x80000));
    DrvTitleSample = static_cast<INT16 *>(std::malloc(0x80000));
    if (!DrvTempRom || !DrvTitleSample) return 2;
    if (std::fread(DrvTempRom, 1, 0x80000, stdin) != 0x80000) return 3;
    if (std::fgetc(stdin) != EOF) return 4;
    TmntDecodeTitleSample();
    if (std::fwrite(DrvTitleSample, 2, 0x40000, stdout) != 0x40000) return 5;
    std::free(DrvTitleSample);
    std::free(DrvTempRom);
    return 0;
}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract_decoder(source):
    start = source.index('static void TmntDecodeTitleSample()')
    opening = source.index('{', start)
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == '{':
            depth += 1
        elif source[offset] == '}':
            depth -= 1
            if depth == 0:
                return source[start:offset + 1]
    raise AssertionError('Unterminated production title decoder')


def oracle(packed):
    """Yamaha sign/exponent inversion, then signed narrowing and right shift.

    Independently expressed from the representation documented by Aaron Giles
    in ymfm.h (BSD-3-Clause; pinned source URL in the acceptance report). The
    additional floor division retains FBNeo's existing quarter-scale amplitude.
    This uses the bit-domain method, not the production mantissa/scale formula.
    """
    inverted = (packed >> 3) ^ 0x1e00
    narrowed = (inverted * 64) & 0xffff
    signed = narrowed if narrowed < 0x8000 else narrowed - 0x10000
    return (signed >> ((inverted >> 10) & 7)) // 4


def expected_pcm(data):
    return struct.pack('<' + 'h' * FRAME_COUNT,
                       *(oracle(word[0]) for word in struct.iter_unpack('<H', data)))


def statistics(pcm):
    values = struct.unpack('<' + 'h' * FRAME_COUNT, pcm)
    return {
        'pcmSHA256': digest(pcm), 'sampleFrames': len(values),
        'sampleRateHz': 20000, 'durationSeconds': len(values) / 20000,
        'minimum': min(values), 'maximum': max(values),
        'rms': math.sqrt(sum(value * value for value in values) / len(values)),
        'mean': sum(values) / len(values),
        'zeroSamples': values.count(0),
        'samplesOutsideExpectedRange': sum(value < -8192 or value > 8176 for value in values),
    }


def compare(actual, expected):
    assert len(actual) == len(expected) == 0x80000, 'Incorrect decoded PCM length'
    return sum(a != b for a, b in zip(struct.iter_unpack('<h', actual),
                                    struct.iter_unpack('<h', expected)))


def fixture(directory, name, decoder, sanitized=False):
    source = directory / (name + '.cpp')
    executable = directory / name
    source.write_text(HARNESS.replace('__DECODER__', decoder))
    flags = FLAGS + (['-fsanitize=undefined', '-fno-sanitize-recover=all'] if sanitized else [])
    subprocess.run(['clang++', *flags, str(source), '-o', str(executable)],
                   check=True, capture_output=True)
    return executable, {
        'fixtureSourceSHA256': digest(source.read_bytes()),
        'fixtureExecutableSHA256': digest(executable.read_bytes()),
        'flags': flags,
    }


def decode(executable, data):
    result = subprocess.run([str(executable)], input=data, capture_output=True)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
    assert not result.stderr, result.stderr.decode(errors='replace')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'Assets')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'Documentation/title-audio-acceptance.json')
    args = parser.parse_args()
    assert platform.system() == 'Darwin' and platform.machine() == 'arm64', \
        'This regression must run natively on Apple Silicon'
    directory = ROOT / 'build/verification/title-audio'
    directory.mkdir(parents=True, exist_ok=True)
    data = (args.assets / '963a25.d5').read_bytes()
    assert len(data) == 0x80000 and zlib.crc32(data) == 0xfca078c7, \
        'Title ROM identity does not match the supplied World 4 Players revision'
    production = extract_decoder(DRIVER.read_text())
    with tarfile.open(ARCHIVE) as archive:
        member = archive.extractfile('src/burn/drv/konami/d_tmnt.cpp')
        assert member is not None
        legacy = extract_decoder(member.read().decode())
    # The routine always decodes 262,144 frames. Repeat every possible packed
    # word four times to test all 65,536 values without changing its loop.
    exhaustive = struct.pack('<65536H', *range(65536)) * 4
    exhaustive_expected = expected_pcm(exhaustive)
    title_expected = expected_pcm(data)
    report = {
        'passed': False,
        'scope': 'Compiled production title decoder; no claim of acoustic or full-system validation',
        'compiler': subprocess.run(['clang++', '--version'], check=True,
                                   capture_output=True, text=True).stdout.strip(),
        'platform': {'system': platform.system(), 'machine': platform.machine()},
        'productionDriverSHA256': digest(DRIVER.read_bytes()),
        'productionDecoderSHA256': digest(production.encode()),
        'legacyDecoderSHA256': digest(legacy.encode()),
        'legacyArchiveSHA256': digest(ARCHIVE.read_bytes()),
        'legacyRevision': UPSTREAM,
        'oracle': {
            'method': 'Yamaha sign/exponent inversion; signed 16-bit narrowing; arithmetic right shift; floor divide by four',
            'mameRevision': MAME_REVISION,
            'driverURL': MAME_BASE + '/src/mame/konami/tmnt.cpp',
            'decoderURL': MAME_BASE + '/3rdparty/ymfm/src/ymfm.h',
            'quarterScalePreserved': True,
        },
        'rom': {
            'filename': '963a25.d5', 'bytes': len(data),
            'crc32': f'{zlib.crc32(data):08x}', 'sha256': digest(data),
            'exponentCounts': dict(sorted(collections.Counter(str(word[0] >> 13)
                for word in struct.iter_unpack('<H', data)).items())),
        },
        'production': {},
    }
    for name, sanitized in [('optimized', False), ('undefinedBehaviorSanitizer', True)]:
        executable, result = fixture(directory, name, production, sanitized)
        all_pcm = decode(executable, exhaustive)
        title_pcm = decode(executable, data)
        result.update({
            'uniquePackedWordsChecked': 65536,
            'exhaustiveDecodedFrames': FRAME_COUNT,
            'exhaustiveMismatches': compare(all_pcm, exhaustive_expected),
            'titleMismatches': compare(title_pcm, title_expected),
            'exhaustivePCMSHA256': digest(all_pcm),
            'title': statistics(title_pcm),
            'sanitizerDiagnostics': 0 if sanitized else None,
        })
        assert result['exhaustiveMismatches'] == result['titleMismatches'] == 0, result
        report['production'][name] = result
    executable, negative = fixture(directory, 'legacy-optimized', legacy)
    legacy_all = decode(executable, exhaustive)
    legacy_title = decode(executable, data)
    negative.update({
        'exhaustiveMismatches': compare(legacy_all, exhaustive_expected),
        'titleMismatches': compare(legacy_title, title_expected),
        'title': statistics(legacy_title),
    })
    assert negative['titleMismatches'] > 0, 'Negative control did not reproduce corruption'
    executable, sanitized = fixture(directory, 'legacy-ubsan', legacy, True)
    result = subprocess.run([str(executable)], input=exhaustive, capture_output=True)
    diagnostics = result.stderr.decode(errors='replace')
    (directory / 'legacy-ubsan.stderr.txt').write_text(diagnostics)
    error = re.search(r'runtime error: ([^\n]+)', diagnostics)
    assert result.returncode != 0 and error and 'shift exponent' in error.group(1), diagnostics
    sanitized.update({'exitCode': result.returncode, 'diagnostic': error.group(1)})
    negative['undefinedBehaviorSanitizer'] = sanitized
    report['negativeControl'] = negative
    report['passed'] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: 65,536 encodings and {FRAME_COUNT:,} title samples match Yamaha oracle '
          'in optimized and UBSan arm64 fixtures.')
    print(f'Legacy negative control: {negative["titleMismatches"]:,} corrupt samples; '
          + sanitized['diagnostic'])
    print(args.output)


if __name__ == '__main__':
    main()
