"""Exercise the actual Android window, share button and background/restore via adb."""
import argparse
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = 'org.disgaead2.bootstrap'
ACTIVITY = PACKAGE + '/.BootstrapActivity'


def adb(*args):
    return subprocess.check_output(['adb', *map(str, args)], text=True, timeout=60)


def hierarchy():
    # PopupWindow is deliberately non-focusable to preserve gamepad focus.
    # Query all interactive windows, not just the focused native game window.
    adb('shell', 'uiautomator', 'dump', '--windows', '/sdcard/d2-ui.xml')
    return ET.fromstring(adb('shell', 'cat', '/sdcard/d2-ui.xml'))


def button():
    for _ in range(12):
        root = hierarchy()
        # Material buttons expose their all-caps display text. --windows may
        # list a child popup both independently and under its parent window.
        found = {tuple(map(int, re.findall(r'\d+', n.get('bounds', ''))))
                 for n in root.iter('node')
                 if n.get('text', '').casefold() == 'share diagnostic report'
                 and n.get('class') == 'android.widget.Button'
                 and n.get('enabled') == 'true' and n.get('clickable') == 'true'}
        if len(found) == 1:
            bounds = next(iter(found))
            if len(bounds) == 4 and bounds[2] > bounds[0] and bounds[3] > bounds[1]:
                return bounds
        time.sleep(1)
    raise AssertionError('Share button is missing from the visible Android UI')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--output', type=Path, default=Path('android-ui-smoke'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    adb('install', '-r', args.apk)
    adb('shell', 'am', 'start', '-W', '-n', ACTIVITY)
    button()
    for _ in range(2):
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        time.sleep(1)
        adb('shell', 'am', 'start', '-W', '-n', ACTIVITY)
        button()
    for _ in range(12):
        report = adb('shell', 'run-as', PACKAGE, 'cat', 'files/android-vulkan-report.txt')
        if 'status=presenting-test-triangle' in report and 'core_status=passed' in report:
            break
        time.sleep(1)
    (args.output / 'native-report.txt').write_text(report)
    assert 'status=presenting-test-triangle' in report, report
    assert re.search(r'presented_test_frames=[1-9]\d*', report), report
    assert 'gameplay_available=false' in report
    assert 'core_status=passed' in report and 'core_failures=0' in report, report
    for name in ('core_vm_reservation', 'core_ppu_big_endian', 'core_ppu_integer_ops',
                 'core_ppu_inline_reservation_cas', 'core_host_threads_guest_cas',
                 'core_auto_manual_events', 'core_counting_semaphore',
                 'core_spu_instruction_vectors', 'core_spu_numeric_vectors', 'core_guest_poll_stop', 'core_vm_release'):
        assert name + '=passed' in report, report
    assert 'core_full_runtime_linked=false' in report
    bounds = button()
    (args.output / 'controls.xml').write_text(ET.tostring(hierarchy(), encoding='unicode'))
    # Capture the compositor output, not merely the Java layout.
    subprocess.run(['adb', 'shell', 'screencap', '-p', '/sdcard/d2-overlay.png'], check=True, timeout=30)
    adb('pull', '/sdcard/d2-overlay.png', args.output / 'overlay.png')
    adb('shell', 'input', 'tap', (bounds[0] + bounds[2]) // 2, (bounds[1] + bounds[3]) // 2)
    for _ in range(10):
        foreground = adb('shell', 'dumpsys', 'activity', 'activities')
        resumed = '\n'.join(line for line in foreground.splitlines() if 'ResumedActivity' in line)
        if 'ChooserActivity' in resumed or 'ResolverActivity' in resumed:
            (args.output / 'share-foreground.txt').write_text(resumed)
            break
        time.sleep(1)
    else:
        raise AssertionError('Tapping the visible share button did not open the Android chooser')
    print('PASS: SDK core primitive tests, Vulkan presents, diagnostic panel visible, Home/restore twice, share chooser opens')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        # Keep compositor/window evidence even when the first visibility check
        # fails, so a missing control can be distinguished from a test-query bug.
        out = Path('android-ui-smoke')
        out.mkdir(exist_ok=True)
        for name, command in (
            ('failure-windows.txt', ('shell', 'dumpsys', 'window', 'windows')),
            ('failure-logcat.txt', ('logcat', '-d', '-t', '300')),
            ('failure-native-report.txt', ('shell', 'run-as', PACKAGE, 'cat', 'files/android-vulkan-report.txt')),
        ):
            try:
                (out / name).write_text(adb(*command))
            except subprocess.SubprocessError:
                pass
        try:
            (out / 'failure-hierarchy.xml').write_text(ET.tostring(hierarchy(), encoding='unicode'))
            adb('shell', 'screencap', '-p', '/sdcard/d2-failure.png')
            adb('pull', '/sdcard/d2-failure.png', out / 'failure.png')
        except Exception:
            pass
        raise
