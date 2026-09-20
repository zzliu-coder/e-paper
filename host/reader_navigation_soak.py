"""Bounded next/back soak on the currently open book; no flashing or resource writes."""
import argparse
import json
import time
from pathlib import Path
from paper_action import action as verified_action, query


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cycles', type=int, default=20)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 100:
        parser.error('cycles must be 1..100')

    initial = query('paper.status')
    if initial['app']['screen'] != 2:
        raise RuntimeError('Open the chosen test book before running')
    boot = initial['boot_id']
    position = initial['app']['reader']
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x') as log:
        log.write(json.dumps({'initial': initial}, ensure_ascii=False) + '\n')
        for cycle in range(args.cycles):
            for action in ('next', 'prev'):
                result = verified_action(action, timeout=300)
                current = result['receipt']
                if current['boot_id'] != boot:raise RuntimeError('Device restarted')
                if current['app'].get('reading_content',{}).get('image_error'):raise RuntimeError(current)
                log.write(json.dumps({'cycle':cycle,'action':action,**result},ensure_ascii=False)+'\n')
                log.flush()
            end = current['app']['reader']
            if tuple(end.get(k) for k in ('engine','chapter','offset','page_hint')) != tuple(position.get(k) for k in ('engine','chapter','offset','page_hint')):
                raise RuntimeError('Next/back did not restore original location')
        result = {'result': 'PASS', 'cycles': args.cycles, 'actions': args.cycles * 2,
                  'boot_id': boot, 'physical_ghosting': 'NOT_PROVEN'}
        log.write(json.dumps(result) + '\n')
        print(json.dumps(result))


if __name__ == '__main__':
    main()
