import sys
from pathlib import Path
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from serial_quiet import QuietSerial
import struct
import termios


class QuietTransportTest(unittest.TestCase):
    def test_lines_deasserted_together(self):
        port=QuietSerial(port=None);port.fd=12;port._dtr_state=False;port._rts_state=False
        with patch('serial_quiet.fcntl.ioctl') as ioctl:
            port._update_dtr_state()
            ioctl.assert_called_once_with(12,termios.TIOCMBIC,struct.pack('I',termios.TIOCM_DTR|termios.TIOCM_RTS))

    def test_assertion_is_rejected(self):
        port=QuietSerial(port=None);port._dtr_state=True;port._rts_state=False
        with patch('serial_quiet.fcntl.ioctl') as ioctl:
            with self.assertRaises(ValueError):port._update_dtr_state()
            ioctl.assert_not_called()


if __name__=='__main__':unittest.main()
