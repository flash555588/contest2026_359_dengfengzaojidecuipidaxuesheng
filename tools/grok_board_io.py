"""Physical USB console helper. Pairing replies are deliberately not logged."""
import re,time,serial
class Console:
    def __init__(self,port='COM4',log=None):
        self.serial=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=3)
        self.serial.dtr=self.serial.rts=False;self.serial.port=port;self.serial.open();self.log=log
    def close(self):self.serial.close()
    def command(self,text,timeout=20,private=False):
        if text.startswith('desktop ui ') and ' ' in text[11:]:text='desktop ui "'+text[11:]+'"'
        time.sleep(.1);self.serial.reset_input_buffer();encoded=(text+'\r').encode('utf-8')
        for i in range(0,len(encoded),8):self.serial.write(encoded[i:i+8]);self.serial.flush();time.sleep(.025)
        data=bytearray();began=received=time.monotonic()
        while time.monotonic()-began<timeout:
            chunk=self.serial.read(self.serial.in_waiting or 1)
            if chunk:data.extend(chunk);received=time.monotonic()
            if b'nsh>' in data and time.monotonic()-received>.25:break
        else:raise RuntimeError('Console timed out: '+text)
        value=data.decode('utf-8',errors='replace').replace('\r','')
        if self.log and not private:self.log.write('\nCOMMAND '+text+'\n'+value);self.log.flush()
        if re.search(r'Assertion|PANIC|Guru Meditation|Segmentation fault',value,re.I):raise RuntimeError('Board runtime fault')
        return value
