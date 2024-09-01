import os
import time
import numpy as np
from matplotlib import pyplot as plt
temp_buffer=''
buffer_impair=0
record_count=0
plt.switch_backend('agg')

def Read_Folder():
    """
    读取文件夹下所有文件
    """
    f_path="F:\VR_pbk\VRrun\VR_W_CTXD/run_files"
    files = os.listdir(f_path)
    files.sort(key=lambda fn: os.path.getmtime(f_path+'/'+fn), reverse=True)
    for file_name in files:
        if file_name[0]=='P':
            return(f_path+"/"+file_name)

temp_file=Read_Folder()
if not os.path.exists(temp_file):
    exit()
xx_max=10000
xx=range(0, xx_max, 1)
xx_label=0
yy=[]
fig, ax = plt.subplots(figsize=(24, 8))
pos_line, = ax.plot(xx[0:len(yy)], yy, lw=2)
read_sep=[0.002, 0.01]
draw_sep=[int(0.1/read_sep[0]), 0]
plt.show()
with open(temp_file, mode='r') as f:
    while True:
        try:
            temp_content=f.read()
            if len(temp_content):
                if buffer_impair:
                    temp_buffer=temp_buffer+temp_content[:temp_content.index('\n')+1]
                    temp_content=temp_buffer+temp_content[temp_content.index('\n')+1:]
                temp_buffer=temp_content[temp_content.rindex('\n')+1:]
                buffer_impair=1 if len(temp_buffer) else 0
                
                temp_content=(temp_content[:temp_content.rindex('\n')]).split('\n')
                if len(temp_content):
                    for line in temp_content:
                        if len(yy)>=xx_max*0.8:
                            del yy[0]
                            xx_label+=1
                        yy.append(int((line.split(' '))[1]))
                        draw_sep[1]+=1
                        if draw_sep[1]==draw_sep[0]:
                            draw_sep[1]=0
                            pos_line.set_data(xx[0:len(yy)], yy)
                            plt.pause(read_sep[0])
                        #if xx_label
                        print('\n'+line+" --"+str(record_count), end='')
                        record_count+=1
                #else:
                    #print("<blank>", end='')
                    #time.sleep(0.01)
                #plt.pause(read_sep[0])
                #time.sleep(read_sep[0])
            else:
                time.sleep(read_sep[1])
        except(Exception):
            print (Exception.__name__)
            #time.sleep(0.01)
            continue
