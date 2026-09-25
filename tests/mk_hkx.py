"""Write a synthetic Havok classic packfile (.HKX): a minimal but structurally
valid two-section (__classnames__ + __data__) big-endian packfile with the
exact header/section-table layout confirmed against Tenchu: Shadow Assassins'
retail Common/Motion/*.HKX samples (see project/src/lib-hkx.h).
"""
import struct, sys

def be32(v):
    return struct.pack(">I", v)

HDR_SIZE = 0x40
SEC_SIZE = 48

classnames_data = b"\x00\x00\x00\x00hkRootLevelContainer\x00" + b"\x00" * 4
data_data = b"\x00" * 16

sections = [
    ("__classnames__", classnames_data),
    ("__data__", data_data),
]

sec_table_size = SEC_SIZE * len(sections)
offset = HDR_SIZE + sec_table_size
sec_headers = b""
blobs = b""
for name, blob in sections:
    name_field = name.encode().ljust(20, b"\x00")
    sec_headers += name_field + be32(offset) + be32(len(blob)) * 5
    blobs += blob
    offset += len(blob)

header = be32(0x57e0e057) + be32(0x10c0c010)
header += be32(0) + be32(4)  # userTag, fileVersion
header += bytes([4, 0, 1, 1])  # ptrSize, bigEndian, reusePad, emptyBase
header += be32(len(sections))
header += be32(1) + be32(0)  # contentsSectionIndex/Offset
header += be32(0) + be32(0x65)  # contentsClassNameSectionIndex/Offset
header += b"Havok-4.6.1-r1".ljust(16, b"\x00")
header = header.ljust(HDR_SIZE, b"\xff")

open(sys.argv[1], "wb").write(header + sec_headers + blobs)
