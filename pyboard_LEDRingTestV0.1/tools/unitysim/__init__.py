# unitysim — Unity 上位机行为模拟器（握手 / 任务启动 / 压测循环 / stv 索取）
#
# 日常 debug 示例：
#   from unitysim import BoardLink, UnitySimulator
#   link = BoardLink("COM7")
#   sim = UnitySimulator(link, trial_mode=0x02)
#   assert sim.connect()
#   sim.context_init_sync()
#   sim.start_stress()
#   sim.start_trial(0, pos=0)
#   import time; t0 = time.monotonic()
#   while time.monotonic() - t0 < 10:
#       sim.poll(0.05)
#   print(sim.trial_records)
from . import protocol
from .link import BoardLink, mono
from .sim import UnitySimulator

__all__ = ["protocol", "BoardLink", "mono", "UnitySimulator"]
