import os
import serial
import datetime
import keyboard
import tkinter as tk
from threading import Thread

import TkinterGUI
#import asyncio


clock=0
#log_output=[0, 3]#log_output status and vertical pos
data_record=[]
portName="COM4"
baudRate=115200
ser=None
timeOut=2
command_external=""
log_time_enable=False
serial_send=True#按Shift+T通讯

file_rec_map={"lick":"lick_rec"+datetime.datetime.now().strftime("%Y_%m_%d %H_%M_%S")+".txt"}
os.system('')
print("recording......\nno connection\n\n")

class threads(Thread):
  def __init__(self, func, *args):
    Thread.__init__(self)
    self.func=func
    self.args=args
    #self.args=args
    self.result=None

  def run(self):
    self.func(self.args[0], self.args[1:])

def inputs(ser, *args):
  if len(args)==1:
    input_textbox=args[0][0]
    output_textbox=args[0][1]
  else:
    input_textbox=args[0]
    output_textbox=args[1]
  global serial_send

  def event_prase(event):
    return event.name, event.event_type

  if serial_send:
    temp_string = ""
    input_textbox.delete("1.0", tk.END) # 清空输入文本框内容
    input_textbox.insert(tk.END, "message will be sent: ")
    while True:
      temp_key, temp_key_status = event_prase(keyboard.read_event())
      if temp_key == "enter":
        if "c/" not in temp_string:
          ser.write((temp_string + '\n').encode('utf-8'))
          input_textbox.delete("1.0", tk.END)
          output_textbox.insert(tk.END, f"\nmessage sent: {temp_string}\n") # 在输出文本框添加发送的消息
          #log_output[1] += 1
        elif "c/" in temp_string:
          print("")
          command_external = temp_string
          external_command_prase(command_external)
        serial_send = False
        return
      else:
        if temp_key_status == "down":
          if temp_key in "abcdefghijklmnopqrstuvwxyz0123456789=[]":
            temp_string += temp_key
          else:
            if temp_key=="backspace":
              if len(temp_string):
                temp_string=temp_string[0:-1]

def formulate(_str):
  _str=str(_str)
  if _str[:2]=="b'":
    _str=_str[2:]
  if _str[len(_str)-5:]=="\\r\\n'":
    _str=_str[:len(_str)-5]
  return _str

def keyboard_check(hotkey=None, *args):#key:"shift+t" or "a"...
  global serial_send
  #添加热键对应行为
  if hotkey=="shift+t" and not serial_send:
    print("hotkey: shift+t")
    serial_send=True
    Serial_readThread=threads(inputs, ser, input_textbox, output_textbox)
    Serial_readThread.daemon=True
    Serial_readThread.start()
    return
  if hotkey=="shift+a":
    print("hotkey: shift+a")
    #inputs(ser, input_textbox, output_textbox)
    return
  elif hotkey=="others":#无意义
    pass
  else:
    pass

def external_command_prase(command):
  pass

#async def main(ser):
def Serial_read(ser, *args):
  output_textbox=args[0][0]
  while True:
    try:
      # write in serialport
      if ser.isOpen():
        temp_content = ser.readline()
        temp_content = formulate(temp_content)
        if temp_content != "" or "'":
          #data_record.append(temp_content)
          if temp_content[:3] == "log":
            pass
          elif temp_content[:8] == "received":
            pass
            output_textbox.see(tk.END) # 滚动到底部
          elif temp_content[:4] == "rec:":#"rec:lick:trail=n,lick_count=xend;"
            temp_rec_type=temp_content[4:temp_content.find(":", 4)]
            if temp_rec_type=="lick" and "end;" in temp_content:
              temp_content=(temp_content[temp_content.find(":", 4)+1:temp_content.find("end;")]).split(",")  #['trail=n', 'lick_count=x']
              with open(file_rec_map["lick"], "+a", encoding='utf-8') as f:
                f.write((temp_content[0].split("="))[1]+" "+(temp_content[1].split("="))[1]+"\n")
          if log_time_enable:
            temp_content=datetime.datetime.now().strftime("%H: %M: %S")+"-> "+temp_content
            output_textbox.insert(tk.END, temp_content + "\n")
            #print(temp_content)
            output_textbox.see(tk.END)
          else:
            output_textbox.insert(tk.END, temp_content + "\n")
            output_textbox.see(tk.END)

    except Exception as e:
      output_textbox.insert(tk.END, f"Error occurred in reading: {e}\n")
      output_textbox.see(tk.END)

#initial:
try:#set serialport parameters
  win=TkinterGUI.WinGUI()
  input_textbox=win.intput_textbox
  output_textbox=win.output_textbox
  #input_textbox.config(state="disabled")
  #output_textbox.config(state="disabled")

  # ser=serial.Serial(portName,baudRate,timeout=timeOut)

  # Serial_readThread=threads(Serial_read, ser, output_textbox)
  # Serial_readThread.daemon=True
  # Serial_readThread.start()

  Serial_readThread=threads(inputs, ser, input_textbox, output_textbox)
  Serial_readThread.daemon=True
  Serial_readThread.start()

except Exception as e:
  print("erros occured:",e)
  exit()

#start
keyboard.add_hotkey("shift+t", keyboard_check, args=["shift+t"])
keyboard.add_hotkey("shift+a", keyboard_check, args=["shift+a"])
win.mainloop()
