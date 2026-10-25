"""Put RECOMP_LOOP_BARRIER() on every backward goto of a lifted function.

Guest memory is volatile by default (recomp_types.h, RECOMP_MEM_VOLATILE),
which keeps every load and store but also every redundant one. Built with
plain accesses instead, the compiler may keep a loaded word in a register --
right for straight-line code, wrong for a loop that waits for another
thread or the hardware to change that word. spin_hint.py marks the pure poll
loops; loops that also store are left alone there, like D3D's vblank wait:

    loc_003E5080: ;
        MEM32(edi + 0x600100) = ecx;
        _fa = (uint32_t)(MEM32(edi + 0x100)) ...
        if (TEST_NZ(_fa, _fb)) goto loc_003E5080;

(the store and the load have the same base and different offsets, so the
compiler may hoist the load: the loop would never see the runtime clear the
bit). A compiler barrier on every back edge keeps every loop re-reading
memory each turn, as the x86 code does, and leaves straight-line code free.
The macro is empty in volatile builds: the default output behaves exactly as
before.

Works on the C text, like spin_hint and mmio_rewrite.
"""

import re

_LABEL = re.compile(r"^\s*(loc_[0-9A-Fa-f]+): ;")
_GOTO_ANY = re.compile(r"goto (loc_[0-9A-Fa-f]+);")


def rewrite(code):
    """Return (code, number of back edges marked)."""
    seen = set()
    out = []
    marked = 0
    for line in code.split("\n"):
        m = _LABEL.match(line)
        if m:
            seen.add(m.group(1))
        if "goto loc_" in line and "RECOMP_SPIN_HINT" not in line:
            def mark(g):
                nonlocal marked
                if g.group(1) not in seen:
                    return g.group(0)           # forward: no loop
                marked += 1
                return "{ RECOMP_LOOP_BARRIER(); " + g.group(0) + " }"
            line = _GOTO_ANY.sub(mark, line)
        out.append(line)
    return "\n".join(out), marked
