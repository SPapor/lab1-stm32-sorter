"""Тести tools/stack_graph.py на шматках дизасемблера у форматі arm-none-eabi-objdump -d.

    python3 -m unittest discover -s tools/tests
"""
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

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
 8000112:\te7fd      \tb.n\t8000110 <leaf>
 8000114:\tbd10      \tpop\t{r4, pc}

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

# збірка PERF=0: meas_write немає, драйвер кличе hal_write; xfer розщеплений GCC
DIS_PERF0 = """
08000200 <write_port>:
 8000200:\tb510      \tpush\t{r4, lr}
 8000202:\t47a0      \tblx\tr4

08000210 <hal_write>:
 8000210:\tb510      \tpush\t{r4, lr}
 8000212:\tf000 f805 \tbl\t8000220 <xfer.part.0>

08000220 <xfer.part.0>:
 8000220:\tb5f0      \tpush\t{r4, r5, r6, r7, lr}
 8000222:\t47a8      \tblx\tr5
 8000224:\t4798      \tblx\tr3

08000230 <HAL_I2C_Master_Transmit>:
 8000230:\tb570      \tpush\t{r4, r5, r6, lr}
 8000232:\tb082      \tsub\tsp, #8

08000240 <HAL_I2C_Master_Receive>:
 8000240:\tb510      \tpush\t{r4, lr}

08000250 <MX_I2C1_Init>:
 8000250:\tb510      \tpush\t{r4, lr}

08000260 <odd>:
 8000260:\tb510      \tpush\t{r4, lr}
 8000262:\t4798      \tblx\tr3
 8000264:\tf7ff ffcc \tbl\t8000200 <write_port>
"""

DIS_CYCLE = """
08000300 <a>:
 8000300:\tb500      \tpush\t{lr}
 8000302:\tf000 f805 \tbl\t8000310 <b>

08000310 <b>:
 8000310:\tb510      \tpush\t{r4, lr}
 8000312:\tf7ff fff5 \tbl\t8000300 <a>
"""


def load(text):
    funcs = sg.parse(io.StringIO(text))
    unknown, missing = sg.add_indirect(funcs)
    return funcs, unknown, missing


def run_main(text, *args):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'fw.dis')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            sg.main([path, *args])
    return out.getvalue(), err.getvalue()


class StackGraphTest(unittest.TestCase):
    def setUp(self):
        self.funcs, self.unknown, self.missing = load(DIS)

    def test_frame_push_and_sub(self):
        self.assertEqual(self.funcs['main']['frame'], 5 * 4 + 44)
        self.assertEqual(self.funcs['deep']['frame'], 4 + 16)
        self.assertEqual(self.funcs['HAL_IncTick']['frame'], 0)

    def test_calls_skip_branches_inside_function(self):
        self.assertEqual(self.funcs['main']['calls'], {'leaf', 'deep'})

    def test_loop_to_own_start_is_not_recursion(self):
        self.assertEqual(self.funcs['leaf']['calls'], set())
        self.assertIn('deep', self.funcs['deep']['calls'])      # bl на себе - рекурсія

    def test_indirect_edge_added(self):
        self.assertIn('meas_write', self.funcs['write_port']['calls'])
        self.assertNotIn('hal_write', self.funcs['write_port']['calls'])
        self.assertEqual((self.unknown, self.missing), ([], []))

    def test_deepest_chain_stops_on_recursion(self):
        total, chain = sg.deepest(self.funcs, 'main')
        self.assertEqual([n for n, _ in chain], ['main', 'deep', 'write_port', 'meas_write'])
        self.assertEqual(total, 64 + 20 + 8 + 16)

    def test_chain_keeps_leaf_with_zero_frame(self):
        total, chain = sg.deepest(self.funcs, 'SysTick_Handler')
        self.assertEqual(total, 8)
        self.assertEqual(chain, [('SysTick_Handler', 8), ('HAL_IncTick', 0)])

    def test_main_adds_exception_frames_with_alignment(self):
        out, _ = run_main(DIS, '--isr', 'SysTick_Handler')
        self.assertIn('main: 108 B', out)
        self.assertIn('SysTick_Handler: 8 B + 32 B кадр винятку + 4 B вирівнювання', out)
        self.assertIn('108 + 44 = 152 B', out)

    def test_unknown_isr_is_an_error(self):
        with self.assertRaises(SystemExit) as e:
            run_main(DIS, '--isr', 'EXTI4_15_IRQHandle')
        self.assertIn('EXTI4_15_IRQHandle', str(e.exception.code))


class VariantTest(unittest.TestCase):
    def test_perf0_driver_goes_to_hal_write(self):
        funcs, _, _ = load(DIS_PERF0)
        self.assertEqual(funcs['write_port']['calls'], {'hal_write'})

    def test_gcc_suffix_matches_indirect_entry(self):
        funcs, _, missing = load(DIS_PERF0)
        self.assertTrue({'HAL_I2C_Master_Transmit', 'HAL_I2C_Master_Receive', 'MX_I2C1_Init'}
                        <= funcs['xfer.part.0']['calls'])
        self.assertEqual(missing, [])
        total, chain = sg.deepest(funcs, 'write_port')
        self.assertEqual([n for n, _ in chain],
                         ['write_port', 'hal_write', 'xfer.part.0', 'HAL_I2C_Master_Transmit'])
        self.assertEqual(total, 8 + 8 + 20 + 24)

    def test_unlisted_blx_is_reported(self):
        _, unknown, _ = load(DIS_PERF0)
        self.assertEqual(unknown, ['odd'])
        _, err = run_main(DIS_PERF0, '--root', 'odd', '--isr', 'MX_I2C1_Init')
        self.assertIn('odd', err)
        self.assertIn('INDIRECT', err)

    def test_mutual_recursion_counts_each_function_once(self):
        funcs, _, _ = load(DIS_CYCLE)
        total, chain = sg.deepest(funcs, 'a')
        self.assertEqual(total, 4 + 8)
        self.assertEqual(sorted(n for n, _ in chain), ['a', 'b'])
        _, err = run_main(DIS_CYCLE, '--root', 'a', '--isr', 'b')
        self.assertIn('рекурсія a, b', err)


if __name__ == '__main__':
    unittest.main()
