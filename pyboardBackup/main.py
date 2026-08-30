'''
实验名称：综合测试程序
版本：v1.0
日期：2019.4
作者：01Studio
说明：开发板测试程序
'''

#导入相关模块
import pyb
from machine import Pin,I2C
from ssd1306 import SSD1306_I2C
from onewire import OneWire
from ds18x20 import DS18X20
from dht import DHT11

#############################################
# 初始化相关模块
#############################################

#初始化OLED
i2c = I2C(sda=Pin("Y8"), scl=Pin("Y6"))
oled = SSD1306_I2C(128, 64, i2c)

#初始化按键
sw = pyb.Switch()     #定义按键对象名字为sw

#初始化内部三轴加速度传感器
accel = pyb.Accel()

#初始化ADC,Pin='X7'
adc = pyb.ADC('X7')

#初始化DAC,输出引脚为'X5'
dac = pyb.DAC(1)
# 定义8位精度下方波的值。0、255分别对应输出0V、3.3V。需要定义成字节数组。
buf = bytearray(2)
buf[0]=0
buf[1]=255

#初始化DS18B20
ow= OneWire(Pin('X11')) #使能单总线
ds = DS18X20(ow)        #传感器是DS18B20
rom = ds.scan()         #扫描单总线上的传感器地址，支持多个传感器同时连接

#初始化DHT11
dt=DHT11(Pin('X12')) #'X20'连接到开发板上的DHT11


key_node = 0  #按键标志位
f = 1         #用于测试项目切换

##############################################
#  按键和其回调函数
##############################################
def key():
    global key_node
    key_node = 1

sw.callback(key)  #当按键被按下时，执行函数key()

##############################################
#  OLED初始显示
##############################################
oled.fill(0)  # 清屏显示黑色背景
oled.text('01Studio Test', 0, 0)  # 首行显示01Studio
oled.text('Pls Press USER', 0, 40)  # 显示当前频率
oled.show()

while True:

    if key_node==1: #按键被按下
        f = f+1
        if f == 7:
            f = 1
        key_node = 0 #清空按键标志位

    if f == 1: #LED测试

        #显示信息
        oled.fill(0)  # 清屏显示黑色背景
        oled.text(str(f)+': '+'LEDS'+' Test', 0, 0)  # 次行显示实验名称
        oled.text('Pls Press USER', 0, 55)  # 按键提示
        oled.show()

        # 使用for循环
        for i in range(1, 5):
            pyb.LED(i).on()
            pyb.delay(1000)  # 延时1000毫秒，即1秒
            pyb.LED(i).off()

    if f == 2: #内部加速度计测试

        #显示信息
        oled.fill(0)  # 清屏显示黑色背景
        oled.text(str(f)+': '+'Accel'+' Test', 0, 0)  # 次行显示实验名称
        oled.text('Pls Press USER', 0, 55)  # 按键提示

        # 获取x,y,z的值并显示
        oled.text('X:' + str(accel.x()), 0, 20)
        oled.text('Y:' + str(accel.y()), 44, 20)
        oled.text('Z:' + str(accel.z()), 88, 20)
        oled.show()

        pyb.delay(1000)  # 延时1s

    if f==3: #ADC

        #显示信息
        oled.fill(0)  # 清屏显示黑色背景
        oled.text(str(f)+': '+'ADC'+' Test', 0, 0)  # 次行显示实验名称
        oled.text('Pls Press USER', 0, 55)  # 按键提示

        # 获取ADC数值
        oled.text(str(adc.read()), 0, 20)
        oled.text('(4095)', 40, 20)
        # 计算电压值，获得的数据0-4095相当于0-3V，（'%.2f'%）表示保留2位小数
        oled.text(str('%.2f' % (adc.read() / 4095 * 3.3))+' V', 0, 35)  # oled.text('V',40,55)

        oled.show()
        pyb.delay(1000)

    if f==4: #DAC

        #显示信息
        oled.fill(0)  # 清屏显示黑色背景
        oled.text(str(f)+': '+'DAC'+' Test', 0, 0)  # 次行显示实验名称
        oled.text('Pls Press USER', 0, 55)  # 按键提示

        dac.write_timed(buf, 200* len(buf), mode=pyb.DAC.CIRCULAR)
        oled.text('200Hz', 0, 20)  # 显示当前频率
        oled.show()

        pyb.delay(1000)

    if f==5: #温度传感器DS18B20

        #关闭蜂鸣器
        dac.write(0)

        #显示信息
        oled.fill(0)  # 清屏显示黑色背景
        oled.text(str(f)+': '+'DS18B20'+' Test', 0, 0)  # 次行显示实验名称
        oled.text('Pls Press USER', 0, 55)  # 按键提示

        ds.convert_temp()  # 温度采集转换
        temp = ds.read_temp(rom[0])  # 温度显示,rom[0]为第1个DS18B20

        oled.text(str('%.2f' % temp) + ' C', 0, 20)  # 显示temp,保留2位小数
        oled.show()

        pyb.delay(1000)

    if f==6: #温湿度传感器DHT11

        #显示信息
        oled.fill(0)  # 清屏显示黑色背景
        oled.text(str(f)+': '+'DHT11'+' Test', 0, 0)  # 次行显示实验名称
        oled.text('Pls Press USER', 0, 55)  # 按键提示

        dt.measure()  # 温湿度采集
        te = dt.temperature()  # 获取温度值
        dh = dt.humidity()  # 获取湿度值

        # 温度显示
        oled.text(str(te) + ' C', 0, 20)
        # 湿度显示
        oled.text(str(dh) + ' %', 48, 20)

        oled.show()

        pyb.delay(1000)
