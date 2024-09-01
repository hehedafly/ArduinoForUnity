from threading import Thread
import TkinterGUI

temp_str=""
def run(str):
    print(str)

win=TkinterGUI.WinGUI()
input_textbox=win.intput_textbox
output_textbox=win.output_textbox

def main():
    win.mainloop()

main()