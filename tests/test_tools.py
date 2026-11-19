import unittest, tempfile, subprocess, wave, sys, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from player_serial import Player
class Tools(unittest.TestCase):
    def run_tool(self,tool,*args):
        return subprocess.run([sys.executable,str(ROOT/'tools'/tool),*map(str,args)],capture_output=True,text=True)
    def test_install_preserves_files(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(self.run_tool('prepare_sd.py',d).returncode,0)
            config=Path(d)/'tabnamp/config/settings.json';config.write_text('USER')
            self.assertEqual(self.run_tool('prepare_sd.py',d).returncode,0)
            self.assertEqual(config.read_text(),'USER')
    def test_install_rejects_symlink(self):
        with tempfile.TemporaryDirectory() as d,tempfile.TemporaryDirectory() as other:
            (Path(d)/'tabnamp').symlink_to(other,target_is_directory=True)
            self.assertNotEqual(self.run_tool('prepare_sd.py',d).returncode,0)
            self.assertEqual(list(Path(other).iterdir()),[])
    def test_tone_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'teste.wav';self.assertEqual(self.run_tool('generate_tone.py',p).returncode,0)
            with wave.open(str(p)) as f:self.assertEqual((f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes()),(2,2,48000,240000))
            content=p.read_bytes();self.assertNotEqual(self.run_tool('generate_tone.py',p).returncode,0);self.assertEqual(p.read_bytes(),content)
class SerialProtocol(unittest.TestCase):
    def status(self,**changes):
        reply=dict(ok=True,state_id=3,uri='/sdcard/test.mp3',position_ms=1000,
                   buffered_frames=4096,output_writes=100,output_errors=0,output_peak=500,uptime_ms=2000,
                   repeat=0,volume=25,mute=False,orientation_mode=1,landscape=True,output=0,
                   eq_enabled=True,bands_db=[0]*10,usb_mounted=True,psram_free=24000000,internal_free=230000)
        reply.update(changes)
        return b'TABNAMP '+json.dumps(reply).encode()+b'tabnamp> \n'
    def player(self,lines):
        class Wire:
            def __init__(self):self.lines=list(lines);self.sent=[]
            def write(self,data):self.sent.append(data)
            def readline(self):
                if not self.lines:raise AssertionError('No matching reply was accepted')
                return self.lines.pop(0)
        player=object.__new__(Player);player.serial=Wire();player.log=None;return player
    def test_status_skips_control_ack_and_accepts_prompt_suffix(self):
        p=self.player([b'TABNAMP {"ok":true}\n',self.status()])
        self.assertEqual(p.command('status')['state_id'],3)
        self.assertEqual(len(p.serial.sent),1)
    def test_status_rejects_valid_json_with_dropped_telemetry(self):
        damaged=self.status().replace(b'"buffered_frames": 4096, ',b'')
        p=self.player([damaged,self.status(position_ms=1200)])
        self.assertEqual(p.command('status')['position_ms'],1200)
    def test_control_skips_old_status_without_repeating_command(self):
        p=self.player([b'TABNAMP {"ok":true,"state_id":3}\n',b'TABNAMP {"ok":true}tabnamp> \n'])
        self.assertEqual(p.command('repeat'),{'ok':True})
        self.assertEqual(len(p.serial.sent),1)
    def test_unicode_paths_survive_ascii_only_console(self):
        p=self.player([b'TABNAMP {"ok":true}\n'])
        uri='/usb/Músicas/teste.mp3';p.command('play',uri)
        command=p.serial.sent[0].decode('ascii')
        self.assertIn('"utf8"',command)
        self.assertIn(uri.encode('utf-8').hex(),command)


class EnduranceProtocol(unittest.TestCase):
    def exercise(self, stall=False):
        import json
        from unittest.mock import patch
        import hardware_endurance
        uri='/sdcard/test.mp3'
        class Device:
            def __init__(self,*args):
                self.playing=False;self.ready=False;self.i=0;self.repeat=0;self.volume=25;self.mode=2;self.band=0
            def close(self):pass
            def command(self,cmd,*args):
                if cmd=='repeat':self.repeat=(self.repeat+1)%3
                elif cmd=='volume':self.volume=int(args[0])
                elif cmd=='rotate':self.mode=int(args[0])
                elif cmd=='band':self.band=int(args[1])
                elif cmd=='play':self.playing=True;self.ready=False
                elif cmd=='stop':self.playing=False
                if cmd!='status':return {'ok':True}
                state=6;position=0;uptime=10000;writes=1000
                if self.playing:
                    if not self.ready:self.ready=True;state=3;position=350813
                    else:
                        self.i+=1;uptime+=self.i*5000
                        state=1 if self.i==1 or stall else 3
                        position=max(0,self.i-1)*5000 if not stall else 0
                        writes+=0 if stall else self.i*500
                return dict(ok=True,state_id=state,uri=uri,position_ms=position,uptime_ms=uptime,
                            output_writes=writes,output_errors=0,output_peak=500,eq_enabled=True,
                            bands_db=[0,0,0,0,0,self.band,0,0,0,0],repeat=self.repeat,volume=self.volume,
                            mute=False,orientation_mode=self.mode,landscape=self.mode==1,
                            usb_mounted=True,internal_free=220000,psram_free=24000000)
        with tempfile.TemporaryDirectory() as d:
            output=Path(d);settings=output/'settings.json';settings.write_text(json.dumps(dict(eq_enabled=True,bands_db=[0]*10)))
            with patch.object(hardware_endurance,'Player',Device),patch.object(hardware_endurance.time,'sleep',lambda _:None),patch('builtins.print'):
                if stall:
                    with self.assertRaisesRegex(AssertionError,'No output progress'):
                        hardware_endurance.run('fake',uri,60,354813,settings,output)
                else:hardware_endurance.run('fake',uri,60,354813,settings,output)
            return json.loads((output/'hardware-endurance-report.json').read_text())
    def test_repeat_loading_transition_does_not_abort_soak(self):
        r=self.exercise();self.assertEqual(r['status'],'passed');self.assertEqual(r['completed_tracks'],1)
        self.assertGreaterEqual(r['elapsed_seconds'],60)
    def test_loading_without_output_progress_is_rejected(self):
        self.assertEqual(self.exercise(stall=True)['status'],'failed')

if __name__=='__main__':unittest.main()
