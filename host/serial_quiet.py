"""Opt-in macOS Serial/JTAG experiment. Never asserts reset/download lines.
Deasserts both modem lines in one ioctl; verify target boot-id across reopening.
"""
import fcntl
import struct
import sys
import termios
from serial.serialposix import Serial


class QuietSerial(Serial):
    def _reconfigure_port(self, force_update=False):
        super()._reconfigure_port(force_update)
        attr=termios.tcgetattr(self.fd)
        if attr[2]&termios.HUPCL:
            attr[2]&=~termios.HUPCL
            termios.tcsetattr(self.fd,termios.TCSANOW,attr)

    def _update_dtr_state(self):
        if self._dtr_state or self._rts_state:
            raise ValueError('Quiet transport never asserts DTR or RTS')
        fcntl.ioctl(self.fd,termios.TIOCMBIC,struct.pack('I',termios.TIOCM_DTR|termios.TIOCM_RTS))

    def _update_rts_state(self):
        self._update_dtr_state()


def open_quiet_serial(port):
    if sys.platform!='darwin':raise RuntimeError('macOS-only experiment')
    link=QuietSerial(port=None,baudrate=115200,timeout=.1,write_timeout=2,exclusive=True)
    link.dtr=False;link.rts=False;link.port=port;link.open()
    return link
