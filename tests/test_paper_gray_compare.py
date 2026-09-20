import importlib.util
from pathlib import Path
import pytest
spec=importlib.util.spec_from_file_location('gray_compare',Path(__file__).parents[1]/'host/paper_gray_compare.py')
# Host modules share the existing tests' host import path.
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def test_summary_excludes_transitions():
    rows=[]
    for mode in ('mono','gray4'):
        for kind,seconds in [('warmup',50),('transition',100),('steady',2),('steady',4)]:
            rows.append({'mode':mode,'kind':kind,'seconds':seconds,'receipt':{'display':{'elapsed_us':seconds*1000000,'phases':[{}]*3}}})
    result=module.summarize(rows)
    assert result['mono']['mean_seconds']==3
    assert result['gray4']['phase_counts']==[3]
    assert result['mono']['phase_counts'] is None
def test_summary_requires_measurements():
    with pytest.raises(ValueError):module.summarize([])
