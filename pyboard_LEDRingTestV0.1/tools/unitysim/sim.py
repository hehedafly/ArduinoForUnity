# unitysim.sim — 复刻 Unity(Moving.cs) 与板子交互的关键行为：
#   握手(banner×2/initialed/ACK/ACK_OK)、forceinit 重连、ContextInitSync/ContextStartSync、
#   StartTrial 后 +1s 索取 stv 快照、li:-4 压测语义(按 trialMode 分派回写与自动下一 trial)。
# 本模块只做"模拟 Unity"，检测断言放在 tools/checks/ 下的脚本里。
from .link import mono
from . import protocol


class UnitySimulator:
    def __init__(self, link, trial_mode=0x00, trial_interval=1.0, status_req_delay=1.0):
        self.link = link
        self.trial_mode = trial_mode          # 对标 Unity trialMode（>>4 为判定模式）
        self.trial_interval = trial_interval  # mode>>4==0/1 时自动下一 trial 的间隔(s)
        self.status_req_delay = status_req_delay
        self.device_type = None
        self.version = None
        self.banners = 0
        self.status_backup = None             # 最近一次 stv 体(str)
        self.events = []                      # (t, kind, data) 全量事件流，供检测脚本断言
        self.trial_records = []               # 每 trial 一条: dict(trial,t_set,licks,writes,stv)
        self.trial_index = -1
        self._stash = []                      # 等待期间收到、尚未分派的消息
        self._next_trial_at = None
        self._status_due = []                 # statusValue 索取到期时间列表

    # ================= 内部工具 =================
    def _log(self, kind, data=""):
        self.events.append((mono(), kind, data))

    def _wait_msg(self, pred, timeout):
        """从 stash/串口流中等待第一条满足 pred 的 msg；其余消息入 stash 留给 poll。"""
        end = mono() + timeout
        while mono() < end:
            for i, m in enumerate(self._stash):
                if pred(m):
                    return self._stash.pop(i)
            got = self.link.read_msgs(0.05)
            for m in got:
                if pred(m):
                    return m
                self._stash.append(m)
        return None

    def _cur_rec(self):
        return self.trial_records[-1] if self.trial_records else None

    # ================= 握手 / 连接 =================
    def connect(self, timeout=8.0):
        """复刻 CreateSerialConnection：发 //forceinit → 收 banner×2 + initialed → 发 ACK → 等 ACK_OK。"""
        self.link.send_text("forceinit")
        banners = 0
        version = None
        end = mono() + timeout
        while mono() < end:
            for m in self.link.read_msgs(0.1):
                self._log("rx", m)
                if m[0] == "line":
                    if m[1] == protocol.BOARD_BANNER:
                        banners += 1
                    elif m[1].startswith("initialed:"):
                        version = m[1].split(":", 1)[1]
                        self.link.send_line("ACK")
                    elif m[1] == "ACK_OK":
                        self.banners = banners
                        self.version = version
                        self.device_type = "Pyboard" if banners >= 1 else "Arduino"
                        self._log("handshake_ok", "banners={0} version={1}".format(banners, version))
                        return True
        self._log("handshake_fail", "banners={0}".format(banners))
        return False

    # ================= 变量/命令发送（CommandVerify 简化：发一帧等一回显） =================
    def set_var(self, idx, val, timeout=1.5):
        cmd = "{0}={1}".format(idx, val)
        self.link.send_frame("cmd", cmd)
        want = "echo:{0}:echo".format(cmd)
        ok = self._wait_msg(lambda m: m[0] == "line" and m[1] == want, timeout) is not None
        self._log("echo_ok" if ok else "echo_miss", cmd)
        if ok and idx == 2 and val == 1:
            rec = self._cur_rec()
            if rec is not None:
                rec["t_set"] = mono()
        return ok

    def set_arr(self, arr_idx, i, val, timeout=1.5):
        cmd = "{0}[{1}]={2}".format(arr_idx, i, val)
        self.link.send_frame("cmd", cmd)
        want = "echo:{0}:echo".format(cmd)
        ok = self._wait_msg(lambda m: m[0] == "line" and m[1] == want, timeout) is not None
        self._log("echo_ok" if ok else "echo_miss", cmd)
        return ok

    def send_cmd(self, kw):
        """关键字命令（clear/autoStressTest/statusValue 等），无回显。"""
        self.link.send_frame("cmd", kw)
        self._log("tx_cmd", kw)

    # ================= 会话 / trial 流程 =================
    def context_init_sync(self, lick_mode=0, trial=0):
        """复刻 ContextInitSync：clear + 写 0=lick_mode、1=trial。"""
        self.send_cmd("clear")
        self.link.read_msgs(0.3)   # 等 clear 处理完（板端无回显）
        ok1 = self.set_var(0, lick_mode)
        ok2 = self.set_var(1, trial)
        return ok1 and ok2

    def start_trial(self, trial, pos=0):
        """复刻 ContextStartSync：1=trial;3=pos;2=1；并登记 +1s 的 statusValue 索取。"""
        self.trial_index = trial
        self.trial_records.append({"trial": trial, "t_set": None, "licks": [], "writes": [], "stv": None})
        self._log("trial_start", trial)
        ok = self.set_var(1, trial)
        ok = self.set_var(3, pos) and ok
        ok = self.set_var(2, 1) and ok
        self._status_due.append(mono() + self.status_req_delay)
        return ok

    def request_status_value(self, timeout=2.0):
        self.send_cmd("statusValue")
        m = self._wait_msg(lambda x: x[0] == "frame" and x[1] == 7 and x[2].startswith("stv:"), timeout)
        if m is not None:
            self.status_backup = m[2]
            rec = self._cur_rec()
            if rec is not None:
                rec["stv"] = m[2]
            self._log("stv_ok", m[2][:40])
            return m[2]
        self._log("stv_miss")
        return None

    def start_stress(self):
        self.send_cmd("autoStressTest")

    def stop_stress(self):
        self.send_cmd("autoStressTestStop")

    # ================= 接收分派 =================
    def poll(self, seconds=0.0):
        """分派 stash + 当前可读消息；处理 statusValue 到期与自动下一 trial。返回分派条数。"""
        msgs = self._stash
        self._stash = []
        msgs.extend(self.link.read_msgs(seconds))
        for m in msgs:
            self._dispatch(m)
        now = mono()
        due = [t for t in self._status_due if t <= now]
        if due:
            self._status_due = [t for t in self._status_due if t > now]
            self.request_status_value()
        if self._next_trial_at is not None and now >= self._next_trial_at:
            self._next_trial_at = None
            self.start_trial(self.trial_index + 1, pos=self._next_pos())
        return len(msgs)

    def _next_pos(self):
        return 0

    def _dispatch(self, m):
        self._log("rx", m)
        if m[0] == "frame" and m[1] == 0:     # li
            self._on_lick(m[2], m[3] if len(m) > 3 else mono())

    def _on_lick(self, content, t_rx):
        """content 形如 '<lickInd>:<reach>'；t_rx 为链路层到达时间戳；仅复刻 lickInd==-4 的压测路径。"""
        rec = self._cur_rec()
        try:
            ind, reach = content.split(":")
            ind, reach = int(ind), int(reach)
        except ValueError:
            return
        if rec is not None:
            rec["licks"].append((t_rx, ind, reach))
        if ind != -4 or reach != 1:
            return
        if rec is None:
            return   # 无当前 trial 记录（如 trial 启动前的残留帧），只记事件不驱动
        mode = self.trial_mode >> 4
        n_lick = sum(1 for t, i, r in rec["licks"] if i == -4) if rec else 1
        if mode == 0:
            # 不回写；interval 后自动下一 trial（SetTrialAutomaticWhenStressTest 近似）
            if n_lick == 1:
                self._next_trial_at = mono() + self.trial_interval
        elif mode == 1:
            if n_lick == 1:
                if self.trial_mode == 0x11:
                    self._write_var(2, 2)
                else:
                    self._write_var(2, 0)
                self._next_trial_at = mono() + self.trial_interval
        else:  # mode>>4 == 2 位置模式
            if n_lick == 1:
                if self.trial_mode % 0x10 == 1:
                    self._write_var(2, 2)
                rec["arrived"] = True
            elif n_lick == 2:
                if self.trial_mode % 0x10 == 2:
                    self._write_var(2, 2)
                self._next_trial_at = mono() + 2.0

    def _write_var(self, idx, val):
        rec = self._cur_rec()
        if rec is not None:
            rec["writes"].append("{0}={1}".format(idx, val))
        self.set_var(idx, val)
