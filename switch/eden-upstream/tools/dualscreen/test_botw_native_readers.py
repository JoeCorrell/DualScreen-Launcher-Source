"""Compile the exact read-only native readers, without building an APK."""
import argparse
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root.parent/'mods/botw/native/state.cpp'
    text = source.read_text()
    begin = text.index('std::optional<VAddr> ResolvePointerGetter(')
    end = text.index('std::optional<s32> ReadGameDataS32(', begin)
    a.work.mkdir(parents=True, exist_ok=True)
    (a.work/'botw_test_getters.inc').write_text(text[begin:end])
    subprocess.run([str(a.compiler), '-std=c++20', '-O0', '-static-libstdc++',
                    '-I', str(a.work.resolve()),
                    str(root/'tools/dualscreen/test_botw_held_preview.cpp'),
                    '-o', str(a.work/'test-held-preview')], check=True)


if __name__ == '__main__':
    main()
