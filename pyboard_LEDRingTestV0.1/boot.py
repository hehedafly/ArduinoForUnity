import pyb
import os
os.dupterm(pyb.UART(6, 115200))