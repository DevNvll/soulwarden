"""Recover state registrations from x86-64 instructions of the supported client.

This is a bounded static interpreter. Unknown registration arguments and unknown
control flow are errors. It does not execute the client or read a live process.
"""
from __future__ import annotations

import re
import hashlib
from functools import lru_cache

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RIP

BASE = 0x162F3E0
NEW_GROUP = 0x1677DD0
ADD_FIELD = 0x1775C60
ARRAY = 0x7AA69FC
OBJECT = 0x10000000000
STACK = 0x30000000000
HEAP = 0x20000000000
MASK = (1 << 64) - 1
VOLATILE = ('rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11')
HELPER_BYTES = (
    (BASE, 0xA8, 'efb3c40e99f2a0c52ca55d36aabaadbb3df104e16daebe341a9acca5397efea2'),
    (NEW_GROUP, 0x278, 'e6ba0954f9adcbc1c9e049ac399251d1aa2b12829d628e6b345231c85d81c5ff'),
    (ADD_FIELD, 0xB1, '4c468c6f7137a67103020d6b8929b286d50b275afd2d2b01ae9d642bda4cfbde'),
)


class UncertainLayout(ValueError):
    """The available instructions do not prove a complete layout."""


def canonical(name):
    aliases = {'eax': 'rax', 'ax': 'rax', 'al': 'rax', 'ah': 'rax',
               'ebx': 'rbx', 'bx': 'rbx', 'bl': 'rbx',
               'ecx': 'rcx', 'cx': 'rcx', 'cl': 'rcx',
               'edx': 'rdx', 'dx': 'rdx', 'dl': 'rdx',
               'esi': 'rsi', 'si': 'rsi', 'sil': 'rsi',
               'edi': 'rdi', 'di': 'rdi', 'dil': 'rdi',
               'ebp': 'rbp', 'bp': 'rbp', 'bpl': 'rbp',
               'esp': 'rsp', 'sp': 'rsp', 'spl': 'rsp'}
    return aliases.get(name, re.sub(r'^(r\d+)[dwb]$', r'\1', name))


class Instructions:
    def __init__(self, image):
        self.image = image
        self.engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.engine.detail = True

    @lru_cache(maxsize=None)
    def at(self, address):
        found = next(self.engine.disasm(self.image.read(address, 15), address, 1), None)
        if found is None:
            raise UncertainLayout(f'Cannot decode instruction at 0x{address:X}')
        return found

    @lru_cache(maxsize=None)
    def body(self, address):
        """Follow all local edges, including split PE unwind ranges."""
        pending = [address]
        result = {}
        while pending:
            pc = pending.pop()
            while pc not in result:
                if len(result) > 20000 or not address <= pc < address + 0x10000:
                    raise UncertainLayout(f'Function bounds are uncertain at 0x{address:X}')
                ins = self.at(pc)
                result[pc] = ins
                pc += ins.size
                if ins.mnemonic in ('ret', 'int3', 'ud2'):
                    break
                if ins.mnemonic.startswith('j'):
                    op = ins.operands[0]
                    if op.type != X86_OP_IMM:
                        raise UncertainLayout(f'Indirect jump at 0x{ins.address:X}')
                    target = op.imm
                    if address <= target < address + 0x10000:
                        pending.append(target)
                    elif ins.mnemonic != 'jmp':
                        raise UncertainLayout(f'External branch at 0x{ins.address:X}')
                    if ins.mnemonic == 'jmp':
                        break
        return result


class Interpreter:
    def __init__(self, image, instructions=None, image_values=None):
        self.image = image
        for address, size, digest in HELPER_BYTES:
            if hashlib.sha256(image.read(address, size)).hexdigest() != digest:
                raise UncertainLayout(f'Registration helper at 0x{address:X} changed; review its contract')
        self.ins = instructions or Instructions(image)
        self.memory = {}
        self.regs = {'rcx': OBJECT, 'rsp': STACK}
        self.sources = {}
        self.memory_sources = {}
        self.flags = None
        self.groups = {}
        self.special = []
        self.vectors = {}
        self.count = 0
        self.form = 'chained field mask'
        self.trace = []
        self.notes = []
        self.steps = 0
        self.calls = []
        self.registered_names = {}
        self.functions = set()
        self.image_values = image_values or {}
        for address, value in self.image_values.items():
            self.write_memory(address, 8, value)
        self.stop_at_reader = False

    def fail(self, ins, message):
        raise UncertainLayout(f'{message} at 0x{ins.address:X}: {ins.mnemonic} {ins.op_str}')

    def name_at(self, address):
        if address is None:
            return None
        address = self.image.normalize(address) if address < OBJECT else address
        try:
            if address >= OBJECT:
                raw = bytes(self.memory.get(address + n, 0) or 0 for n in range(160)).split(b'\0', 1)[0]
            else:
                raw = self.image.read(address, 160).split(b'\0', 1)[0]
        except ValueError:
            return None
        return raw.decode('ascii') if raw and all(32 <= n < 127 for n in raw) else None

    def reg(self, ins, op):
        name = ins.reg_name(op.reg)
        value = self.regs.get(canonical(name))
        if value is not None and op.size < 8:
            return value & ((1 << (op.size * 8)) - 1)
        return value

    def address(self, ins, op):
        mem = op.mem
        base = ins.address + ins.size if mem.base == X86_REG_RIP else self.regs.get(canonical(ins.reg_name(mem.base)), 0) if mem.base else 0
        index = self.regs.get(canonical(ins.reg_name(mem.index))) if mem.index else 0
        if base is None or index is None:
            return None
        return (base + index * mem.scale + mem.disp) & MASK

    def read(self, address, size):
        if address is None:
            return None
        if all(address + n in self.memory for n in range(size)):
            values = [self.memory[address + n] for n in range(size)]
            return None if None in values else int.from_bytes(bytes(values), 'little')
        if address < 0x100000000:
            try:
                return int.from_bytes(self.image.read(address, size), 'little')
            except ValueError:
                return None
        return None

    def value(self, ins, op):
        if op.type == X86_OP_IMM:
            return op.imm & ((1 << (op.size * 8)) - 1)
        if op.type == X86_OP_REG:
            return self.reg(ins, op)
        if op.type == X86_OP_MEM:
            return self.read(self.address(ins, op), op.size)
        return None

    def write_memory(self, address, size, value):
        if address is None:
            return
        raw = (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little') if value is not None else [None] * size
        for n, byte in enumerate(raw):
            self.memory[address + n] = byte

    def write(self, ins, op, value):
        source = set()
        if len(ins.operands) > 1:
            incoming = ins.operands[1]
            if incoming.type == X86_OP_REG:
                source = set(self.sources.get(canonical(ins.reg_name(incoming.reg)), ()))
            elif incoming.type == X86_OP_MEM:
                address = self.address(ins, incoming)
                if address is not None:
                    source = set(self.memory_sources.get(address, ()))
                    section = self.image.section_of(address) if address < OBJECT else None
                    if section and section[0] == '.data' and address not in self.memory:
                        source.add(address)
        if op.type == X86_OP_REG:
            name = canonical(ins.reg_name(op.reg))
            if ins.mnemonic in ('add', 'sub', 'and', 'or', 'xor', 'shr', 'sar', 'shl', 'imul', 'inc', 'dec'):
                source.update(self.sources.get(name, ()))
            if ins.mnemonic == 'xor' and len(ins.operands) == 2 and ins.operands[0].reg == ins.operands[1].reg:
                source.clear()
            self.sources[name] = source
            if op.size >= 4:
                self.regs[name] = value & ((1 << (op.size * 8)) - 1) if value is not None else None
            else:
                previous = self.regs.get(name)
                mask = (1 << (op.size * 8)) - 1
                self.regs[name] = (previous & ~mask) | (value & mask) if previous is not None and value is not None else None
        elif op.type == X86_OP_MEM:
            address = self.address(ins, op)
            if address is None:
                self.fail(ins, 'Memory write address is uncertain')
            self.memory_sources[address] = source
            self.write_memory(address, op.size, value)

    def vector(self, address, group, stride=24):
        data = HEAP + (len(self.vectors) + 1) * 0x100000
        self.write_memory(address, 8, data)
        self.write_memory(address + 8, 8, data)
        self.write_memory(address + 16, 8, data + 0x80000)
        self.vectors[address] = (group, stride, data)

    def base(self):
        self.count = 1
        self.groups[0] = []
        self.write_memory(OBJECT + 0x10, 8, HEAP)
        self.write_memory(OBJECT + 0x18, 8, HEAP + 0x320)
        self.vector(HEAP, 0)
        self.vector(OBJECT + 0x680, 'header')

    def flush_vectors(self, ins):
        for address, (group, stride, data) in self.vectors.items():
            end = self.read(address + 8, 8)
            if end is None or not data <= end <= data + 0x80000 or (end - data) % stride:
                self.fail(ins, 'Field vector is uncertain')
            result = []
            for row in range(data, end, stride):
                name_rva = self.read(row, 8)
                offset = self.read(row + 8, 8)
                name = self.registered_names.get(row) or self.name_at(name_rva)
                if not name or offset is None or not OBJECT <= offset < OBJECT + 0x100000 or (offset - OBJECT) % 8:
                    self.fail(ins, 'Inline field registration is uncertain')
                result.append((name, (offset - OBJECT) // 8))
            if group == 'header':
                self.special = result
            else:
                self.groups[group] = result

    def append(self, ins, group, name_rva, field):
        name = self.name_at(name_rva)
        if not name or field is None or not OBJECT <= field < OBJECT + 0x100000 or (field - OBJECT) % 8:
            self.fail(ins, 'Field name or address is uncertain')
        if group not in self.groups:
            self.fail(ins, f'Field group {group} is uncertain')
        vector = next(a for a, v in self.vectors.items() if v[0] == group)
        end = self.read(vector + 8, 8)
        self.registered_names[end] = name
        self.write_memory(end, 8, name_rva)
        self.write_memory(end + 8, 8, field)
        self.write_memory(end + 16, 1, 0)
        self.write_memory(vector + 8, 8, end + 24)
        self.trace.append({'site': ins.address, 'group': group, 'name': name, 'offset': field - OBJECT})

    def clobber(self):
        for name in VOLATILE:
            self.regs[name] = None
            self.sources[name] = set()
        for index in range(6):
            self.regs[f'xmm{index}'] = None
        self.flags = None

    def call(self, ins, depth):
        op = ins.operands[0]
        target = op.imm if op.type == X86_OP_IMM else None
        this = self.regs.get('rcx')
        if target == 0x7AA68B0:
            # __chkstk preserves the general registers and RAX.
            return
        if target in (0x7AA6598, 0x7AA652C):
            if this is None or not this < OBJECT:
                self.fail(ins, 'Static initializer guard is uncertain')
            self.write_memory(this, 4, 0xffffffff if target == 0x7AA6598 else 0)
            self.clobber()
            return
        if target == 0x2B0E70:
            text = self.name_at(self.regs.get('rdx'))
            if text is not None and '%' in text:
                number = self.regs.get('r8')
                if text.count('%d') != 1 or text.replace('%d', '').count('%') or number is None:
                    self.fail(ins, 'Formatted field name is uncertain')
                text = text.replace('%d', str(number))
            if this is None or text is None:
                self.fail(ins, 'Temporary field name is uncertain')
            data = this if len(text) < 16 else STACK - 0x100000 - len(self.trace) * 0x1000
            self.write_memory(data, len(text) + 1, int.from_bytes(text.encode() + b'\0', 'little'))
            if data != this:
                self.write_memory(this, 8, data)
            self.write_memory(this + 0x10, 8, len(text))
            self.write_memory(this + 0x18, 8, 15 if len(text) < 16 else len(text))
            self.clobber()
            self.regs['rax'] = this
            return
        if target == BASE:
            if this != OBJECT:
                self.fail(ins, 'Base state pointer is uncertain')
            self.base()
            self.clobber()
            self.regs['rax'] = OBJECT
        elif target == NEW_GROUP:
            if this != OBJECT or not 0 < self.count < 10:
                self.fail(ins, 'New group arguments are uncertain')
            group = self.count
            self.count += 1
            self.groups[group] = []
            self.vector(HEAP + group * 0x320, group)
            self.write_memory(OBJECT + 0x18, 8, HEAP + self.count * 0x320)
            self.clobber()
            self.regs['rax'] = group
            self.trace.append({'site': ins.address, 'new_group': group})
        elif target == ADD_FIELD:
            if this != OBJECT:
                self.fail(ins, 'Field state pointer is uncertain')
            if self.sources.get('r9'):
                self.fail(ins, 'Group uses an unproved image variable: ' + ', '.join(f'0x{a:X}' for a in sorted(self.sources['r9'])))
            self.append(ins, self.regs.get('r9'), self.regs.get('rdx'), self.regs.get('r8'))
            self.clobber()
        elif target == ARRAY:
            size, count, ctor = (self.regs.get(r) for r in ('rdx', 'r8', 'r9'))
            if this == OBJECT + 0x10 and size != 0x3B0:
                self.fail(ins, f'Unsupported inline group stride {size}; the field mask contract is not proved')
            if this == OBJECT + 0x10 and size == 0x3B0:
                if count is None or not 0 < count <= 8 or ctor != 0x2A37D10:
                    self.fail(ins, 'Wide group count is uncertain')
                self.count = count
                self.groups = {n: [] for n in range(count)}
                self.form = 'wide field mask (size64)'
                for n in range(count):
                    self.vector(this + n * size + 0x358, n, 16)
                    self.write_memory(this + n * size + 0x398, 1, 0)
            self.clobber()
        elif this == OBJECT and not self.count and target is not None and target != 0x6152AD0:
            self.execute(target, depth + 1)
        elif this == OBJECT and target is not None:
            try:
                body = self.ins.body(target)
            except UncertainLayout:
                self.fail(ins, 'Whole-state helper cannot be checked')
            if any(i.mnemonic in ('call', 'jmp') and i.operands[0].type == X86_OP_IMM and i.operands[0].imm in (ADD_FIELD, NEW_GROUP) for i in body.values()):
                self.execute(target, depth + 1)
            else:
                self.calls.append((ins.address, target, this))
                self.clobber()
        else:
            # Calls are opaque. Only caller-saved registers lose their values.
            # A later registration that uses an opaque result fails explicitly.
            self.calls.append((ins.address, target, this))
            self.clobber()

    def condition(self, name):
        if self.flags is None:
            return None
        zero, carry, signed_less = self.flags
        return {'e': zero, 'z': zero, 'ne': not zero, 'nz': not zero,
                'b': carry, 'c': carry, 'nae': carry, 'ae': not carry,
                'nb': not carry, 'a': not carry and not zero,
                'be': carry or zero, 'l': signed_less, 'ge': not signed_less,
                'g': not signed_less and not zero, 'le': signed_less or zero}.get(name)

    def set_flags(self, a, b, size, test=False):
        if a is None or b is None:
            self.flags = None
            return
        sign = 1 << (size * 8 - 1)
        signed_a = a - 2 * sign if a & sign else a
        signed_b = b - 2 * sign if b & sign else b
        self.flags = ((a & b) == 0, False, bool(a & b & sign)) if test else (a == b, a < b, signed_a < signed_b)

    def execute(self, start, depth=0):
        if depth > 8:
            raise UncertainLayout('Constructor inheritance exceeds eight calls')
        self.functions.add(start)
        pc = start
        while True:
            if start == 0x671E040 and pc == 0x671E51A:
                # Reviewed suffix: default values in two fields and destruction
                # of stack temporaries. No field/group list changes occur here.
                digest = hashlib.sha256(self.image.read(pc, 0x671E732 - pc)).hexdigest()
                if digest != '7c019c07e8814463fe8d9a27f4494512b9865dc396cdeb01036c036ed5f61664':
                    raise UncertainLayout('Vitals constructor suffix changed; review its registration effects')
                self.notes.append('Vitals default-value suffix matches the reviewed instruction bytes.')
                self.flush_vectors(self.ins.at(pc))
                return
            self.steps += 1
            if self.steps > 100000 or not start <= pc < start + 0x10000:
                raise UncertainLayout(f'Constructor exceeds static limits at 0x{pc:X}')
            ins = self.ins.at(pc)
            pc += ins.size
            m, ops = ins.mnemonic, ins.operands
            if m == 'ret':
                self.flush_vectors(ins)
                return
            if m in ('nop', 'endbr64'):
                continue
            if m in ('int3', 'ud2'):
                self.fail(ins, 'Constructor reached a trap')
            if m == 'call':
                if self.stop_at_reader and ops[0].type == X86_OP_IMM and ops[0].imm == 0x6160AE0:
                    self.flush_vectors(ins)
                    return
                self.call(ins, depth)
            elif m == 'push':
                self.regs['rsp'] -= 8
                self.write_memory(self.regs['rsp'], 8, self.value(ins, ops[0]))
            elif m == 'pop':
                self.write(ins, ops[0], self.read(self.regs['rsp'], 8))
                self.regs['rsp'] += 8
            elif m == 'lea':
                self.write(ins, ops[0], self.address(ins, ops[1]))
            elif m == 'movsxd':
                value = self.value(ins, ops[1])
                if value is not None and value & (1 << (ops[1].size * 8 - 1)):
                    value -= 1 << (ops[1].size * 8)
                self.write(ins, ops[0], value)
            elif m.startswith('mov'):
                self.write(ins, ops[0], self.value(ins, ops[1]))
            elif m in ('cmp', 'test'):
                self.set_flags(self.value(ins, ops[0]), self.value(ins, ops[1]), ops[0].size, m == 'test')
            elif m in ('xor', 'xorps', 'xorpd', 'pxor') and ops[0].type == ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
                self.write(ins, ops[0], 0)
                self.flags = (True, False, False)
            elif m in ('add', 'sub', 'and', 'or', 'xor', 'shr', 'sar', 'shl', 'imul', 'inc', 'dec'):
                if m == 'imul' and len(ops) == 1:
                    a, b = self.regs.get('rax'), self.value(ins, ops[0])
                    if a is None or b is None:
                        self.regs['rax'] = self.regs['rdx'] = None
                    else:
                        a = a - (1 << 64) if a >> 63 else a
                        b = b - (1 << 64) if b >> 63 else b
                        product = a * b
                        self.regs['rax'], self.regs['rdx'] = product & MASK, (product >> 64) & MASK
                    self.flags = None
                    continue
                a = self.value(ins, ops[0])
                b = 1 if m in ('inc', 'dec') else self.value(ins, ops[1]) if len(ops) > 1 else None
                if m == 'imul' and len(ops) == 3:
                    a, b = self.value(ins, ops[1]), self.value(ins, ops[2])
                value = None
                if a is not None and b is not None:
                    value = {'add': lambda: a + b, 'sub': lambda: a - b, 'inc': lambda: a + b,
                             'dec': lambda: a - b, 'and': lambda: a & b, 'or': lambda: a | b,
                             'xor': lambda: a ^ b, 'shr': lambda: a >> b, 'shl': lambda: a << b,
                             'sar': lambda: (a - (1 << (ops[0].size * 8)) if a & (1 << (ops[0].size * 8 - 1)) else a) >> b,
                             'imul': lambda: a * b}[m]()
                self.write(ins, ops[0], value)
                self.set_flags(value & ((1 << (ops[0].size * 8)) - 1) if value is not None else None, 0, ops[0].size)
            elif m.startswith('cmov'):
                condition = self.condition(m[4:])
                self.write(ins, ops[0], self.value(ins, ops[1]) if condition else self.value(ins, ops[0]) if condition is not None else None)
            elif m.startswith('j'):
                if ops[0].type != X86_OP_IMM:
                    self.fail(ins, 'Indirect constructor jump')
                if m == 'jmp' and ops[0].imm in (ADD_FIELD, NEW_GROUP):
                    self.call(ins, depth)
                    self.flush_vectors(ins)
                    return
                taken = True if m == 'jmp' else self.condition(m[1:])
                if taken is None:
                    target = self.ins.at(ops[0].imm)
                    next_ins = self.ins.at(target.address + target.size)
                    if m == 'jg' and target.mnemonic == 'lea' and target.op_str.startswith('rcx, [rip') and next_ins.mnemonic == 'call' and next_ins.operands[0].imm == 0x7AA6598:
                        # Follow the cold initialization path of the MSVC guard.
                        taken = True
                        self.notes.append(f'Static group initializer at 0x{target.address:X} uses the cold path.')
                    else:
                        self.fail(ins, 'Branch condition is uncertain')
                if taken:
                    pc = ops[0].imm
            else:
                # Unsupported instructions taint every register they write.
                _, writes = ins.regs_access()
                for reg in writes:
                    self.regs[canonical(ins.reg_name(reg))] = None
                if any(ins.reg_name(reg) in ('rflags', 'eflags') for reg in writes):
                    self.flags = None
                if any(op.type == X86_OP_MEM and op.access & capstone.CS_AC_WRITE for op in ops):
                    self.fail(ins, 'Unsupported memory write')

    def result(self, constructor, wrapper=False):
        if constructor == 0x671E040 and not all(address in self.image_values for address in (0xA563290, 0xA563298)):
            raise UncertainLayout('Vitals needs proved group variables: --group-variable 0xA563290=0 --group-variable 0xA563298=0. Protocol notes 4.5 prove these values for the supported live client.')
        if constructor == BASE:
            self.base()
            return {'constructor': constructor, 'count': self.count, 'groups': self.groups,
                    'special': [], 'form': self.form, 'trace': [], 'notes': []}
        if wrapper:
            self.regs = {'rcx': None, 'rdx': None, 'r8': OBJECT, 'r9': None, 'rsp': STACK}
            self.stop_at_reader = True
        self.execute(constructor)
        required = {i.address for start in self.functions for i in self.ins.body(start).values()
                    if i.mnemonic in ('call', 'jmp') and i.operands[0].type == X86_OP_IMM
                    and i.operands[0].imm in (ADD_FIELD, NEW_GROUP)}
        visited = {event['site'] for event in self.trace}
        if required - visited:
            raise UncertainLayout('Registration call was not accounted for: ' + ', '.join(f'0x{a:X}' for a in sorted(required - visited)))
        if not self.count:
            raise UncertainLayout('No replicated state base or wide groups were proved')
        return {'constructor': constructor, 'count': self.count, 'groups': self.groups,
                'special': self.special, 'form': self.form, 'trace': self.trace,
                'notes': self.notes}


def constructor_of(image, unmarshal, instructions=None):
    instructions = instructions or Instructions(image)
    machine = Interpreter(image, instructions)
    machine.regs = {'rcx': None, 'rdx': None, 'r8': OBJECT, 'rsp': STACK}
    pc = unmarshal
    for _ in range(80):
        ins = instructions.at(pc)
        pc += ins.size
        if ins.mnemonic == 'call':
            if ins.operands[0].type != X86_OP_IMM or machine.regs.get('rcx') != OBJECT:
                machine.fail(ins, 'Unmarshal constructor call is uncertain')
            target = ins.operands[0].imm
            if target == BASE:
                body = instructions.body(unmarshal)
                if any(i.mnemonic == 'call' and i.operands[0].type == X86_OP_IMM and i.operands[0].imm == ADD_FIELD for i in body.values()):
                    return unmarshal
            return target
        if ins.mnemonic in ('ret', 'jmp'):
            machine.fail(ins, 'Unmarshal constructor was not found')
        if ins.mnemonic.startswith('mov'):
            machine.write(ins, ins.operands[0], machine.value(ins, ins.operands[1]))
        elif ins.mnemonic == 'lea':
            machine.write(ins, ins.operands[0], machine.address(ins, ins.operands[1]))
        elif ins.mnemonic == 'push':
            machine.regs['rsp'] -= 8
        elif ins.mnemonic == 'sub' and ins.op_str.startswith('rsp,'):
            machine.regs['rsp'] -= ins.operands[1].imm
        elif ins.mnemonic == 'nop':
            continue
        else:
            _, writes = ins.regs_access()
            for reg in writes:
                machine.regs[canonical(ins.reg_name(reg))] = None
    raise UncertainLayout('Unmarshal exceeds 80 instructions')
