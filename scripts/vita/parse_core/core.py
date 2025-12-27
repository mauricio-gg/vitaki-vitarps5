import gzip

from elftools.elf.elffile import ELFFile

from sys import argv
from collections import defaultdict

from util import u16, u32, c_str, hexdump
from indent import indent, iprint


class VitaThread():

    def __init__(self, data):
        self.uid = u32(data, 4)
        self.name = c_str(data, 8)
        self.stop_reason = u32(data, 0x74)
        self.status = u16(data, 0x30)
        self.pc = u32(data, 0x9C)

    def __str__(self):
        return "{} <0x{:x}>".format(self.name, self.uid)


class VitaModuleSegment():

    def __init__(self, data, num):
        self.num = num
        self.attr = u32(data, 4)
        self.start = u32(data, 8)
        self.size = u32(data, 12)
        self.align = u32(data, 16)


class VitaModule():

    def __init__(self, data):
        self.uid = u32(data, 4)
        self.num_segs = u32(data, 0x4C)
        self.name = c_str(data, 0x24)

    def parse_segs(self, data):
        self.segments = []
        for x in range(self.num_segs):
            sz = 0x14
            self.segments.append(VitaModuleSegment(data[sz*x:sz*(x+1)], x + 1))

    def parse_foot(self, data):
        # arm exception start/end
        pass


class VitaRegs():

    def __init__(self, data):
        self.tid = u32(data, 4)

        self.gpr = []
        for x in range(16):
            self.gpr.append(u32(data, 8 + 4 * x))


class VitaAddress():
    def __init__(self, symbol, vaddr, module = None, segment = None, offset = None):
        self.__symbol = symbol
        self.__module = module
        self.__segment = segment
        self.__offset = offset
        self.__vaddr = vaddr

    def is_located(self):
        return self.__module and self.__segment and self.__offset

    def print_disas_if_available(self, elf):
        addr_to_display = self.__vaddr
        if addr_to_display & 1 == 0:
            state = "ARM"
        else:
            state = "Thumb"
            addr_to_display &= ~1

        if self.is_located():
            iprint()
            iprint("DISASSEMBLY AROUND {}: 0x{:x} ({}):".format(self.__symbol, addr_to_display, state))
            elf.disas_around_addr(self.__offset)

    def to_string(self, elf=None):
        if self.is_located():
            output = "{}: 0x{:x} ({}@{} + 0x{:x}".format(self.__symbol, self.__vaddr,
                       self.__module.name, self.__segment.num, self.__offset)
            if elf and self.__module.name.endswith(".elf") and self.__segment.num == 1:
                output += " => {}".format(elf.addr2line(self.__offset))
            output += ')'
        else:
            output = "{}: 0x{:x}".format(self.__symbol, self.__vaddr)

        return output

    def __str__(self):
        return self.to_string()



class Segment():

    def __init__(self, vaddr, data):
        self.vaddr = vaddr
        self.data = data
        self.size = len(data)


class CoreParser():

    def __init__(self, filename):
        try:
            f = gzip.open(filename, "rb")
            self.elf = ELFFile(f)
        except IOError:
            f.close()
            f = open(filename, "rb")
            self.elf = ELFFile(f)
        
        self.init_notes()

        self.parse_modules()
        self.parse_threads()
        self.parse_thread_regs()

        f.close()

    def init_notes(self):
        self.notes = dict()
        self.segments = []

        for seg in self.elf.iter_segments():
            if seg.header.p_type == "PT_NOTE":
                for note in seg.iter_notes():
                    self.notes[note["n_name"]] = note["n_desc"]
            elif seg.header.p_type == "PT_LOAD":
                self.segments.append(Segment(seg.header.p_vaddr, seg.data()))

    def parse_modules(self):
        self.modules = []

        data = self.notes["MODULE_INFO"]
        num = u32(data, 4)
        off = 8
        for x in range(num):
            # module head
            sz = 0x50
            module = VitaModule(data[off:off+sz])
            off += sz
            # module segs
            sz = module.num_segs * 0x14
            module.parse_segs(data[off:off+sz])
            off += sz
            # module foot
            sz = 0x10
            module.parse_foot(data[off:off+sz])
            off += sz

            self.modules.append(module)

    def parse_threads(self):
        self.threads = []
        self.tid_to_thread = dict()

        data = self.notes["THREAD_INFO"]
        num = u32(data, 4)
        off = 8
        for x in range(num):
            sz = u32(data, off)
            thread = VitaThread(data[off:off+sz])
            self.threads.append(thread)
            self.tid_to_thread[thread.uid] = thread
            off += sz

    def parse_thread_regs(self):
        data = self.notes["THREAD_REG_INFO"]
        num = u32(data, 4)
        off = 8
        for x in range(num):
            sz = u32(data, off)
            regs = VitaRegs(data[off:off+sz])
            # assign registers to the thread they belong to
            self.tid_to_thread[regs.tid].regs = regs
            off += sz

    def get_address_notation(self, symbol, vaddr):
        for module in self.modules:
            for segment in module.segments:
                if vaddr >= segment.start and vaddr < segment.start + segment.size:
                    return VitaAddress(symbol, vaddr, module, segment, vaddr - segment.start)
        return VitaAddress(symbol, vaddr)

    def read_vaddr(self, addr, size):
        for segment in self.segments:
            if addr >= segment.vaddr and addr < segment.vaddr + segment.size:
                addr -= segment.vaddr
                return segment.data[addr:addr+size]
        return None
