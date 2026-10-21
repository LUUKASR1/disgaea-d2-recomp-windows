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


def button(label="share diagnostic report"):
    for _ in range(12):
        root = hierarchy()
        # Material buttons expose their all-caps display text. --windows may
        # list a child popup both independently and under its parent window.
        found = {tuple(map(int, re.findall(r'\d+', n.get('bounds', ''))))
                 for n in root.iter('node')
                 if n.get('text', '').casefold() == label.casefold()
                 and n.get('class') == 'android.widget.Button'
                 and n.get('enabled') == 'true' and n.get('clickable') == 'true'}
        if len(found) == 1:
            bounds = next(iter(found))
            if len(bounds) == 4 and bounds[2] > bounds[0] and bounds[3] > bounds[1]:
                return bounds
        time.sleep(1)
    raise AssertionError('Button missing: ' + label)


def tap(bounds):
    adb('shell', 'input', 'tap', (bounds[0]+bounds[2])//2, (bounds[1]+bounds[3])//2)


def tap_node(text=None, description=None):
    root = hierarchy()
    for n in root.iter('node'):
        if ((text is not None and n.get('text', '').casefold() == text.casefold()) or
                (description is not None and n.get('content-desc', '').casefold() == description.casefold())):
            b = tuple(map(int, re.findall(r'\d+', n.get('bounds', ''))))
            if len(b) == 4 and b[2]>b[0] and b[3]>b[1]:
                tap(b)
                return True
    return False


def document_test(output):
    tap(button('Select file to validate'))
    time.sleep(2)
    foreground = adb('shell', 'dumpsys', 'activity', 'activities')
    assert 'documentsui' in foreground.lower(), foreground
    adb('shell', 'input', 'keyevent', 'KEYCODE_BACK')
    button()
    cancelled = adb('shell', 'run-as', PACKAGE, 'cat', 'files/selected-document-report.txt')
    assert 'document_status=cancelled' in cancelled, cancelled
    # Exercise the real system picker and Downloads provider, not a fake result.
    fixture = output / 'd2-document-fixture.bin'
    fixture.write_bytes(b'\x7fELF' + bytes(range(256))*300)
    adb('push', fixture, '/sdcard/Download/d2-document-fixture.bin')
    adb('shell', 'am', 'broadcast', '-a', 'android.intent.action.MEDIA_SCANNER_SCAN_FILE',
        '-d', 'file:///sdcard/Download/d2-document-fixture.bin')
    tap(button('Select file to validate'))
    time.sleep(2)
    if not tap_node(text='d2-document-fixture.bin'):
        assert tap_node(description='Show roots'), ET.tostring(hierarchy(), encoding='unicode')
        time.sleep(1)
        assert tap_node(text='Downloads'), ET.tostring(hierarchy(), encoding='unicode')
        time.sleep(2)
        assert tap_node(text='d2-document-fixture.bin'), ET.tostring(hierarchy(), encoding='unicode')
    button()
    for _ in range(15):
        selected = adb('shell', 'run-as', PACKAGE, 'cat', 'files/selected-document-report.txt')
        if 'document_status=readable' in selected:
            break
        time.sleep(1)
    (output / 'document-report.txt').write_text(selected)
    assert 'document_status=readable' in selected, selected
    assert 'document_boundary_check=passed' in selected, selected
    assert 'document_container_hint=elf-header-only' in selected, selected
    assert 'document_bytes=76804' in selected, selected
    assert 'document_head_sample_bytes=65536' in selected, selected
    assert 'document_tail_sample_bytes=64' in selected, selected
    assert 'document_game_boot=false' in selected
    assert 'document_ps3_mount_available=false' in selected
    assert 'd2-document-fixture.bin' not in selected and 'content://' not in selected
    adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
    adb('shell', 'am', 'start', '-W', '-n', ACTIVITY)
    button()
    retained = adb('shell', 'run-as', PACKAGE, 'cat', 'files/selected-document-report.txt')
    assert retained == selected, retained
    # Repeated selection/cancellation returns control without replacing the
    # validated file or retaining a descriptor in the application.
    tap(button('Select file to validate'))
    time.sleep(1)
    adb('shell', 'input', 'keyevent', 'KEYCODE_BACK')
    button()
    assert 'document_status=cancelled' in adb('shell', 'run-as', PACKAGE, 'cat', 'files/selected-document-report.txt')
    # A generated UDF fixture has synthetic SFO and tiny placeholder assets.
    # It is transferred only to the CI device, never bundled with the APK.
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'port/vfs/tests'))
    from make_fixture import make
    image = output / 'd2-disc-fixture.iso'
    make(image, complete=True)
    adb('push', image, '/sdcard/Download/d2-disc-fixture.iso')
    adb('shell', 'am', 'broadcast', '-a', 'android.intent.action.MEDIA_SCANNER_SCAN_FILE',
        '-d', 'file:///sdcard/Download/d2-disc-fixture.iso')
    tap(button('Select file to validate'))
    time.sleep(2)
    if not tap_node(text='d2-disc-fixture.iso'):
        assert tap_node(description='Show roots'), ET.tostring(hierarchy(), encoding='unicode')
        time.sleep(1)
        assert tap_node(text='Downloads'), ET.tostring(hierarchy(), encoding='unicode')
        time.sleep(2)
        assert tap_node(text='d2-disc-fixture.iso'), ET.tostring(hierarchy(), encoding='unicode')
    button()
    for _ in range(15):
        disc = adb('shell', 'run-as', PACKAGE, 'cat', 'files/selected-document-report.txt')
        if 'disc_status=base-files-readable' in disc:
            break
        time.sleep(1)
    (output / 'disc-report.txt').write_text(disc)
    for field in ('document_status=readable', 'disc_backend=UDF', 'disc_title_id=BLUS31313',
                  'disc_required_files_checked=18', 'disc_required_files_failed=0',
                  'disc_status=base-files-readable', 'disc_game_boot=false',
                  'executable_format=unknown', 'executable_status=unrecognized',
                  'executable_guest_memory_written=false', 'executable_code_executed=false'):
        assert field in disc, disc
    assert 'content://' not in disc and 'd2-disc-fixture.iso' not in disc



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
        if 'status=presenting-test-triangle' in report and 'dispatch_status=passed' in report:
            break
        time.sleep(1)
    (args.output / 'native-report.txt').write_text(report)
    assert 'status=presenting-test-triangle' in report, report
    assert re.search(r'presented_test_frames=[1-9]\d*', report), report
    assert 'gameplay_available=false' in report
    assert 'document_picker_integrated=true' in report
    assert 'core_status=passed' in report and 'core_failures=0' in report, report
    for name in ('core_vm_reservation', 'core_ppu_big_endian', 'core_ppu_integer_ops',
                 'core_ppu_inline_reservation_cas', 'core_host_threads_guest_cas',
                 'core_auto_manual_events', 'core_counting_semaphore',
                 'core_spu_instruction_vectors', 'core_spu_numeric_vectors', 'core_guest_poll_stop', 'core_vm_release'):
        assert name + '=passed' in report, report
    assert 'core_full_runtime_linked=false' in report
    assert 'elf_status=passed' in report and 'elf_failures=0' in report, report
    assert 'elf_full_ppu_loader_linked=false' in report
    assert 'elf_code_executed=false' in report
    assert 'elf_game_loaded=false' in report
    assert 'dispatch_status=passed' in report and 'dispatch_failures=0' in report, report
    assert 'dispatch_generated_functions=11' in report
    assert 'dispatch_lifted_code_executed=true' in report
    assert 'dispatch_game_executed=false' in report
    assert 'dispatch_full_runtime_available=false' in report
    assert 'services_status=passed' in report and 'services_failures=0' in report, report
    assert 'bundle_status=passed' in report and 'bundle_failures=0' in report, report
    assert 'bundle_rounds=3' in report and 'bundle_workers_created_and_joined=12' in report
    assert 'bundle_game_boot=false' in report
    assert 'disc_guest_status=passed' in report and 'disc_guest_failures=0' in report, report
    assert 'disc_guest_game_executed=false' in report
    for name in ('disc_guest_executable_plan', 'disc_guest_assets_stat_seek_and_eof',
                 'disc_guest_errors_and_mount_transaction', 'disc_guest_descriptor_lifetime_and_cleanup',
                 'dispatch_integrated_disc'):
        assert name + '=passed' in report, report
    assert 'files_status=passed' in report and 'files_failures=0' in report, report
    assert 'files_game_loaded=false' in report
    assert 'files_document_picker_integrated=false' in report  # Synthetic fixture scope only
    assert 'files_document_inspection=passed' in report
    for name in ('files_descriptor_bounds_and_parallel_reads',
                 'files_lifted_open_read_seek_stat_close_and_eof',
                 'files_errors_recovery_and_handle_audit',
                 'files_truncation_transaction_and_cleanup', 'dispatch_integrated_files'):
        assert name + '=passed' in report, report
    for name in ('bundle_memory_allocate_free', 'bundle_mapping_protection_release',
                 'bundle_mutex_and_shared_counter', 'bundle_semaphore_wait_post_timeout',
                 'bundle_mutex_cross_thread_timeout', 'bundle_thread_create_exit_join',
                 'bundle_no_post_exit_execution', 'bundle_error_paths_and_recovery', 'bundle_resource_audit'):
        assert name + '=passed' in report, report
    for name in ('services_tables_initialized', 'services_nid_matches_lifted_import',
                 'services_syscall_timebase_frequency', 'services_syscall_current_time',
                 'services_hle_rtc_tick', 'services_hle_big_endian_output',
                 'services_hle_null_returns_einval', 'services_unknown_hle_rejected',
                 'services_unknown_syscall_returns_enosys',
                 'services_invalid_guest_pointers_rejected', 'services_recovery_after_errors'):
        assert name + '=passed' in report, report
    for name in ('dispatch_context_abi', 'dispatch_sdk_registry',
                 'dispatch_registry_collision_and_replace', 'dispatch_lifted_toc_arithmetic',
                 'dispatch_lifted_big_endian_store_load', 'dispatch_lifted_tail_trampoline',
                 'dispatch_lifted_indirect_call', 'dispatch_lifted_direct_call',
                 'dispatch_missing_target_rejected', 'dispatch_unsupported_syscall_rejected',
                 'dispatch_invalid_opd_rejected', 'dispatch_memory_bounds_rejected',
                 'dispatch_recovers_after_errors', 'dispatch_thread_local_context',
                 'dispatch_fixture_removed', 'dispatch_vm_release'):
        assert name + '=passed' in report, report
    assert 'elf_rejected_fixtures=29' in report, report
    for name in ('elf_segments_and_bss', 'elf_entry_opd_and_toc',
                 'elf_tls_template_metadata', 'elf_invalid_images_no_monitored_writes',
                 'elf_truncated_images_rejected', 'elf_tls_destination_overflow_rejected',
                 'elf_unmapped_tls_rejected', 'elf_overflowing_sections_rejected',
                 'elf_valid_reload_after_rejections', 'elf_absent_tls_resets_metadata',
                 'elf_fixture_file_removed', 'elf_vm_release'):
        assert name + '=passed' in report, report
    document_test(args.output)
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
    assert not adb('shell', 'run-as', PACKAGE, 'ls', 'files/').count('d2-elf-fixture-')
    assert not adb('shell', 'run-as', PACKAGE, 'ls', 'files/').count('d2-dispatch-fixture-')
    print('PASS: real picker, native UDF mount, synthetic SFO + 18 base assets, cancellation, lifecycle; all prior runtime tests + Vulkan/share UI')


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
