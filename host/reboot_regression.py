"""Reproduce startup traffic, then verify scenes and repeated connections."""
import argparse
from pathlib import Path

from metalio import Client, Journal, open_serial, render_scene
from hardware_acceptance import run, wait_until_ready


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    journal = Journal(out / 'reboot-regression.jsonl')
    try:
        for attempt in range(3):
            link = open_serial(args.port)
            try:
                client = Client(link, journal, expected_device='1020ba6e0be0')
                if attempt == 0:
                    report = run(client, storage_write=True)
                    assert report['identity']['version'] == '1.0.0-dev.9'
                    journal.record('hardware_report', report)
                    failures = {k: v for k, v in report['commands'].items()
                                if v['protocol'] != 'PASS'}
                    if failures:
                        raise RuntimeError(f'Hardware protocol failures: {failures}')
                else:
                    assert client.hello()['version'] == '1.0.0-dev.9'
                    wait_until_ready(client)
                for _ in range(40):
                    status = client.request('status')
                    if status['usb_stack_free_min_bytes'] < 1024:
                        raise RuntimeError(f'Insufficient stack margin: {status}')
                if attempt == 0:
                    scene = {'version': 'dev8-stack-fix', 'rects': [
                        {'x': 30, 'y': 30, 'w': 100, 'h': 60, 'black': True}]}
                    client.push_scene(scene)
                    if client.read_frame() != render_scene(scene):
                        raise RuntimeError('Full scene readback mismatch')
                journal.record('session_pass', {'attempt': attempt, 'status': status})
                print('PASS session', attempt, 'version', '1.0.0-dev.9',
                      'stack_min_bytes', status['usb_stack_free_min_bytes'], flush=True)
            finally:
                link.close()
        journal.record('regression_pass', {'sessions': 3, 'status_queries': 120})
    finally:
        journal.close()


if __name__ == '__main__':
    main()
