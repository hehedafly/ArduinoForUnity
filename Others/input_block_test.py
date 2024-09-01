import os
import keyboard
from threading import Thread
#import asyncio


clock=0
portName="COM4"
baudRate=115200
timeOut=2
command_external=""

os.system('')

class threads(Thread):
    def __init__(self, func, args):
        Thread.__init__(self)
        self.func=func
        self.args=args
        #self.args=args
        self.result=None

    def run(self):
        self.func(self.args)

def main():
    temp_key=keyboard.read_key()
    print(temp_key)
    pass
    
def inputs(arg=None):
    while True:
        result=input("message will be sent:")
        if type(result) is str and "c/" not in result:
            print("message sent:"+result)

inputThread=threads(inputs, None)
inputThread.daemon=True
inputThread.start()

while True:
    main()