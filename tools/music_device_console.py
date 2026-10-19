"""Shared physical P4 music-test console; importing never opens the port."""
import re,time
import serial

class Console:
    def __init__(self,log,port='COM4'):
        self.log=log;self.serial=serial.Serial(port=None,baudrate=115200,timeout=.08,write_timeout=3)
        self.serial.dtr=self.serial.rts=False;self.serial.port=port;self.serial.open()
    def collect(self,seconds):
        data=bytearray();end=time.monotonic()+seconds
        while time.monotonic()<end:
            block=self.serial.read(self.serial.in_waiting or 1)
            if block:data+=block
        text=data.decode(errors='replace').replace('\r','');self.log.write(text);self.log.flush()
        if re.search(r'PANIC|Assertion failed|(?:Load|Store) access fault|Unhandled exception',text,re.I):raise RuntimeError('Board runtime fault')
        return text
    def command(self,text,timeout=30):
        for _ in range(4):
            value=self._command_once(text,timeout)
            rejected=re.search(r'(?m)^nsh: [^:\n]+: command not found\s*$',value)
            if not rejected:return value
            self.log.write('CONSOLE_REJECTED_BEFORE_DISPATCH: retrying command\n');self.log.flush();self.collect(.3)
        raise RuntimeError('Console rejected command before dispatch: '+text)
    def _command_once(self,text,timeout):
        wire=text
        if text.startswith('desktop ui ') and ' ' in text[11:]:wire='desktop ui "'+text[11:]+'"'
        assert len(wire.encode())<79,wire
        self.collect(.05);self.serial.write(b'\x15');self.collect(.1);self.log.write('\nCOMMAND '+text+'\n')
        # NSH treats CR and LF as separate submissions; one CR avoids a stale
        # empty-command prompt being mistaken for the next command's result.
        # Keep CR in a USB packet with command bytes. One-byte writes can lose
        # the final CR while the board is logging, leaving NSH waiting forever.
        payload=(wire+'\r').encode()
        for offset in range(0,len(payload),32):
            self.serial.write(payload[offset:offset+32]);self.serial.flush();time.sleep(.02)
        value=''
        for phase in range(2):
            end=time.monotonic()+(timeout if not phase else min(timeout,5))
            while time.monotonic()<end:
                value+=self.collect(.12)
                if 'nsh>' in value or re.search(r'(?m)^(?:sh|ns)> \x1b\[K',value):return value
            if not phase:
                # USB/JTAG can lose the line terminator while audio logs are
                # arriving. Submit a pending line (or an empty line if the
                # command already ran), without replaying any command bytes.
                self.log.write('CONSOLE_PROMPT_RECOVERY: sending CR only; no command replay\n');self.log.flush()
                self.serial.write(b'\r');self.serial.flush()
        raise TimeoutError(text)
    def close(self):self.serial.close()
