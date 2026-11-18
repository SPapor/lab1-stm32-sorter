"""docs/port_sim-V11.3.3.patch відповідає копії порту в Platform/rtos/port_sim/.

Патч - те, що у звіті показує рівно відмінності обходу SIM_CRIT від офіційного
порту V11.3.3; тест не дає йому розійтися з кодом.

    python3 -m unittest discover -s tools/tests
"""
import os
import shutil
import subprocess
import unittest

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
FW = os.path.join(ROOT, 'firmware')
PORT = 'Middlewares/FreeRTOS/portable/GCC/ARM_CM0'
SIM = 'Platform/rtos/port_sim'
FILES = ('port.c', 'portasm.c', 'portasm.h')
PATCH = os.path.join(ROOT, 'docs', 'port_sim-V11.3.3.patch')


def current_diff():
    out = []
    for f in FILES:
        r = subprocess.run(['diff', '-u', '--label', f'{PORT}/{f}', '--label', f'{SIM}/{f}',
                            f'{PORT}/{f}', f'{SIM}/{f}'], cwd=FW, capture_output=True, text=True)
        out.append(r.stdout)
    return ''.join(out)


@unittest.skipUnless(os.path.exists(os.path.join(FW, PORT, 'port.c')) and shutil.which('diff'),
                     'немає сабмодуля FreeRTOS або diff')
class PortSimPatchTest(unittest.TestCase):
    def test_patch_matches_port_sim(self):
        with open(PATCH, encoding='utf-8') as f:
            text = f.read()
        body = text[text.index('--- '):]
        self.assertEqual(body, current_diff(),
                         'патч застарів: перегенеруй його (заголовок файлу - docs/port_sim-V11.3.3.patch)')


if __name__ == '__main__':
    unittest.main()
