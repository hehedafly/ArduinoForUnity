# test_pulse_current.py - reproduce current main.py one-pulse, measure X1 timing
from pyb import Pin, Timer
from utime import ticks_us, ticks_diff

PIN = 'X1'
p = Pin(PIN, Pin.OUT_PP)
t = Timer(5, freq=1000)                 # current: 1kHz
c = t.channel(1, Timer.PWM, pin=p)
c.pulse_width_percent(0)

def measure(tag):
    c.pulse_width(10000)                # target 10ms high pulse
    t0 = ticks_us()
    transitions = []
    prev = p.value()
    while ticks_diff(ticks_us(), t0) < 30000:
        v = p.value()
        if v != prev:
            transitions.append(ticks_diff(ticks_us(), t0))
            prev = v
    print(tag, 'transitions(us)=', transitions[:12], 'n=', len(transitions), 'final=', prev)
    c.pulse_width_percent(0)

measure('current freq=1000 pw=10000:')
