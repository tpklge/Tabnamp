#!/usr/bin/env python3
"""Check codec gain, UI scheduling and SD playback on the running Tab5.

Serial timings include console transport. Flush counts describe LVGL submissions,
not physical panel FPS. Acoustic level and touch feel require a listener/user.
"""
import argparse
import hashlib
import json
import time
from pathlib import Path
from player_serial import Player


def test(port, report_path, seconds):
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report = dict(status='running', checks=[], samples=[], acoustic_confirmation=False,
                  physical_touch_validated=False, panel_fps_measured=False)
    app = Path(__file__).resolve().parents[1] / 'build/tabnamp.bin'
    report['firmware_sha256'] = hashlib.sha256(app.read_bytes()).hexdigest()

    def save():
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')

    with report_path.with_suffix('.log').open('w') as log:
        player = Player(port, log)
        initial = None
        try:
            initial = player.command('status')
            report['initial'] = initial
            player.command('volume', 20)
            if initial['mute']:
                player.command('mute')
            for route in (0, 1):
                player.command('output', route)
                player.command('play', '/sdcard/tabnamp/music/testes-20261119/apitos.mp3')
                time.sleep(3)
                status = player.command('status')
                assert status['state_id'] == 3 and status['position_ms'] > 1000, status
                assert status['output'] == route and status['output_peak'] > 0, status
                assert status['codec_volume'] == 100, status
                assert 0 <= status['dac_left_reg'] == status['dac_right_reg'] < 30, status
                assert status['output_errors'] == initial['output_errors'], status
                report['checks'].append(dict(check='codec_unity_route_' + str(route), sample=status))
                save()
            player.command('output', 0)
            player.command('play', '/sdcard/tabnamp/music/mp3/Bruce Springsteen - Ghosts.mp3')
            time.sleep(3)
            player.command('perf-reset')
            before = player.command('status')
            began = time.monotonic()
            while time.monotonic() - began < seconds:
                time.sleep(max(0, min(2, seconds - (time.monotonic() - began))))
                status = player.command('status')
                assert status['state_id'] == 3, status
                assert status['output_errors'] == initial['output_errors'], status
                report['samples'].append(status)
                save()
            elapsed = (status['uptime_ms'] - before['uptime_ms']) / 1000
            report['ui_window'] = dict(seconds=elapsed,
                callbacks_per_second=(status['ui_refresh_calls'] - before['ui_refresh_calls']) / elapsed,
                flush_submissions_per_second=(status['ui_flushes'] - before['ui_flushes']) / elapsed,
                refresh_us_max=status['ui_refresh_us_max'], callback_gap_ms_max=status['ui_poll_gap_ms_max'])
            assert report['ui_window']['callbacks_per_second'] >= 25, report['ui_window']
            assert report['ui_window']['callback_gap_ms_max'] < 100, report['ui_window']
            assert status['position_ms'] > before['position_ms'] + seconds * 900, status
            assert status['underruns'] == before['underruns'], status
            player.command('pause')
            time.sleep(.4)
            paused = player.command('status')
            time.sleep(1)
            held = player.command('status')
            assert held['state_id'] == 4 and held['position_ms'] == paused['position_ms'], held
            report['checks'].append(dict(check='large_mp3_play_pause', sample=held))
            report['status'] = 'passed'
        except BaseException as error:
            report['status'] = 'failed'
            report['error'] = str(error)
            raise
        finally:
            try:
                player.command('pause')
                if initial:
                    player.command('output', initial['output'])
                    player.command('volume', initial['volume'])
                    if initial['mute'] != player.command('status')['mute']:
                        player.command('mute')
                report['final'] = player.command('status')
            finally:
                player.close()
                save()
    print(json.dumps(report['ui_window'], indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--report', type=Path, default=Path('artifacts/hardware-ui-audio-report.json'))
    parser.add_argument('--seconds', type=int, default=30)
    args = parser.parse_args()
    if not 5 <= args.seconds <= 120:
        parser.error('--seconds must be between 5 and 120')
    test(args.port, args.report, args.seconds)
