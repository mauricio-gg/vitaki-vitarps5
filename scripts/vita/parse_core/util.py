from elftools.common.py3compat import str2bytes

import string
import struct


def _byte_val(value):
    if isinstance(value, str):
        return ord(value)
    return value


def u16(buf, off):
    buf = buf[off:off+2]
    try:
        buf = str2bytes(buf)
    except AttributeError:
        pass
    return struct.unpack("<H", buf)[0]


def u32(buf, off):
    buf = buf[off:off+4]
    try:
        buf = str2bytes(buf)
    except AttributeError:
        pass
    return struct.unpack("<I", buf)[0]


def c_str(buf, off):
    out = bytearray()
    while off < len(buf):
        b = _byte_val(buf[off])
        if b == 0:
            break
        out.append(b)
        off += 1
    return out.decode('ascii', errors='ignore')


def hexdump(src, length=16, sep='.'):
    if isinstance(src, str):
        src = src.encode('latin-1')
    DISPLAY = string.digits + string.ascii_letters + string.punctuation
    FILTER = ''.join(((x if x in DISPLAY else sep) for x in map(chr, range(256))))
    lines = []
    for c in range(0, len(src), length):
        chars = src[c:c+length]
        hex_chunk = ' '.join(["%02x" % _byte_val(x) for x in chars])
        if len(hex_chunk) > 24:
            hex_chunk = "%s %s" % (hex_chunk[:24], hex_chunk[24:])
        printable = ''.join(["%s" % FILTER[_byte_val(x)] for x in chars])
        lines.append("%08x:  %-*s  |%s|\n" % (c, length*3, hex_chunk, printable))
    print(''.join(lines))
