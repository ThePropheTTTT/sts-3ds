"""Instruction-level walk over JVM method bytecode.

Only lengths matter here: once every instruction boundary is known, a `getstatic`
inside an operand is no longer mistaken for a real one, which is what makes the
card-argument scan in sts_assets reliable.
"""

from __future__ import annotations

# opcode -> total instruction length in bytes, for the fixed-width opcodes
_OPLEN = [1] * 256
for _op in (0x10, 0x12, 0x15, 0x16, 0x17, 0x18, 0x19,
            0x36, 0x37, 0x38, 0x39, 0x3A, 0xA9, 0xBC):
    _OPLEN[_op] = 2
for _op in (0x11, 0x13, 0x14, 0x84, 0xBB, 0xBD, 0xC0, 0xC1, 0xC6, 0xC7,
            0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8):
    _OPLEN[_op] = 3
for _op in range(0x99, 0xA9):  # if*, goto, jsr
    _OPLEN[_op] = 3
_OPLEN[0xB9] = 5   # invokeinterface
_OPLEN[0xBA] = 5   # invokedynamic
_OPLEN[0xC5] = 4   # multianewarray
_OPLEN[0xC8] = 5   # goto_w
_OPLEN[0xC9] = 5   # jsr_w
del _op

# variable-length opcodes, resolved in _var_len
_TABLESWITCH = 0xAA
_LOOKUPSWITCH = 0xAB
_WIDE = 0xC4


def _var_len(code, off, op):
    if op == _WIDE:
        if off + 1 >= len(code):
            raise ValueError("truncated wide instruction")
        # wide <opcode> <index16>  or  wide iinc <index16> <const16>
        return 6 if code[off + 1] == 0x84 else 4
    pad = (4 - (off + 1) % 4) % 4
    base = off + 1 + pad
    if op == _TABLESWITCH:
        if base + 12 > len(code):
            raise ValueError("truncated tableswitch")
        low = int.from_bytes(code[base + 4:base + 8], "big", signed=True)
        high = int.from_bytes(code[base + 8:base + 12], "big", signed=True)
        if high < low:
            raise ValueError(f"bad tableswitch range {low}..{high}")
        return 1 + pad + 12 + 4 * (high - low + 1)
    if op == _LOOKUPSWITCH:
        if base + 8 > len(code):
            raise ValueError("truncated lookupswitch")
        npairs = int.from_bytes(code[base + 4:base + 8], "big", signed=True)
        if npairs < 0:
            raise ValueError(f"bad lookupswitch npairs {npairs}")
        return 1 + pad + 8 + 8 * npairs
    raise ValueError(f"not a variable-length opcode: {op:#x}")


def iter_insns(code):
    """Yield (offset, opcode, operand_bytes) for every instruction."""
    i = 0
    n = len(code)
    while i < n:
        op = code[i]
        if op in (_TABLESWITCH, _LOOKUPSWITCH, _WIDE):
            ln = _var_len(code, i, op)
        else:
            ln = _OPLEN[op]
        if ln <= 0 or i + ln > n:
            raise ValueError(f"instruction at {i} runs past the end of the code")
        yield i, op, code[i + 1:i + ln]
        i += ln


def u1(operand):
    return operand[0] if operand else 0


def u2(operand):
    if len(operand) < 2:
        return 0
    return (operand[0] << 8) | operand[1]


def push_int(op, operand):
    """Value pushed by an immediate int constant, or None.

    Note iconst_m1 (0x02) is the X-cost marker in Slay the Spire, so -1 is a
    legitimate cost and must not be treated as "no value".
    """
    if 0x02 <= op <= 0x08:
        return op - 0x03
    if op == 0x10 and operand:
        return operand[0] - 256 if operand[0] > 127 else operand[0]
    if op == 0x11 and len(operand) >= 2:
        v = (operand[0] << 8) | operand[1]
        return v - 65536 if v > 32767 else v
    return None
