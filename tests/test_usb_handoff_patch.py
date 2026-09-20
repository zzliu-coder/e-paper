import importlib.util
from pathlib import Path
import pytest

spec = importlib.util.spec_from_file_location('handoff', Path(__file__).resolve().parents[1] / 'tools/patch_tinyusb_handoff.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

def test_overlay_is_idempotent_and_preserves_failure():
    original = patch.OLD + '\n' + patch.OLD_FORMAT
    fixed = patch.patched(original)
    assert 'return result;' in fixed
    assert 'storage->mount_point = previous_mount_point;' in fixed
    assert 'ret = ESP_ERR_NOT_FOUND;' in fixed
    assert patch.patched(fixed) == fixed

def test_unknown_dependency_fails_closed():
    with pytest.raises(ValueError):
        patch.patched('unreviewed dependency')
