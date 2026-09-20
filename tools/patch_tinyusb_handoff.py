"""Idempotent, fail-closed overlay for the pinned TinyUSB MSC handoff API."""
from pathlib import Path
import sys

OLD = '''    if (mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        // If the storage is mounted to application, mount it
        msc_storage_mount(storage);
    } else {
        // If the storage is mounted to USB host, unmount it
        msc_storage_unmount(storage);
    }
    storage->mount_point = mount_point;'''
NEW = '''    const tinyusb_msc_mount_point_t previous_mount_point = storage->mount_point;
    esp_err_t result;
    if (mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        // If the storage is mounted to application, mount it
        result = msc_storage_mount(storage);
    } else {
        // If the storage is mounted to USB host, unmount it
        result = msc_storage_unmount(storage);
    }
    // PAPER_MSC_HANDOFF_ERRORS: never expose a failed handoff as mounted.
    if (result != ESP_OK) {
        storage->mount_point = previous_mount_point;
        return result;
    }
    storage->mount_point = mount_point;'''
OLD_FORMAT = '''            tinyusb_event_cb(storage, TINYUSB_MSC_EVENT_FORMAT_REQUIRED);
            ret = ESP_OK;
            goto exit;'''
NEW_FORMAT = OLD_FORMAT.replace('ret = ESP_OK;', 'ret = ESP_ERR_NOT_FOUND;')

def patched(source):
    for old, new in ((OLD, NEW), (OLD_FORMAT, NEW_FORMAT)):
        if source.count(new) == 1:
            continue
        if source.count(old) != 1:
            raise ValueError('TinyUSB source changed; review handoff overlay before building')
        source = source.replace(old, new, 1)
    return source

def main():
    path = Path(sys.argv[1])
    source = path.read_text()
    result = patched(source)
    if result != source:
        path.write_text(result)
    print('TinyUSB handoff errors: verified')

if __name__ == '__main__':
    main()
