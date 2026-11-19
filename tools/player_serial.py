#!/usr/bin/env python3
"""Control Tabnamp and collect its diagnostics over USB Serial/JTAG (pyserial)."""
import argparse
import json
import time

class Player:
    def __init__(self, port, log=None):
        import serial
        self.serial=serial.Serial(port=None,baudrate=115200,timeout=.25,write_timeout=5)
        self.serial.rts=True
        self.serial.dtr=True
        self.serial.port=port
        self.serial.open()
        self.serial.rts=False
        self.serial.dtr=False
        self.log=log
        self.serial.write(b'\r\n')
        time.sleep(.2)
    def close(self):
        self.serial.close()
    def command(self,*args,timeout=15):
        operation=str(args[0]) if args else 'status'
        if any(any(ord(c)>126 for c in str(arg)) for arg in args):
            args=('utf8',)+tuple(str(arg).encode('utf-8').hex() for arg in args)
        values=[]
        for arg in args:
            arg=str(arg)
            if any(ord(c)<32 for c in arg):
                raise ValueError('Control characters rejected')
            values.append('"'+arg.replace('\\','\\\\').replace('"','\\"')+'"')
        command=('player '+' '.join(values)+'\r\n').encode()
        if len(command)>4600:
            raise ValueError('Command too long')
        # A repeated read-only query may leave a late reply queued after the
        # first valid response. Preserve its diagnostics, but do not accept it
        # as the result of a later query after changing EQ/orientation/etc.
        pending=getattr(self.serial,'in_waiting',0)
        if pending:
            stale=self.serial.read(pending).decode(errors='replace')
            if self.log:self.log.write(stale);self.log.flush()
        self.serial.write(command)
        deadline=time.monotonic()+timeout
        retries=0;next_retry=time.monotonic()+2
        while time.monotonic()<deadline:
            if args and args[0] in ('status','files') and time.monotonic()>=next_retry:
                retries+=1;self.serial.write(command);next_retry=time.monotonic()+2
            line=self.serial.readline().decode(errors='replace')
            if self.log:
                self.log.write(line)
                self.log.flush()
            start=line.find('TABNAMP {')
            if start>=0:
                try:reply,_=json.JSONDecoder().raw_decode(line[start+8:])
                except json.JSONDecodeError:
                    # USB Serial/JTAG may cut a line during an idle TX timeout.
                    # Retry only read-only queries; never repeat control commands.
                    if args and args[0] in ('status','files') and retries<3:
                        retries+=1;self.serial.write(command)
                    continue
                if operation=='status' and not all(key in reply for key in (
                    'state_id','uri','position_ms','buffered_frames',
                    'output_writes','output_errors','output_peak','uptime_ms',
                    'repeat','volume','mute','orientation_mode','landscape','output',
                    'eq_enabled','bands_db','usb_mounted','psram_free','internal_free')):continue
                if operation=='files' and not all(key in reply for key in ('entries','path','total')):continue
                if operation not in ('status','files') and ('state_id' in reply or 'entries' in reply):continue
                if not reply.get('ok'):
                    raise RuntimeError(reply)
                return reply
        raise TimeoutError('No Tabnamp reply: '+str(args))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True)
    p.add_argument('command',nargs='*',default=['status'])
    args=p.parse_args()
    player=Player(args.port)
    try:print(json.dumps(player.command(*(args.command or ['status'])),ensure_ascii=False,indent=2))
    finally:player.close()
