import struct
import sys

def parse_curi(path):
    with open(path, "rb") as f:
        data = f.read()
    
    magic, version, const_count, inst_count, func_count = struct.unpack_from("<4sIIII", data, 0)
    print(f"Magic: {magic}, Version: {version}, Consts: {const_count}, Insts: {inst_count}, Funcs: {func_count}")
    
    offset = 20
    consts = []
    for _ in range(const_count):
        tag = data[offset]
        offset += 1
        if tag == 1:
            length = struct.unpack_from("<I", data, offset)[0]
            offset += 4
            string = data[offset:offset+length].decode('utf-8')
            consts.append(string)
            offset += length
        else:
            val = struct.unpack_from("<d", data, offset)[0]
            consts.append(val)
            offset += 8
    
    print("Constants:", consts)
    
    insts = []
    for _ in range(inst_count):
        inst = struct.unpack_from("<I", data, offset)[0]
        insts.append(inst)
        offset += 4
    
    print("Instructions:")
    for i, inst in enumerate(insts):
        op = inst & 0xFF
        a = (inst >> 8) & 0xFF
        b = (inst >> 16) & 0xFF
        c = (inst >> 24) & 0xFF
        bx = (inst >> 16) & 0xFFFF
        sbx = struct.unpack("<h", struct.pack("<H", bx))[0]
        print(f"{i:4d}: OP={op:2d} A={a:3d} B={b:3d} C={c:3d} Bx={bx:5d} sBx={sbx:5d}")
        
    print("Functions:")
    for _ in range(func_count):
        f_off, f_reg, f_param = struct.unpack_from("<III", data, offset)
        print(f"  Offset: {f_off}, Regs: {f_reg}, Params: {f_param}")
        offset += 12

if len(sys.argv) > 1:
    parse_curi(sys.argv[1])
else:
    parse_curi("test_coro_stress.curi")
