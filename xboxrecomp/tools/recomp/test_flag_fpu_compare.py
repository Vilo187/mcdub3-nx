"""fcomi/fucomip flags: the conditions after them follow the hardware's
EFLAGS, unordered (a NaN) included -- ZF=PF=CF=1, so jb/jbe/je/jp jump and
ja/jae/jne/jnp do not (conformance fpu_*comi*; MC3 Remix, 2026-10-25)."""
from tools.recomp import config
from tools.recomp.translator import FunctionTranslator

BASE = 0x10000


def translate(image):
    config._install([config.Section('.text', BASE, len(image), 0, len(image), True)],
                    entry_point=BASE, kernel_thunk_addr=BASE,
                    origin='fpu-compare-test')
    db = {BASE: {'start': hex(BASE), 'end': BASE + len(image),
                 '_addr': BASE, 'size': len(image)}}
    return FunctionTranslator(image, db).translate_function(BASE, db[BASE])


def test_conditions_after_fucomip():
    want = {
        '77': 'g_fp_cmp == 1',                          # ja
        '73': '(g_fp_cmp == 1 || g_fp_cmp == 0)',       # jae
        '72': '(g_fp_cmp == -1 || g_fp_cmp == 2)',      # jb
        '76': 'g_fp_cmp != 1',                          # jbe
        '74': '(g_fp_cmp == 0 || g_fp_cmp == 2)',       # je
        '75': '(g_fp_cmp == 1 || g_fp_cmp == -1)',      # jne
        '7a': 'g_fp_cmp == 2',                          # jp
        '7b': 'g_fp_cmp != 2',                          # jnp
    }
    for op, cond in want.items():
        # fucomip st(0), st(1); jcc +1; nop; ret
        code = translate(bytes.fromhex('dfe9' + op + '01' '90' 'c3'))
        assert cond in code, (op, code)
