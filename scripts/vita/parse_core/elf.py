from elftools.common.py3compat import str2bytes, bytes2str
from elftools.elf.elffile import ELFFile

from sys import argv
from collections import defaultdict
import subprocess

from util import u16, u32, c_str, hexdump
from indent import indent, iprint


class ElfParser():

    def __init__(self, filename):
        self.filename = filename
        f = open(filename, "rb")
        self.elf = ELFFile(f)

        self.rx_vaddr = -1
        self.parse_segments()

        f.close()

        self.open_addr2line()

    def parse_segments(self):
        for seg in self.elf.iter_segments():
            if seg["p_type"] != "PT_LOAD":
                continue
            if seg["p_flags"] == 5:
                self.rx_vaddr = seg["p_vaddr"]

    def disas_around_addr(self, addr):
        """ Addr is offset in executable segment """
        if addr & 1 != 0:
            addr &= ~1
            thumb = True
        else:
            thumb = False

        addr += self.rx_vaddr
        start = addr - 0x10
        end = addr + 0x10

        args = ["arm-vita-eabi-objdump", "-d", "-S",
            "--start-address=0x{:x}".format(start), "--stop-address=0x{:x}".format(end), self.filename]
        if thumb:
            args += ['-Mforce-thumb']

        output = subprocess.check_output(args)
        lines = output.split(str2bytes("\n"))
        keep = False
        new_lines = []
        for line in lines:
            if str2bytes("Disassembly of section") in line:
                keep = True
                continue
            if keep:
                new_lines.append(line)
        lines = new_lines

        for x, line in enumerate(lines):
            line = bytes2str(line)
            if "{:x}:".format(addr) in line:
                line = line[line.find("\t"):]
                line = "!!! \t{} !!!".format(line)
                line = '\033[91m' + line + '\033[0m'
            else:
                line = '\033[90m' + line + '\033[0m'
            lines[x] = line

        print("\n".join(lines))


    def open_addr2line(self):
        self.a2l = subprocess.Popen(["arm-vita-eabi-addr2line", "-e", self.filename, "-f", "-p", "-C"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def close_addr2line(self):
        self.a2l.kill()

    def addr2line(self, addr):
        """ Addr is offset in executable segment """
        addr += self.rx_vaddr
        self.a2l.stdin.write(str2bytes(hex(addr) + "\n"))
        self.a2l.stdin.flush()
        out = self.a2l.stdout.readline()
        return out.strip()
