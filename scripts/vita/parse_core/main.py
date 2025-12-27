from elftools.elf.elffile import ELFFile

from collections import defaultdict
from argparse import ArgumentParser

from util import u16, u32, c_str, hexdump
from indent import indent, iprint
from elf import ElfParser
from core import CoreParser

str_stop_reason = defaultdict(str, {
    0: "No reason",
    0x30002: "Undefined instruction exception",
    0x30003: "Prefetch abort exception",
    0x30004: "Data abort exception",
    0x60080: "Division by zero",
})

str_status = defaultdict(str, {
    1: "Running",
    8: "Waiting",
    16: "Not started",
})

str_attr = defaultdict(str, {
    5: "RX",
    6: "RW",
})

reg_names = {
    13: "SP",
    14: "LR",
    15: "PC",
}

core = None

isPC = True

def print_module_info(module):
    iprint(module.name)
    with indent():
        for x, segment in enumerate(module.segments):
            iprint("Segment {}".format(x + 1))
            with indent():
                iprint("Start: 0x{:x}".format(segment.start))
                iprint("Size: 0x{:x} bytes".format(segment.size))
                iprint("Attributes: 0x{:x} ({})".format(segment.attr, str_attr[segment.attr & 0xF]))
                iprint("Alignment: 0x{:x}".format(segment.align))


def print_thread_info(thread, elf=None):
    iprint(thread.name)
    with indent():
        iprint("ID: 0x{:x}".format(thread.uid))
        iprint("Stop reason: 0x{:x} ({})".format(thread.stop_reason, str_stop_reason[thread.stop_reason]))
        iprint("Status: 0x{:x} ({})".format(thread.status, str_status[thread.status]))
        pc = core.get_address_notation("PC", thread.pc)
        iprint(pc.to_string(elf))
        if not pc.is_located():
            iprint(core.get_address_notation("LR", thread.regs.gpr[14]).to_string(elf))

def main():
    global core

    parser = ArgumentParser()
    parser.add_argument("-s", "--stack-size-to-print", dest="stacksize",
                        type=int, help="Number of addresses of the stack to print", metavar="SIZE", default=24)
    parser.add_argument("corefile")
    parser.add_argument("elffile")
    args = parser.parse_args()
    stackSize = args.stacksize

    elf = ElfParser(args.elffile)
    core = CoreParser(args.corefile)
    # iprint("=== MODULES ===")
    # with indent():
    #     for module in core.modules:
    #         print_module_info(module)
    # iprint()
    iprint("=== THREADS ===")
    crashed = []
    with indent():
        for thread in core.threads:
            if thread.stop_reason != 0:
                crashed.append(thread)
            print_thread_info(thread, elf)
    iprint()
    for thread in crashed:
        iprint('=== THREAD "{}" <0x{:x}> CRASHED ({}) ==='.format(thread.name, thread.uid, str_stop_reason[thread.stop_reason]))

        pc = core.get_address_notation('PC', thread.pc)
        pc.print_disas_if_available(elf)
        lr = core.get_address_notation('LR', thread.regs.gpr[14])
        lr.print_disas_if_available(elf)

        iprint("REGISTERS:")
        with indent():
            for x in range(14):
                reg = reg_names.get(x, "R{}".format(x))
                iprint("{}: 0x{:x}".format(reg, thread.regs.gpr[x]))

            iprint(pc)
            iprint(lr)
                
                
        iprint()

        iprint("STACK CONTENTS AROUND SP:")
        with indent():
            sp = thread.regs.gpr[13]
            for x in range(-16, stackSize):
                addr = 4 * x + sp
                data = core.read_vaddr(addr, 4)
                if data:
                    data = u32(data, 0)
                    prefix = "     "
                    if addr == sp:
                        prefix = "SP =>"
                    data_notation = core.get_address_notation("{} 0x{:x}".format(prefix, addr), data)
                    iprint(data_notation.to_string(elf))

if __name__ == "__main__":
    main()
