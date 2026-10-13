#!/usr/bin/env python3
"""Readable pseudo-code of the compiled Lua 5.1 scripts of the game (analysis only).

The game scripts (`*.luac` in the pak files) are standard Lua 5.1 bytecode with a 2-byte
prefix and no debug data. This tool folds register operations into expressions and prints
one block for each function. It is a reading aid, not a decompiler:

- Control flow stays as labels and `goto`.
- `rN` is a register, `aN` a parameter, `^name` an upvalue. The first parameter of a function
  that is stored in a table field is printed as `self` (an assumption).
- An expression that reads state is printed where it is used, not where it was computed.

Check a function against `luac5.1 -l -l` when the exact sequence is important.

Usage: luadis.py FILE.luac...        (writes FILE.lua.txt next to each input file)
"""

from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

OPS = ("MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE NEWTABLE "
       "SELF ADD SUB MUL DIV MOD POW UNM NOT LEN CONCAT JMP EQ LT LE TEST TESTSET CALL TAILCALL RETURN "
       "FORLOOP FORPREP TFORLOOP SETLIST CLOSE CLOSURE VARARG").split()
OP = {name: index for index, name in enumerate(OPS)}
ARITH = {"ADD": "+", "SUB": "-", "MUL": "*", "DIV": "/", "MOD": "%", "POW": "^"}
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def take(self, layout: str):
        value = struct.unpack_from("<" + layout, self.data, self.position)
        self.position += struct.calcsize("<" + layout)
        return value[0] if len(value) == 1 else value

    def string(self):
        size = self.take("Q")
        if size == 0:
            return None
        text = self.data[self.position:self.position + size - 1].decode("utf-8", "replace")
        self.position += size
        return text


class Proto:
    pass


def read_function(reader: Reader) -> Proto:
    proto = Proto()
    reader.string()
    reader.take("ii")
    proto.nups, proto.numparams, proto.vararg, proto.maxstack = reader.take("BBBB")
    proto.code = [reader.take("I") for _ in range(reader.take("i"))]
    proto.constants = []
    for _ in range(reader.take("i")):
        kind = reader.take("B")
        if kind == 0:
            proto.constants.append("nil")
        elif kind == 1:
            proto.constants.append("true" if reader.take("B") else "false")
        elif kind == 3:
            number = reader.take("d")
            proto.constants.append(str(int(number)) if number == int(number) and abs(number) < 1e15 else repr(number))
        else:
            proto.constants.append(json.dumps(reader.string(), ensure_ascii=False))
    proto.protos = [read_function(reader) for _ in range(reader.take("i"))]
    line_count = reader.take("i")
    reader.position += 4 * line_count
    for _ in range(reader.take("i")):
        reader.string()
        reader.take("ii")
    for _ in range(reader.take("i")):
        reader.string()
    return proto


def decode(word: int):
    return OPS[word & 0x3F], (word >> 6) & 0xFF, (word >> 23) & 0x1FF, (word >> 14) & 0x1FF, (word >> 14) & 0x3FFFF


class Entry:
    def __init__(self, text, impure, compound, deps, selfobj, sequence):
        self.text, self.impure, self.compound, self.deps = text, impure, compound, deps
        self.selfobj, self.sequence = selfobj, sequence


class Function:
    def __init__(self, proto: Proto, ident: str, name: str, upvalues: list[str], method: bool):
        self.proto, self.ident, self.name, self.upvalues, self.method = proto, ident, name, upvalues, method
        self.lines: list[str] = []
        self.children: dict[int, tuple[str, bool, list[str]]] = {}
        self.pending: dict[int, Entry] = {}
        self.sequence = 0
        self.deps = 0
        self.hints: dict[int, str] = {}

    # -- static analysis ----------------------------------------------------
    def analyze(self) -> None:
        code = self.proto.code
        count = len(code)
        self.uses, self.defs = [0] * count, [0] * count
        self.succs: list[list[int]] = [[] for _ in range(count)]
        self.skip = [False] * count
        self.targets: set[int] = set()
        self.top_at = [0] * count
        top = 0

        def span(first, last):
            return ((1 << (last + 1)) - 1) & ~((1 << first) - 1) if last >= first else 0

        pc = 0
        while pc < count:
            op, a, b, c, bx = decode(code[pc])
            sbx = bx - 131071
            use = define = 0
            succ = [pc + 1]
            self.top_at[pc] = top

            def rk(value):
                return (1 << value) if value < 256 else 0

            if op == "MOVE":
                use, define = 1 << b, 1 << a
            elif op in ("LOADK", "GETUPVAL", "GETGLOBAL", "NEWTABLE"):
                define = 1 << a
            elif op == "LOADBOOL":
                define = 1 << a
                if c:
                    succ = [pc + 2]
                    self.targets.add(pc + 2)
            elif op == "LOADNIL":
                define = span(a, b)
            elif op == "GETTABLE":
                use, define = (1 << b) | rk(c), 1 << a
            elif op in ("SETGLOBAL", "SETUPVAL"):
                use = 1 << a
            elif op == "SETTABLE":
                use = (1 << a) | rk(b) | rk(c)
            elif op == "SELF":
                use, define = (1 << b) | rk(c), span(a, a + 1)
            elif op in ARITH:
                use, define = rk(b) | rk(c), 1 << a
            elif op in ("UNM", "NOT", "LEN"):
                use, define = 1 << b, 1 << a
            elif op == "CONCAT":
                use, define = span(b, c), 1 << a
            elif op == "JMP":
                succ = [pc + 1 + sbx]
                self.targets.add(pc + 1 + sbx)
            elif op in ("EQ", "LT", "LE"):
                use = rk(b) | rk(c)
                succ = [pc + 1, pc + 2]
            elif op == "TEST":
                use = 1 << a
                succ = [pc + 1, pc + 2]
            elif op == "TESTSET":
                use = 1 << b
                succ = [pc + 1, pc + 2]
            elif op in ("CALL", "TAILCALL"):
                use = span(a, a + b - 1) if b else span(a, top)
                if op == "TAILCALL":
                    succ = []
                elif c == 0:
                    top = a
                    define = 1 << a
                else:
                    define = span(a, a + c - 2)
            elif op == "RETURN":
                use = span(a, a + b - 2) if b else span(a, top)
                succ = []
            elif op == "FORLOOP":
                use, define = span(a, a + 2), (1 << a) | (1 << (a + 3))
                succ = [pc + 1 + sbx, pc + 1]
                self.targets.add(pc + 1 + sbx)
            elif op == "FORPREP":
                use, define = span(a, a + 2), 1 << a
                succ = [pc + 1 + sbx]
                self.targets.add(pc + 1 + sbx)
            elif op == "TFORLOOP":
                use, define = span(a, a + 2), span(a + 2, a + 2 + c)
                succ = [pc + 1, pc + 2]
            elif op == "SETLIST":
                use = span(a, a + b) if b else span(a, top)
                if c == 0:
                    self.skip[pc + 1] = True
                    succ = [pc + 2]
            elif op == "CLOSURE":
                define = 1 << a
                nups = self.proto.protos[bx].nups
                for index in range(nups):
                    pseudo = decode(code[pc + 1 + index])
                    if pseudo[0] == "MOVE":
                        use |= 1 << pseudo[2]
                    self.skip[pc + 1 + index] = True
                succ = [pc + 1 + nups]
            elif op == "VARARG":
                if b == 0:
                    top = a
                    define = 1 << a
                else:
                    define = span(a, a + b - 2)
            self.uses[pc], self.defs[pc] = use, define
            self.succs[pc] = [target for target in succ if target < count]
            pc += 1
        self.live_in, self.live_out = [0] * count, [0] * count
        changed = True
        while changed:
            changed = False
            for pc in range(count - 1, -1, -1):
                out = 0
                for target in self.succs[pc]:
                    out |= self.live_in[target]
                into = self.uses[pc] | (out & ~self.defs[pc])
                if out != self.live_out[pc] or into != self.live_in[pc]:
                    self.live_out[pc], self.live_in[pc] = out, into
                    changed = True

    # -- expression state ---------------------------------------------------
    def reg(self, index: int) -> str:
        if index < self.proto.numparams:
            return "self" if index == 0 and self.method else f"a{index}"
        return f"r{index}"

    def emit(self, text: str) -> None:
        self.lines.append("    " + text)

    def assign(self, index: int, text: str) -> None:
        self.emit(f"{self.reg(index)} = {text}")
        match = re.search(r'RequireScript\("([^"]+)"\)$', text)
        if match:
            self.hints[index] = match.group(1).rsplit(".", 1)[-1]
        elif len(text) <= 40:
            self.hints[index] = text
        else:
            self.hints.pop(index, None)

    def materialize(self, index: int) -> None:
        entry = self.pending.pop(index)
        self.release(index)
        self.assign(index, entry.text)

    def release(self, index: int) -> None:
        """Print each pending expression that reads the current value of a register."""
        for other in sorted(self.pending, key=lambda key: self.pending[key].sequence):
            if other in self.pending and (self.pending[other].deps >> index) & 1:
                self.materialize(other)

    def clobber(self, index: int) -> None:
        entry = self.pending.pop(index, None)
        if entry is not None and entry.impure:
            self.pending[index] = entry
            self.materialize(index)
        self.release(index)

    def flush(self, live: int) -> None:
        for index in sorted(self.pending, key=lambda key: self.pending[key].sequence):
            entry = self.pending.get(index)
            if entry is not None and (entry.impure or (live >> index) & 1):
                self.materialize(index)
        self.pending.clear()

    def flush_impure(self) -> None:
        for index in sorted(self.pending, key=lambda key: self.pending[key].sequence):
            entry = self.pending.get(index)
            if entry is not None and entry.impure:
                self.materialize(index)

    def use(self, index: int, pc: int, base: bool = False) -> str:
        entry = self.pending.get(index)
        if entry is None:
            self.deps |= 1 << index
            return self.reg(index)
        if entry.impure:
            if (self.live_out[pc] >> index) & 1 and not (self.defs[pc] >> index) & 1:
                self.materialize(index)
                self.deps |= 1 << index
                return self.reg(index)
            del self.pending[index]
        self.deps |= entry.deps
        return f"({entry.text})" if base and entry.compound else entry.text

    def define(self, index: int, text: str, impure=False, compound=False, selfobj=None) -> None:
        old = self.pending.pop(index, None)
        if old is not None and old.impure:
            self.pending[index] = old
            self.materialize(index)
        self.sequence += 1
        self.pending[index] = Entry(text, impure, compound, self.deps, selfobj, self.sequence)

    # -- generation ---------------------------------------------------------
    def generate(self) -> None:
        self.analyze()
        proto, code = self.proto, self.proto.code
        constants = proto.constants
        multi = None
        skip_return = False

        def rk(value, pc, base=False):
            return constants[value - 256] if value >= 256 else self.use(value, pc, base)

        def field(table, key):
            if key.startswith('"') and IDENT.match(key[1:-1]):
                return f"{table}.{key[1:-1]}"
            return f"{table}[{key}]"

        pc = 0
        while pc < len(code):
            if self.skip[pc]:
                pc += 1
                continue
            if pc in self.targets:
                self.flush(self.live_in[pc])
                self.lines.append(f"  ::L{pc}::")
            op, a, b, c, bx = decode(code[pc])
            sbx = bx - 131071
            self.deps = 0
            if op == "MOVE":
                text = self.use(b, pc)
                entry_compound = bool(re.search(r"[ ]", text)) and not text.endswith(")")
                self.define(a, text, compound=entry_compound)
            elif op == "LOADK":
                self.define(a, constants[bx])
            elif op == "LOADBOOL":
                self.define(a, "true" if b else "false")
                if c:
                    self.flush(self.live_in[pc + 2] if pc + 2 < len(code) else 0)
                    self.emit(f"goto L{pc + 2}")
            elif op == "LOADNIL":
                for index in range(a, b + 1):
                    self.define(index, "nil")
            elif op == "GETUPVAL":
                self.define(a, self.upvalues[b] if b < len(self.upvalues) else f"^U{b}")
            elif op == "GETGLOBAL":
                self.define(a, constants[bx][1:-1])
            elif op == "GETTABLE":
                table = self.use(b, pc, base=True)
                self.define(a, field(table, rk(c, pc)))
            elif op == "SETGLOBAL":
                value = self.use(a, pc)
                self.note_closure(value, constants[bx][1:-1], False)
                self.flush_impure()
                self.emit(f"{constants[bx][1:-1]} = {value}")
            elif op == "SETUPVAL":
                value = self.use(a, pc)
                self.flush_impure()
                self.emit(f"{self.upvalues[b] if b < len(self.upvalues) else f'^U{b}'} = {value}")
            elif op == "SETTABLE":
                table = self.use(a, pc, base=True)
                key = rk(b, pc)
                value = rk(c, pc)
                self.note_closure(value, field(table, key).replace(".", ":", 1) if "." in field(table, key)
                                  else field(table, key), True)
                self.flush_impure()
                self.emit(f"{field(table, key)} = {value}")
            elif op == "NEWTABLE":
                self.clobber(a)
                self.assign(a, "{}")
            elif op == "SELF":
                table = self.use(b, pc, base=True)
                key = rk(c, pc)
                name = key[1:-1] if key.startswith('"') else f"[{key}]"
                saved = self.deps
                self.define(a + 1, table)
                self.deps = saved
                self.define(a, f"{table}:{name}", selfobj=table)
            elif op in ARITH:
                left, right = rk(b, pc, True), rk(c, pc, True)
                self.define(a, f"{left} {ARITH[op]} {right}", compound=True)
            elif op in ("UNM", "NOT", "LEN"):
                operand = self.use(b, pc, base=True)
                self.define(a, {"UNM": "-", "NOT": "not ", "LEN": "#"}[op] + operand, compound=True)
            elif op == "CONCAT":
                parts = [self.use(index, pc, base=True) for index in range(b, c + 1)]
                self.define(a, " .. ".join(parts), compound=True)
            elif op == "JMP":
                target = pc + 1 + sbx
                self.flush(self.live_in[target] if target < len(code) else 0)
                self.emit(f"goto L{target}")
            elif op in ("EQ", "LT", "LE", "TEST", "TESTSET"):
                jump = decode(code[pc + 1])
                target = pc + 2 + (jump[4] - 131071)
                self.targets.add(target)
                if op == "TEST":
                    value = self.use(a, pc, base=True)
                    condition = value if c else f"not {value}"
                elif op == "TESTSET":
                    value = self.use(b, pc, base=True)
                    condition = value if c else f"not {value}"
                else:
                    left, right = rk(b, pc, True), rk(c, pc, True)
                    symbol = {"EQ": ("~=", "=="), "LT": (">=", "<"), "LE": (">", "<=")}[op][1 if a else 0]
                    condition = f"{left} {symbol} {right}"
                self.flush_impure()
                self.flush(self.live_out[pc])
                if op == "TESTSET":
                    self.emit(f"if {condition} then {self.reg(a)} = {value}; goto L{target} end")
                else:
                    self.emit(f"if {condition} then goto L{target} end")
                pc += 1
            elif op in ("CALL", "TAILCALL"):
                entry = self.pending.get(a)
                selfobj = entry.selfobj if entry is not None else None
                function = self.use(a, pc, base=True)
                first = a + 1
                if selfobj is not None:
                    self.pending.pop(a + 1, None)
                    first = a + 2
                last = a + b - 1 if b else (multi[0] - 1 if multi else a)
                arguments = [self.use(index, pc) for index in range(first, last + 1)]
                if b == 0 and multi:
                    arguments.append(multi[1])
                    multi = None
                text = f"{function}({', '.join(arguments)})"
                self.flush_impure()
                if op == "TAILCALL":
                    self.emit(f"return {text}")
                    skip_return = True
                elif c == 1:
                    self.emit(text)
                elif c == 2:
                    self.define(a, text, impure=True)
                elif c == 0:
                    multi = (a, text + " --[[all results]]")
                else:
                    for index in range(a, a + c - 1):
                        self.clobber(index)
                    self.emit(f"{', '.join(self.reg(index) for index in range(a, a + c - 1))} = {text}")
            elif op == "RETURN":
                if skip_return:
                    skip_return = False
                else:
                    last = a + b - 2 if b else (multi[0] - 1 if multi else a - 1)
                    values = [self.use(index, pc) for index in range(a, last + 1)]
                    if b == 0 and multi:
                        values.append(multi[1])
                        multi = None
                    self.flush_impure()
                    if pc != len(code) - 1 or values:
                        self.emit(("return " + ", ".join(values)).rstrip())
            elif op == "FORPREP":
                values = [self.use(index, pc) for index in range(a, a + 3)]
                self.flush_impure()
                self.flush(self.live_out[pc])
                self.emit(f"for {self.reg(a + 3)} = {values[0]}, {values[1]}, {values[2]} do  -- test at L{pc + 1 + sbx}")
            elif op == "FORLOOP":
                self.flush(self.live_out[pc])
                self.emit(f"end  -- numeric for {self.reg(a + 3)}: loop to L{pc + 1 + sbx}")
            elif op == "TFORLOOP":
                self.flush(self.live_out[pc])
                names = ", ".join(self.reg(index) for index in range(a + 3, a + 3 + c))
                self.emit(f"{names} = {self.reg(a)}({self.reg(a + 1)}, {self.reg(a + 2)})"
                          f"  -- generic for; exit when {self.reg(a + 3)} == nil, else:")
            elif op == "SETLIST":
                last = a + b if b else (multi[0] - 1 if multi else a)
                values = [self.use(index, pc) for index in range(a + 1, last + 1)]
                if b == 0 and multi:
                    values.append(multi[1])
                    multi = None
                self.flush_impure()
                block = c if c else code[pc + 1]
                self.emit(f"{self.reg(a)}[{(block - 1) * 50 + 1}...] = {{{', '.join(values)}}}")
            elif op == "CLOSURE":
                child = proto.protos[bx]
                names = []
                for index in range(child.nups):
                    pseudo = decode(code[pc + 1 + index])
                    if pseudo[0] == "MOVE":
                        source = pseudo[2]
                        entry = self.pending.get(source)
                        if entry is not None and entry.impure:
                            self.materialize(source)
                            entry = None
                        if entry is not None and len(entry.text) <= 48:
                            names.append("^" + entry.text)
                        else:
                            if entry is not None:
                                self.materialize(source)
                            hint = self.hints.get(source)
                            names.append(f"^{self.reg(source)}<{hint}>" if hint and hint != "{}"
                                         else f"^{self.ident}.{self.reg(source)}")
                    else:
                        names.append(self.upvalues[pseudo[2]] if pseudo[2] < len(self.upvalues) else f"^U{pseudo[2]}")
                self.children[bx] = (f"function<{self.ident}.{bx}>", False, names)
                self.define(a, f"function<{self.ident}.{bx}>")
            elif op == "VARARG":
                if b == 0:
                    multi = (a, "...")
                else:
                    for index in range(a, a + b - 1):
                        self.clobber(index)
                    self.emit(f"{', '.join(self.reg(index) for index in range(a, a + b - 1))} = ...")
            pc += 1

    def note_closure(self, value: str, name: str, method: bool) -> None:
        match = re.match(r"^function<.*\.(\d+)>$", value)
        if match and int(match.group(1)) in self.children:
            index = int(match.group(1))
            self.children[index] = (name, method, self.children[index][2])


def render(proto: Proto, ident: str, name: str, upvalues: list[str], method: bool, output: list[str]) -> None:
    function = Function(proto, ident, name, upvalues, method)
    function.generate()
    parameters = [function.reg(index) for index in range(proto.numparams)] + (["..."] if proto.vararg & 2 else [])
    output.append(f"function {name}({', '.join(parameters)})  -- <{ident}>")
    output.extend(function.lines)
    output.append("end")
    output.append("")
    for index, child in enumerate(proto.protos):
        child_name, child_method, child_upvalues = function.children.get(
            index, (f"function<{ident}.{index}>", False, [f"^U{i}" for i in range(child.nups)]))
        render(child, f"{ident}.{index}", child_name, child_upvalues, child_method, output)


def main() -> None:
    for argument in sys.argv[1:]:
        data = Path(argument).read_bytes()
        start = data.find(b"\x1bLua")
        if start < 0 or data[start + 4] != 0x51:
            print(f"{argument}: not Lua 5.1 bytecode")
            continue
        reader = Reader(data)
        reader.position = start + 12
        output: list[str] = []
        render(read_function(reader), "0", "main", [], False, output)
        target = Path(argument).with_suffix(".lua.txt")
        target.write_text("\n".join(output) + "\n", encoding="utf-8")
        print(f"{target} ({len(output)} lines)")


if __name__ == "__main__":
    main()
