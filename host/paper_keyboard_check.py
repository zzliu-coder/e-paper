"""Non-secret search keyboard regression; cancels without changing book content.
Keep the device untouched during a run. Requires an already-running owner.
"""
import argparse
import json
import time
from pathlib import Path
from paper_action import action, query


def wait_frame(before):
    start = time.monotonic()
    while time.monotonic() - start < 60:
        state = query('paper.status')
        assert state['boot_id'] == before['boot_id'], 'Device restarted'
        if (state['performance']['completed'] > before['performance']['completed']
                and state['app']['revision'] == state['app']['presented']):
            assert not state['last_action_error'], state['last_action_error']
            return state
        time.sleep(.1)
    raise TimeoutError('Outcome unknown; do not replay input')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--rapid', action='store_true')
    p.add_argument('--reference', type=Path)
    args = p.parse_args()
    if args.rapid and not args.reference:
        p.error('--rapid requires --reference to verify the entire input batch')
    with args.out.open('x') as log:
        def save(label, result):
            log.write(json.dumps(dict(label=label, result=result), ensure_ascii=False) + '\n')
            log.flush()
            print(label, round(result.get('seconds', 0), 3), flush=True)
        def run(name, value=''):
            result = action(name, value)
            save(name + ':' + value, result)
            return result['receipt']
        run('search'); before = run('mode', '1')
        start = time.monotonic()
        if args.rapid:
            for x, y in ((261,468),(304,468),(132,468),(154,536),(304,468)):
                query('paper.tap', x=x, y=y)
            # The first batch may finish while the rest remain queued.
            expected = None
            if args.reference:
                rows = [json.loads(line) for line in args.reference.read_text().splitlines()]
                expected = next(r['result']['app']['frame_crc'] for r in rows if r['label'] == 'candidate-frame')
            while True:
                final = wait_frame(before)
                if expected is None or final['app']['frame_crc'] == expected:
                    break
                if time.monotonic() - start > 60:
                    raise AssertionError('Candidates differ from sequential reference')
                before = final
        else:
            for key in 'yuedu': final = run('key', key)
        save('typing', dict(seconds=time.monotonic()-start))
        save('candidate-frame', final)
        query('paper.tap', x=75, y=270)
        chosen = wait_frame(final)
        save('chosen-frame', chosen)
        if args.reference:
            rows = [json.loads(line) for line in args.reference.read_text().splitlines()]
            expected = next(r['result']['app']['frame_crc'] for r in rows if r['label'] == 'chosen-frame')
            assert chosen['app']['frame_crc'] == expected, 'Chosen word differs'
        run('input-cancel')


if __name__ == '__main__':
    main()
