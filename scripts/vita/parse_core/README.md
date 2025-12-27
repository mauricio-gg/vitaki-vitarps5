# vita-parse-core

## Installation

```
pip install -r requirements.txt
```

Note that you need the vitasdk toolchain in your path. This tool will use `arm-vita-eabi-objdump` and `arm-vita-eabi-addr2line`.

## Usage

Get a vita core file from `ux0:data` (a `.psp2dmp` file). It's not necessary to `gunzip` it, the tool will do it itself.

Run:

```
python2 main.py core_file.psp2dmp homebrew_file.elf
```

Note that `homebrew_file.elf` is an `.elf` file, not `.velf`, not `eboot.bin`. It's recommended that you build it with the `-g` gcc flag enabled to get better debugging information.

## Example output

```
=== THREADS ===
    CRASHBREW
        ID: 0x40010003
        Stop reason: 0x30004 (Data abort exception)
        Status: 0x1 (Running)
        PC: 0x810462ae (crashbrew.elf@1 + 0x2ae)
    SceCommonDialogWorker
        ID: 0x40010073
        Stop reason: 0x0 (No reason)
        Status: 0x8 (Waiting)
        PC: 0xe000af94 (SceLibKernel@1 + 0x6304)

=== THREAD "CRASHBREW" <0x40010003> CRASHED (Data abort exception) ===

DISASSEMBLY AROUND 0x810462ae:

0000829e <main+0xae>:
	} else if (key == SCE_CTRL_CIRCLE) {
		printf("PABT at NULL\n");
		void (*f)() = NULL;
		f();
	} else if (key == SCE_CTRL_SQUARE) {
		printf("DABT at unmapped addr\n");
    829e:	f000 f9c1 	bl	8624 <psvDebugScreenPrintf>
		int *c = 0xD0123456;
    82a2:	f243 4356 	movw	r3, #13398	; 0x3456
    82a6:	f2cd 0312 	movt	r3, #53266	; 0xd012
    82aa:	607b      	str	r3, [r7, #4]
		int x = *c;
    82ac:	687b      	ldr	r3, [r7, #4]
!!! 		681b      	ldr	r3, [r3, #0] !!!
    82b0:	603b      	str	r3, [r7, #0]
    82b2:	e005      	b.n	82c0 <main+0xd0>
	} else if (key == SCE_CTRL_TRIANGLE) {
    82b4:	697b      	ldr	r3, [r7, #20]
    82b6:	f5b3 5f80 	cmp.w	r3, #4096	; 0x1000
    82ba:	d101      	bne.n	82c0 <main+0xd0>
		overflow_stack();
    82bc:	f7ff ff92 	bl	81e4 <overflow_stack>

REGISTERS:
    R0: 0x0
    R1: 0xdeadbeef
    R2: 0xdeadbeef
    R3: 0xd0123456
    R4: 0xdeadbeef
    R5: 0xdeadbeef
    R6: 0xdeadbeef
    R7: 0x81140fd0
    R8: 0xdeadbeef
    R9: 0xdeadbeef
    R10: 0xdeadbeef
    R11: 0xdeadbeef
    R12: 0xdeadbeef
    SP: 0x81140fd0
    LR: 0x810462a3
    PC: 0x810462ae (crashbrew.elf@1 + 0x2ae)

STACK CONTENTS AROUND SP:
          0x81140fc0: 0x8104ffc4 => crashbrew.elf@1 + 0x9fc4 => .LC8 at main.c:?
          0x81140fc4: 0xdeadbeef 
          0x81140fc8: 0xb 
          0x81140fcc: 0x8000 
    SP => 0x81140fd0: 0x810d0000 => crashbrew.elf@2 + 0x0
          0x81140fd4: 0xd0123456 
          0x81140fd8: 0x2 
          0x81140fdc: 0x8104674f => crashbrew.elf@1 + 0x74f => __libc_init_array at ??:?
          0x81140fe0: 0xdeadbeef 
          0x81140fe4: 0x8000 
          0x81140fe8: 0xdeadbeef 
          0x81140fec: 0x81046137 => crashbrew.elf@1 + 0x137 => _start at ??:?
          0x81140ff0: 0x810500a4 => crashbrew.elf@1 + 0xa0a4 => $d at crt0.c:?
          0x81140ff4: 0x0 
          0x81140ff8: 0xffffffff 
          0x81140ffc: 0xe00053cd => SceLibKernel@1 + 0x73d
```

## Support

Please join #vitasdk on freenode and contact xyz if something goes wrong.
