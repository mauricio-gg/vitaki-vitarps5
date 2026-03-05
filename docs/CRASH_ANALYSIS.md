# Crash Dump Analysis

If the PS Vita throws a `C2-12828-1` crash, it generates a `.psp2dmp` file in `ux0:data/`. You can decode this locally to get a file/line backtrace.

## Setup

Initialize the parser submodule (one-time):

```bash
git submodule update --init scripts/vita/parse_core
```

## Decoding a Crash Dump

1. Pull the `.psp2dmp` file from `ux0:data/` on the Vita
2. Enter the dev container (pyelftools is preinstalled):

```bash
./tools/build.sh shell
```

3. Inside the shell, run the parser against the dump and your debug ELF:

```bash
python3 scripts/vita/parse_core/main.py \
  /build/git/path/to/psp2core-xxxxxxxxx.psp2dmp \
  /build/git/build/vita/VitaRPS5.elf
```

The script prints thread states, register dumps, and a file/line backtrace.

**Note:** You need a debug build (`./tools/build.sh debug`) to get meaningful symbol information in the backtrace.
