#!/usr/bin/env python3
"""Генерує файли дій wokwi-cli для сценаріїв кроку 5: S2, S3, impact, impact-20ms і 6.2.7-fault.

    python3 tools/wokwi/gen_actions.py [--out tools/wokwi]

Кожен файл - кроки set-control і delay за часом симуляції між маркерами
SET name=go і SET name=end (docs/scenarios.md, «Прогін без вікна»). Паузи між
натисканнями B1 у S3 - random з seed 33, тож файл завжди той самий.
S1, 6.2.2 і 6.2.4 зроблені ще в кроці 4 окремо і тут не генеруються.
"""
import argparse
import os
import random

HEAD = ('# Дії для tools/run_scenario.py --wokwi (wokwi-cli, час симуляції).\n'
        '# Старт - на рядку SET name=go (скрипт шле set go 0 після setup), кінець - SET name=end.\n')
GO = '  - wait-serial: "SET name=go st=BAD\\r\\n"\n'
END = '  - wait-serial: "SET name=end st=BAD\\r\\n"\n'
CLASS = [500, 100, 10]          # lux class-LDR: A, UNKNOWN, B


def ctl(part, control, value):
    return f'  - set-control: {{part-id: {part}, control: {control}, value: {value}}}\n'


def timeline(events):
    """events: [(t_мс, рядок set-control)] -> кроки з delay між ними, кінець, мс."""
    out, t = [], 0
    for at, line in sorted(events, key=lambda e: e[0]):
        if at > t:
            out.append(f'  - delay: {at - t}ms\n')
            t = at
        out.append(line)
    return ''.join(out), t


def parcels(n, period, start, dark, events):
    """n посилок: class по колу за 0,5 с до посилки, gate 10 лк на dark мс."""
    for i in range(n):
        t0 = start + i * period
        events.append((t0 - 500, ctl('ldr_class', 'lux', CLASS[i % 3])))
        events.append((t0, ctl('ldr_gate', 'lux', 10)))
        events.append((t0 + dark, ctl('ldr_gate', 'lux', 500)))


def impacts(n, period, start, before, width, every, events):
    """n посилок класу A; перед кожною every-ю - удар X = 2 g на width мс за before мс до неї."""
    events += [(0, ctl('ldr_class', 'lux', 500)), (0, ctl('imu', 'accelX', 0)),
               (0, ctl('imu', 'accelZ', 1))]
    for i in range(n):
        t0 = start + i * period
        if i % every == 0:
            events.append((t0 - before, ctl('imu', 'accelX', 2)))
            events.append((t0 - before + width, ctl('imu', 'accelX', 0)))
        events.append((t0, ctl('ldr_gate', 'lux', 10)))
        events.append((t0 + 1000, ctl('ldr_gate', 'lux', 500)))


def text(comment, name, body):
    return HEAD + comment + '\n' + f'name: "{name}"\nversion: 1\nsteps:\n' + GO + body + END


def generate():
    files = {}

    ev = []
    parcels(30, 3000, 1000, 1000, ev)
    files['S2.yaml'] = text(
        '# S2: 30 посилок по 3 с - class по колу 500/100/10 лк, gate 10 лк на 1 с (~90 с).',
        'S2 30 посилок', timeline(ev)[0])

    ev = []
    parcels(15, 4000, 1000, 1200, ev)
    rnd = random.Random(33)
    t = 1300
    for i in range(30):
        hold = 150 if i % 2 == 0 else 450
        ev.append((t, ctl('estop', 'pressed', 1)))
        ev.append((t + hold, ctl('estop', 'pressed', 0)))
        t += 150 + rnd.randint(1200, 2200)
    files['S3.yaml'] = text(
        '# S3: 60 с - посилка кожні 4 с (gate 10 лк на 1,2 с, class по колу), B1 30 разів:\n'
        '# по черзі 150 мс і 450 мс (довше за найдовше блокування циклу ~196 мс), паузи між\n'
        '# натисканнями 1,2-2,2 с від початку до початку плюс 150 мс (random, seed 33).',
        'S3 найгірший', timeline(ev)[0])

    ev = []
    impacts(6, 4000, 1500, 800, 300, 2, ev)
    files['impact.yaml'] = text(
        '# impact (FR-04, FR-05): 6 посилок класу A раз на 4 с; перед 1-ю, 3-ю і 5-ю удар\n'
        '# X = 2 g при Z = 1 g на 0,3 с, |a| = 2,24 g (по одній осі в Wokwi більше ~2 g немає,\n'
        '# журнал, проблема 16). Посилки з ударом мають піти в брак, без удару - в A.',
        'impact удар', timeline(ev)[0])

    ev = []
    impacts(10, 3000, 1500, 500, 20, 1, ev)
    files['impact-20ms.yaml'] = text(
        '# impact-20ms (FR-04 під навантаженням): 10 посилок класу A раз на 3 с, за 0,5 с до\n'
        '# кожної - удар X = 2 g при Z = 1 g лише на 20 мс. Скрипт тим часом шле stats двічі\n'
        '# на секунду, як у S3, і цикл стоїть до ~200 мс: скільки ударів побачить imu_task.',
        'impact-20ms удар 20 мс', timeline(ev)[0])

    files['6.2.7-fault.yaml'] = text(
        '# 6.2.7-fault (FR-08): старт без MPU-6050 (--drop-part imu). Дій немає, лише маркери.',
        '6.2.7 без MPU', '')
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', default=os.path.dirname(os.path.abspath(__file__)))
    args = ap.parse_args()
    for name, body in generate().items():
        with open(os.path.join(args.out, name), 'w', encoding='utf-8') as f:
            f.write(body)
        print(os.path.join(args.out, name))


if __name__ == '__main__':
    main()
