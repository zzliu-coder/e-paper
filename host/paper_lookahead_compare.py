"""Opt-in real-device ABBA experiment. DOES NOT RUN unless explicitly invoked.

Uses the existing USB-owner socket and perf9 token receipts. No flash, network,
audio or filesystem transfers. Requires an already open book with enough pages.
Restores the original reader location and experiment setting on successful runs.
On interruption, logs the error; never blindly retries a timed-out page turn.
"""
import argparse,json,statistics,time
from pathlib import Path
from paper_action import action,query

def location(s):
    r=s['app']['reader'];return r['engine'],r['chapter'],r['offset'],r.get('page_hint',-1)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--pages',type=int,default=3);p.add_argument('--idle',type=float,default=2)
    a=p.parse_args()
    if not 1<=a.pages<=20 or not .5<=a.idle<=10:p.error('pages 1..20; idle 0.5..10 seconds')
    initial=query('paper.status')
    if initial.get('receipt_token_version')!=1 or initial['app']['screen']!=2 or not initial['app']['reader']['open']:raise RuntimeError('Open a book using perf9 first')
    start=location(initial);original=initial['app']['glyph_lookahead']['enabled'];boot=initial['boot_id'];rows=[]
    a.out.parent.mkdir(parents=True,exist_ok=True)
    with a.out.open('x') as log:
        def emit(row):log.write(json.dumps(row,ensure_ascii=False)+'\n');log.flush()
        try:
            for mode in ('off','on','on','off'):
                action('home');action('glyph-lookahead',mode);action('continue')
                if location(query('paper.status'))!=start:raise RuntimeError('Starting location differs')
                for page in range(a.pages):
                    time.sleep(a.idle);before=query('paper.status')
                    if before['boot_id']!=boot:raise RuntimeError('Unexpected reboot')
                    if before['app']['glyph_lookahead']['error']:raise RuntimeError('Prefetch error')
                    result=action('next');after=result['receipt'];left=before['app']['font_cache'];right=after['app']['font_cache']
                    row={'mode':mode,'page':page,'seconds':result['seconds'],'idle_seconds':a.idle,
                         'prepared':before['app']['glyph_lookahead']['prepared'],
                         'font_delta':{k:right[k]-left[k] for k in ('glyph_reads','glyph_io_us','glyph_opens','glyph_hits')},
                         'performance':after['performance']};rows.append(row);emit(row)
                for _ in range(a.pages):action('prev')
                if location(query('paper.status'))!=start:raise RuntimeError('Reading position was not restored')
            summary={m:{'n':sum(r['mode']==m for r in rows),'median_seconds':statistics.median(r['seconds'] for r in rows if r['mode']==m),
                        'glyph_reads':sum(r['font_delta']['glyph_reads'] for r in rows if r['mode']==m)} for m in ('off','on')}
            action('glyph-lookahead','on' if original else 'off')
            emit({'result':'PASS','summary':summary,'auto_selected':False,'physical_quality':'NOT_PROVEN'})
            print(json.dumps(summary,ensure_ascii=False,indent=2))
        except Exception as exc:
            emit({'result':'FAIL','error':str(exc),'original_lookahead':original,'automatic_retry':False,
                  'recovery':'Inspect current device state before restoring experiment mode or reading location.'})
            raise
if __name__=='__main__':main()
