import io,json,sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
import paper_action
from paper_stability_monitor import sample_health
from paper_loading_acceptance import loading_sample,cancellation_ready

class OfflineToolsTest(unittest.TestCase):
    def test_cancel_accepts_small_active_jobs_but_not_stale_counters(self):
        self.assertTrue(cancellation_ready({'active':True,'frames':0,'bytes_done':4096}))
        self.assertTrue(cancellation_ready({'active':True,'frames':1,'bytes_done':0}))
        self.assertFalse(cancellation_ready({'active':False,'frames':2,'bytes_done':900000}))
        self.assertFalse(cancellation_ready({'active':True,'frames':0,'bytes_done':0}))
    def test_previous_loading_counters_do_not_become_new_feedback(self):
        self.assertEqual(loading_sample({'active':False,'frames':8,'bytes_done':1000000}),(False,0))
        self.assertEqual(loading_sample({'active':True,'frames':1,'bytes_done':2048}),(True,2048))
    def test_eight_hour_schedule_simulated(self):
        now=[0];sink=io.StringIO()
        def q(cmd):return {'boot_id':'b','board_ready':True,'heap_free':200000,'psram_free':5000000} if cmd=='status' else {'boot_id':'b','app':{}}
        result=sample_health(q,sink,8*3600,30,lambda:now[0],lambda n:now.__setitem__(0,now[0]+n))
        self.assertEqual(result['samples'],961);self.assertEqual(result['seconds'],28800)
    def test_errors_and_reboot_fail(self):
        for error in ('app','font','reboot','dropped'):
            def q(cmd):
                if cmd=='status':return {'boot_id':'b','board_ready':True,'heap_free':1,'psram_free':2,'dropped':int(error=='dropped')}
                return {'boot_id':'new' if error=='reboot' else 'b','app':{'error':'bad' if error=='app' else '', 'font_error':'bad' if error=='font' else ''}}
            sink=io.StringIO()
            with self.assertRaises(RuntimeError):sample_health(q,sink,1)
            self.assertEqual(json.loads(sink.getvalue().splitlines()[-1])['result'],'FAIL')
    def test_initial_transport_failure_recorded(self):
        sink=io.StringIO()
        with self.assertRaises(TimeoutError):sample_health(lambda _:(_ for _ in ()).throw(TimeoutError('test')),sink,1)
        self.assertIn('FAIL',sink.getvalue())
    def test_page_receipt_requires_movement_and_reader(self):
        before={'boot_id':'b','receipt_token_version':1,'performance':{'completed':1},'app':{'revision':1,'presented':1,'screen':2,'reader':{'chapter':0,'offset':0}}}
        for screen,offset,success in ((2,100,True),(2,0,False),(9,100,False)):
            after={'boot_id':'b','last_request_token':'token','last_action':'next','performance':{'completed':2},'app':{'revision':2,'presented':2,'screen':screen,'reader':{'chapter':0,'offset':offset}}}
            with patch.object(paper_action.uuid,'uuid4') as token,patch.object(paper_action,'query',side_effect=[before,{},after]):
                token.return_value.hex='token'
                if success:self.assertEqual(paper_action.action('next')['receipt'],after)
                else:
                    with self.assertRaises(RuntimeError):paper_action.action('next')
    def test_identical_action_from_other_request_is_not_success(self):
        before={'boot_id':'b','receipt_token_version':1,'performance':{'completed':1},'app':{'revision':1,'presented':1}}
        after={'boot_id':'b','last_request_token':'old-request','last_action':'next','performance':{'completed':2}}
        with patch.object(paper_action,'query',side_effect=[before,{},after]):
            with self.assertRaisesRegex(RuntimeError,'Different request'):paper_action.action('next')
    def test_image_only_page_can_move_with_same_text_offset(self):
        before={'boot_id':'b','receipt_token_version':1,'performance':{'completed':1},'app':{'revision':1,'presented':1,'screen':2,'reader':{'chapter':0,'offset':0,'page_hint':0}}}
        after={'boot_id':'b','last_request_token':'token','last_action':'next','performance':{'completed':2},'app':{'revision':2,'presented':2,'screen':2,'reader':{'chapter':0,'offset':0,'page_hint':1}}}
        with patch.object(paper_action.uuid,'uuid4') as token,patch.object(paper_action,'query',side_effect=[before,{},after]):
            token.return_value.hex='token'
            self.assertEqual(paper_action.action('next')['receipt'],after)

if __name__=='__main__':unittest.main()
