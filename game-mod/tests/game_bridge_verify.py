"""Integration test against a running demo world; does not inject Taskmgr.
Requires a locally running Fabric 1.21.1 instance with this mod installed.
The protocol is the product's game input API, not desktop SendInput.
"""
import ctypes
from ctypes import wintypes
import json, os, socket, struct, threading, time, hashlib
from pathlib import Path
from collections import deque

discovery=json.loads((Path(os.environ['LOCALAPPDATA'])/'MCNativeTaskmgr/session.json').read_text())
evidence=Path(__file__).resolve().parents[2]/'evidence'
evidence.mkdir(exist_ok=True)

class Connection:
    def __init__(self):
        self.socket=socket.create_connection(('127.0.0.1',discovery['port']),timeout=5)
        self.socket.sendall(('MCNTM1 '+discovery['token']+'\n').encode())
        self.socket.settimeout(5);self.lock=threading.Lock();self.alive=True
        self.frames=deque(maxlen=200);self.statuses=deque(maxlen=100)
        self.reader=threading.Thread(target=self.receive,daemon=True);self.reader.start()
        self.heartbeat=threading.Thread(target=self.pulse,daemon=True);self.heartbeat.start()
    def send(self,kind,payload=b''):
        packet=struct.pack('>i',kind)+payload
        with self.lock:self.socket.sendall(struct.pack('>i',len(packet))+packet)
    def pulse(self):
        while self.alive:
            try:self.send(0)
            except OSError:break
            time.sleep(.25)
    def exact(self,n):
        data=b''
        while len(data)<n:
            p=self.socket.recv(n-len(data))
            if not p:raise EOFError()
            data+=p
        return data
    def receive(self):
        try:
            while self.alive:
                n=struct.unpack('>i',self.exact(4))[0]
                assert 4<=n<=10000
                packet=self.exact(n);kind=struct.unpack_from('>i',packet)[0]
                if kind==1:
                    seq,w,h,sw,sh,flags=struct.unpack_from('>6i',packet,4)
                    assert len(packet)==28+w*h*3
                    self.frames.append(dict(time=time.monotonic(),sequence=seq,width=w,height=h,sourceWidth=sw,sourceHeight=sh,flags=flags,rgb=packet[28:]))
                elif kind==2:self.statuses.append(json.loads(packet[4:]))
        except (OSError,EOFError):pass
    def status(self):
        if not self.statuses:raise RuntimeError('No game diagnostics received')
        return self.statuses[-1].copy()
    def key(self,key,action):self.send(10,struct.pack('>4i',key,0,action,0))
    def close(self):
        self.alive=False
        try:self.socket.shutdown(socket.SHUT_RDWR)
        except OSError:pass
        self.socket.close();self.reader.join(2);self.heartbeat.join(2)

def iconic():
    user=ctypes.WinDLL('user32',use_last_error=True)
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.IsIconic.argtypes=[wintypes.HWND];user.IsIconic.restype=wintypes.BOOL
    result=[]
    def enum(h,parameter):
        pid=wintypes.DWORD();user.GetWindowThreadProcessId(h,ctypes.byref(pid))
        if pid.value==discovery['pid']:result.append(bool(user.IsIconic(h)))
        return True
    user.EnumWindows(callback(enum),0);return any(result)

report={}
bad=socket.create_connection(('127.0.0.1',discovery['port']),timeout=3)
bad.sendall(b'MCNTM1 '+b'0'*64+b'\n');report['invalid_token_rejected']=bad.recv(1)==b'';bad.close()
connection=Connection()
try:
    time.sleep(1.5)
    assert connection.frames,'No internal framebuffer frames'
    connection.send(15,struct.pack('>i',1));time.sleep(1.2)
    # If the game had paused before connecting, resume through the same game input API.
    if connection.status()['screen']:
        connection.key(256,1);connection.key(256,0);time.sleep(.7)
    initial=connection.status();assert initial['inWorld'] and not initial['screen'],initial
    report['actual_win32_is_iconic']=iconic();report['minimized_game_status']=initial
    start=len(connection.frames);start_time=time.monotonic();time.sleep(2)
    measured=list(connection.frames)[start:]
    report['minimized_export_fps']=len(measured)/(time.monotonic()-start_time)
    report['distinct_minimized_frames']=len({hashlib.sha256(f['rgb']).hexdigest() for f in measured})
    connection.key(87,1);time.sleep(.85);connection.key(87,0);time.sleep(.7)
    moved=connection.status();report['movement_delta']=((moved['x']-initial['x'])**2+(moved['z']-initial['z'])**2)**.5
    connection.send(12,struct.pack('>4d',40.,10.,.5,.5));time.sleep(.7)
    looked=connection.status();report['yaw_delta']=looked['yaw']-moved['yaw'];report['pitch_delta']=looked['pitch']-moved['pitch']
    connection.key(256,1);connection.key(256,0);time.sleep(.7);report['escape_opens_menu']=connection.status()['screen']
    connection.key(256,1);connection.key(256,0);time.sleep(.7);report['escape_returns_to_world']=not connection.status()['screen']
    connection.send(15,struct.pack('>i',0));time.sleep(.7);report['focus_release_clears_keys']=connection.status()['heldKeys']==0 and not connection.status()['controlled']
    report['frame_dimensions']=[connection.frames[-1]['width'],connection.frames[-1]['height']]
    sample=connection.frames[-1]
    try:
        from PIL import Image
        image=Image.frombytes('RGB',(sample['width'],sample['height']),sample['rgb'])
        image.resize((960,540),Image.Resampling.NEAREST).save(evidence/'game-minimized-frame.png')
    except ImportError:pass
    connection.send(15,struct.pack('>i',1));time.sleep(.25);connection.key(87,1);time.sleep(.7)
    report['held_key_before_disconnect']=connection.status()['heldKeys']>0
finally:connection.close()
time.sleep(.4)
connection=Connection()
try:
    time.sleep(1);report['disconnect_releases_keys']=connection.status()['heldKeys']==0 and not connection.status()['controlled']
finally:connection.close()
report['game_bridge_passed']=all([report['invalid_token_rejected'],report['actual_win32_is_iconic'],report['minimized_export_fps']>=10,
    report['movement_delta']>.05,abs(report['yaw_delta'])>1,abs(report['pitch_delta'])>.1,
    report['escape_opens_menu'],report['escape_returns_to_world'],report['focus_release_clears_keys'],report['disconnect_releases_keys']])
report['taskmgr_rendering_tested']=False
(evidence/'game-bridge-test.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
if not report['game_bridge_passed']:raise SystemExit(1)
