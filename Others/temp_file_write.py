import time
temp_count=0
with open("test.txt", mode='a') as f:
    while True:
        f.write("temp_count"+str(temp_count)+'\n')
        temp_count+=1
        print(temp_count)
        time.sleep(0.01)
        if temp_count>=32767:
            break