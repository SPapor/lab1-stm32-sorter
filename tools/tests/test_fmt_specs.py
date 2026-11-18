"""Рядки форматів прошивки - лише те, що вміє форматер App/fmt.c.

Атрибут format(printf) у функцій виводу ловить невідповідність типів, але GCC
пропускає специфікатори, яких fmt не знає (%zu, %llu, %.2f): fmt надрукує їх
як текст і не забере аргумент, і всі наступні значення в рядку з'їдуть.

    python3 -m unittest discover -s tools/tests
"""
import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(__file__), '..', '..', 'firmware')
DIRS = ('App', 'Devices', 'Platform', 'Tasks')
FILES = ('Core/Src/main.c', 'Core/Src/stm32c0xx_it.c')
# Копія порту FreeRTOS: там %0, %1 - операнди asm, а не формати
SKIP = (os.path.join('Platform', 'rtos', 'port_sim'),)

# Що вміє fmt_vbuf: прапорці '-' і '0', ширина, модифікатор l, d i u x X c s, %%
SUPPORTED = re.compile(r'%(?:%|[-0]*[0-9]*l?[diuxXcs])')
CONVERSION = re.compile(r'%[-+ #0]*[0-9*]*(?:\.[0-9*]*)?(?:hh|h|ll|l|z|j|t|L)?[a-zA-Z%]?')
STRING = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
# Вставки asm: там %0, %1 - операнди, а не формати
ASM = re.compile(r'__asm(?:__)?\s*(?:volatile|__volatile__)?\s*\(.*?\)\s*;', re.S)


def sources():
    for d in DIRS:
        for base, _, names in os.walk(os.path.join(ROOT, d)):
            if os.path.relpath(base, ROOT).startswith(SKIP):
                continue
            for n in names:
                if n.endswith(('.c', '.h')):
                    yield os.path.join(base, n)
    for f in FILES:
        yield os.path.join(ROOT, f)


def bad_specs(text):
    """Специфікатори в рядкових літералах (поза коментарями), яких fmt не вміє"""
    blank = lambda m: re.sub(r'[^\n]', ' ', m.group(0))  # noqa: E731
    code = ASM.sub(blank, COMMENT.sub(blank, text))
    found = []
    for m in STRING.finditer(code):
        for spec in CONVERSION.finditer(m.group(1)):
            if not SUPPORTED.fullmatch(spec.group(0)):
                line = code.count('\n', 0, m.start()) + 1
                found.append((line, spec.group(0)))
    return found


class FmtSpecsTest(unittest.TestCase):
    def test_firmware_formats_supported(self):
        bad = []
        for path in sources():
            with open(path, encoding='utf-8') as f:
                for line, spec in bad_specs(f.read()):
                    bad.append(f'{os.path.relpath(path, ROOT)}:{line}: {spec}')
        self.assertEqual(bad, [], 'специфікатори, яких немає у App/fmt.c')

    def test_checker_catches_unsupported(self):
        self.assertEqual(bad_specs('x("n=%zu v=%.2f t=%llu ok=%08lX %%");'),
                         [(1, '%zu'), (1, '%.2f'), (1, '%llu')])
        self.assertEqual(bad_specs('/* "%zu" */ x("%-5lu %c %s");'), [])
        self.assertEqual(bad_specs('__asm volatile ("mrs %0, psp" : "=r" (x));'), [])


if __name__ == '__main__':
    unittest.main()
