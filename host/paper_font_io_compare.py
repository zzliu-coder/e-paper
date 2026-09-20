"""ABBA comparison of scoped glyph I/O using the existing USB owner service.

I/O/verification modes are in-memory. Header comparison writes its separate
display preference record and restores the original on exit unless a successful
run explicitly requests --keep-candidate. Reading position is saved normally.
Does not flash, change fonts, scan networks, record audio or read credentials.
"""
import argparse
import json
import statistics
import time
import uuid
from contextlib import ExitStack
from pathlib import Path
from service import call, DEFAULT_SOCKET


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--pages', type=int, default=10)
    p.add_argument('--book-index', type=int, default=0)
    p.add_argument('--experiment', choices=('io','verify','header'), default='io')
    p.add_argument('--keep-candidate',action='store_true',help='Keep compact header after a successful header comparison')
    a = p.parse_args()
    if not 1 <= a.pages <= 50:
        p.error('pages must be 1..50')
    if a.book_index < 0:
        p.error('book-index must be non-negative')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    samples = []
    command = {'verify':'font-verify-mode','io':'font-io-mode','header':'header-mode'}[a.experiment]
    candidate = {'verify':'single','io':'batch','header':'compact'}[a.experiment]
    baseline = 'book' if a.experiment == 'header' else 'legacy'
    with a.out.open('x') as log, ExitStack() as cleanup:
        def query(cmd, **args):
            r = call(DEFAULT_SOCKET, {'cmd': cmd, 'args': args})
            if not r.get('ok') or not r.get('result', {}).get('ok'):
                raise RuntimeError(r)
            return r['result']

        boot = query('hello')['boot_id']

        def action(name, value=''):
            before = query('paper.status')
            if before.get('receipt_token_version')!=1:raise RuntimeError('Requires perf9 token receipts')
            token=uuid.uuid4().hex
            start = time.monotonic()
            query('paper.command', action=name, value=value,request_token=token)
            while time.monotonic() - start < 120:
                s = query('paper.status')
                if s['boot_id'] != boot:
                    raise RuntimeError('Unexpected reboot')
                app = s['app']
                if (s.get('performance', {}).get('completed', 0) > before.get('performance', {}).get('completed', 0)
                        and (s.get('last_action') != name or s.get('last_request_token')!=token)):
                    raise RuntimeError('Unexpected input during comparison: '+str(s.get('last_action')))
                if (app['revision'] > before['app']['revision'] and
                        app['revision'] == app['presented'] and s.get('last_action') == name and
                        s.get('performance', {}).get('completed', 0) > before.get('performance', {}).get('completed', 0)):
                    row = {'action': name, 'value': value,
                           'seconds': round(time.monotonic() - start, 3),
                           'before': before, 'after': s}
                    old = before['app'].get('font_cache', {})
                    new = app.get('font_cache', {})
                    row['font_delta'] = {k: new.get(k, 0) - old.get(k, 0) for k in
                                         ('hash_us', 'validation_us', 'index_us', 'glyph_io_us',
                                          'glyph_opens', 'glyph_reads', 'glyph_hits')}
                    log.write(json.dumps(row, ensure_ascii=False) + '\n'); log.flush()
                    expected = {'home': 0, 'library': 1, 'open': 2, 'continue': 2, 'next': 2, 'prev': 2}.get(name)
                    if expected is not None and app.get('screen') != expected:
                        raise RuntimeError('Incoherent command/frame receipt')
                    if name in ('next','prev') and all(app['reader'][k]==before['app']['reader'][k] for k in ('chapter','offset')):
                        raise RuntimeError('Page did not move; boundary is not a successful turn')
                    if app.get('error') or app.get('font_error') or s.get('last_action_error'):
                        raise RuntimeError(row)
                    return row
                time.sleep(.1)
            raise TimeoutError(name)

        succeeded=False
        if a.experiment=='header':
            original=query('paper.status')['app']['settings']['book_header']
            def restore_header():
                try:
                    action('home');action('header-mode',candidate if succeeded and a.keep_candidate else original);action('home')
                except Exception as exc:
                    log.write(json.dumps({'restore_failed':str(exc),'original_header':original})+'\n')
                    raise
            cleanup.callback(restore_header)
        # Same starting position each run: forward N, back N. Font caches are
        # cleared by both modes. Keep book metadata warm before comparing I/O.
        action('library'); action('open', str(a.book_index)); action('home')
        initial = query('paper.status')['app']['reader']
        for mode in (baseline, candidate, candidate, baseline):
            action('home'); action(command, mode)
            run = [action('continue')]
            for _ in range(a.pages):
                run.append(action('next'))
            for _ in range(a.pages):
                run.append(action('prev'))
            end = query('paper.status')['app']['reader']
            if (end['chapter'], end['offset']) != (initial['chapter'], initial['offset']):
                raise RuntimeError('AB comparison failed to restore reader location')
            samples.extend(dict(mode=mode, action=r['action'], seconds=r['seconds'],
                                performance=r['after'].get('performance', {})) for r in run)
        action('home'); action(command, candidate); action('home')
        def percentile(xs, q):
            xs = sorted(xs)
            return xs[min(len(xs) - 1, int((len(xs) - 1) * q + .5))]
        summary = {}
        for mode in (baseline, candidate):
            summary[mode] = {}
            for op in ('continue', 'next', 'prev'):
                xs = [r['seconds'] for r in samples if r['mode'] == mode and r['action'] == op]
                summary[mode][op] = dict(n=len(xs), p50=statistics.median(xs), p95=percentile(xs, .95))
        log.write(json.dumps({'summary': summary}, ensure_ascii=False) + '\n')
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        succeeded=True


if __name__ == '__main__':
    main()
