"""Тести tools/stack_graph.py на шматку дизасемблера у форматі arm-none-eabi-objdump -d.

    python3 -m unittest discover -s tools/tests
"""
import io
import os
import sys
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import stack_graph as sg  # noqa: E402

DIS = """
08000100 <main>:
 8000100:\tb5f0      \tpush\t{r4, r5, r6, r7, lr}
 8000102:\tb08b      \tsub\tsp, #44\t; 0x2c
 8000104:\tf000 f804 \tbl\t8000110 <leaf>
 8000108:\tf000 f808 \tbl\t8000120 <deep>
 800010c:\te7fa      \tb.n\t8000104 <main+0x4>

08000110 <leaf>:
 8000110:\tb510      \tpush\t{r4, lr}
 8000112:\tbd10      \tpop\t{r4, pc}

08000120 <deep>:
 8000120:\tb500      \tpush\t{lr}
 8000122:\tb084      \tsub\tsp, #16
 8000124:\tf000 f804 \tbl\t8000130 <write_port>
 8000128:\tf7ff fffa \tbl\t8000120 <deep>

08000130 <write_port>:
 8000130:\tb510      \tpush\t{r4, lr}
 8000132:\t4798      \tblx\tr3

08000140 <meas_write>:
 8000140:\tb570      \tpush\t{r4, r5, r6, lr}

08000150 <SysTick_Handler>:
 8000150:\tb510      \tpush\t{r4, lr}
 8000152:\tf000 f805 \tbl\t8000160 <HAL_IncTick>

08000160 <HAL_IncTick>:
 8000160:\t4a02      \tldr\tr2, [pc, #8]\t; (800016c <HAL_IncTick+0xc>)
"""


class StackGraphTest(unittest.TestCase):
    def setUp(self):
        self.funcs = sg.parse(io.StringIO(DIS))

    def test_frame_push_and_sub(self):
        self.assertEqual(self.funcs['main']['frame'], 5 * 4 + 44)
        self.assertEqual(self.funcs['deep']['frame'], 4 + 16)
        self.assertEqual(self.funcs['HAL_IncTick']['frame'], 0)

    def test_calls_skip_branches_inside_function(self):
        self.assertEqual(self.funcs['main']['calls'], {'leaf', 'deep'})

    def test_indirect_edge_added(self):
        self.assertIn('meas_write', self.funcs['write_port']['calls'])

    def test_deepest_chain_stops_on_recursion(self):
        total, chain = sg.deepest(self.funcs, 'main')
        self.assertEqual([n for n, _ in chain], ['main', 'deep', 'write_port', 'meas_write'])
        self.assertEqual(total, 64 + 20 + 8 + 16)

    def test_main_adds_exception_frames(self):
        path = os.path.join(os.path.dirname(__file__), '_stack_graph.dis')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(DIS)
        try:
            out = io.StringIO()
            with redirect_stdout(out):
                sg.main([path, '--isr', 'SysTick_Handler'])
        finally:
            os.remove(path)
        self.assertIn('main: 108 B', out.getvalue())
        self.assertIn('SysTick_Handler: 8 B + 32 B', out.getvalue())
        self.assertIn('108 + 40 = 148 B', out.getvalue())


if __name__ == '__main__':
    unittest.main()
