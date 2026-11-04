"""Observe the UnifiedRGB shim's Chroma IPC without controlling any lights.

No frame contents or individual image hashes are saved. A named cancellation
event cancels pending native I/O and waits for cancellation completion.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import threading
import time

k = C.WinDLL('kernel32', use_last_error=True)
INVALID = C.c_void_p(-1).value
PENDING = 997

class Overlapped(C.Structure):
    _fields_ = [('internal', C.c_size_t), ('internal_high', C.c_size_t),
                ('offset', W.DWORD), ('offset_high', W.DWORD), ('event', W.HANDLE)]

def bind(name, result, *args):
    f = getattr(k, name)
    f.restype = result
    f.argtypes = args
    return f

create_event = bind('CreateEventW', W.HANDLE, C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR)
set_event = bind('SetEvent', W.BOOL, W.HANDLE)
reset_event = bind('ResetEvent', W.BOOL, W.HANDLE)
close = bind('CloseHandle', W.BOOL, W.HANDLE)
wait = bind('WaitForMultipleObjects', W.DWORD, W.DWORD, C.POINTER(W.HANDLE), W.BOOL, W.DWORD)
create_pipe = bind('CreateNamedPipeW', W.HANDLE, W.LPCWSTR, W.DWORD, W.DWORD,
                   W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.c_void_p)
connect = bind('ConnectNamedPipe', W.BOOL, W.HANDLE, C.POINTER(Overlapped))
read = bind('ReadFile', W.BOOL, W.HANDLE, C.c_void_p, W.DWORD,
            C.POINTER(W.DWORD), C.POINTER(Overlapped))
cancel = bind('CancelIoEx', W.BOOL, W.HANDLE, C.POINTER(Overlapped))
complete = bind('GetOverlappedResult', W.BOOL, W.HANDLE, C.POINTER(Overlapped),
                C.POINTER(W.DWORD), W.BOOL)
client_pid = bind('GetNamedPipeClientProcessId', W.BOOL, W.HANDLE, C.POINTER(W.ULONG))

class Stopped(Exception): pass

class Reader:
    def __init__(self):
        self.stop = create_event(None, True, False, f'Local\\OpenRGB.Office.ChromaProbe.{os.getpid()}')
        self.io = create_event(None, True, False, None)
        self.pipe = create_pipe(r'\\.\pipe\UnifiedRgbChroma', 0x40080001,
                                0x8, 1, 0, 65536, 0, None)
        if self.pipe == INVALID:
            self.close()
            raise C.WinError(C.get_last_error())

    def close(self):
        for handle in (getattr(self, 'pipe', INVALID), self.io, self.stop):
            if handle and handle != INVALID: close(handle)

    def await_io(self, ov):
        handles = (W.HANDLE * 2)(self.io, self.stop)
        selected = wait(2, handles, False, 0xFFFFFFFF)
        count = W.DWORD()
        if selected == 1:
            cancel(self.pipe, C.byref(ov))
            complete(self.pipe, C.byref(ov), C.byref(count), True)
            raise Stopped()
        if selected != 0: raise C.WinError(C.get_last_error())
        if not complete(self.pipe, C.byref(ov), C.byref(count), False):
            raise C.WinError(C.get_last_error())
        return count.value

    def accept(self):
        reset_event(self.io)
        ov = Overlapped(event=self.io)
        if not connect(self.pipe, C.byref(ov)):
            error = C.get_last_error()
            if error == PENDING: self.await_io(ov)
            elif error != 535: raise C.WinError(error)
        pid = W.ULONG()
        if not client_pid(self.pipe, C.byref(pid)): raise C.WinError(C.get_last_error())
        return pid.value

    def exact(self, size):
        output = bytearray()
        while len(output) < size:
            reset_event(self.io)
            ov = Overlapped(event=self.io)
            buf = C.create_string_buffer(size - len(output))
            count = W.DWORD()
            if read(self.pipe, buf, len(buf), C.byref(count), C.byref(ov)):
                received = count.value
            elif C.get_last_error() == PENDING:
                received = self.await_io(ov)
            else: raise C.WinError(C.get_last_error())
            if not received: raise EOFError('Chroma producer disconnected')
            output.extend(buf.raw[:received])
        return bytes(output)

def summarize(groups, producer):
    report = {'producer_pid': producer, 'observer_pid': os.getpid(), 'types': {}}
    for kind, values in groups.items():
        times = values['times']
        gaps = [(b-a)*1000 for a, b in zip(times, times[1:])]
        report['types'][str(kind)] = {
            'frames': len(times), 'dimensions': sorted(values['dimensions']),
            'distinct_grids': len(values['hashes']),
            'nonblack_fraction_max': values['nonblack'],
            'fps': (len(times)-1)/(times[-1]-times[0]) if len(times)>1 else None,
            'interval_median_ms': statistics.median(gaps) if gaps else None,
            'interval_p95_ms': sorted(gaps)[min(len(gaps)-1, int(len(gaps)*.95))] if gaps else None,
            'interval_max_ms': max(gaps) if gaps else None,
        }
    return report

def observe(reader, frames, expected_pid, output):
    producer = reader.accept()
    if expected_pid and producer != expected_pid:
        raise RuntimeError(f'Unexpected producer PID {producer}; expected {expected_pid}')
    print(json.dumps({'connected_pid': producer}), flush=True)
    groups = {}
    for index in range(frames):
        kind, rows, cols = struct.unpack('<BHH', reader.exact(5))
        if kind not in (1, 2) or not (1 <= rows <= 8 and 1 <= cols <= 24):
            raise ValueError('Unsupported Chroma frame header')
        data = reader.exact(rows*cols*4)
        values = groups.setdefault(kind, {'times': [], 'dimensions': set(), 'hashes': set(), 'nonblack': 0})
        values['times'].append(time.perf_counter())
        values['dimensions'].add((rows, cols))
        values['hashes'].add(hashlib.sha256(data).digest())
        lit = sum(any(data[offset:offset+3]) for offset in range(0, len(data), 4))
        values['nonblack'] = max(values['nonblack'], lit/(rows*cols))
        if (index+1) % 60 == 0: print(json.dumps({'received_frames': index+1}), flush=True)
    report = summarize(groups, producer)
    if output:
        with output.open('x', encoding='utf-8') as target: json.dump(report, target, indent=2)
    print(json.dumps(report), flush=True)
    return report

def self_test(dll):
    # Exercise immediate and deferred SDK effects through the REAL built DLL
    # and the REAL named pipe. DLL effects do not access hardware.
    reader = Reader()
    result = []
    failures = []
    def serve():
        try: result.append(observe(reader, 3, os.getpid(), None))
        except BaseException as error: failures.append(error)
    worker = threading.Thread(target=serve)
    worker.start()
    try:
        sdk = C.CDLL(str(dll.resolve()))
        assert sdk.Init() == 0
        rgb = (W.DWORD * 132)(*[0x000000FF]*132)
        effect_id = C.create_string_buffer(16)
        assert sdk.CreateKeyboardEffect(2, C.byref(rgb), C.byref(effect_id)) == 0
        assert sdk.CreateKeyboardEffect(2, C.byref(rgb), None) == 0
        # GUID is a 16-byte value passed by value on the C API.
        class Guid(C.Structure): _fields_ = [('data', C.c_ubyte * 16)]
        sdk.SetEffect.argtypes = [Guid]
        assert sdk.SetEffect(Guid.from_buffer_copy(effect_id.raw)) == 0
        link = (W.DWORD * 5)(*[0x00FF0000]*5)
        assert sdk.CreateChromaLinkEffect(1, C.byref(link), None) == 0
        worker.join()
        if failures: raise failures[0]
        assert result[0]['types']['1']['frames'] == 2
        assert result[0]['types']['1']['dimensions'] == [(6,22)]
        assert result[0]['types']['2']['frames'] == 1
        print('ABI_AND_PIPE_SELF_TEST_PASS', flush=True)
    finally:
        set_event(reader.stop)
        worker.join()
        reader.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', type=Path)
    parser.add_argument('--frames', type=int, default=240)
    parser.add_argument('--producer-pid', type=int)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--stop-pid', type=int)
    args = parser.parse_args()
    if args.stop_pid:
        open_event = bind('OpenEventW', W.HANDLE, W.DWORD, W.BOOL, W.LPCWSTR)
        event = open_event(2, False, f'Local\\OpenRGB.Office.ChromaProbe.{args.stop_pid}')
        if not event: raise C.WinError(C.get_last_error())
        try:
            if not set_event(event): raise C.WinError(C.get_last_error())
        finally: close(event)
        return
    if args.self_test: return self_test(args.self_test)
    reader = Reader()
    print(json.dumps({'ready': True, 'pid': os.getpid(), 'cancel_event': f'Local\\OpenRGB.Office.ChromaProbe.{os.getpid()}'}), flush=True)
    try: observe(reader, args.frames, args.producer_pid, args.output)
    except Stopped: print(json.dumps({'stopped': True}), flush=True)
    finally: reader.close()

if __name__ == '__main__': main()
