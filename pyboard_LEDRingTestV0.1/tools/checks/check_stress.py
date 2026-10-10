# checks/check_stress.py — autoStressTest 完整验收（引用 unitysim，不含模拟逻辑）
# 用法: <hex-prism python> check_stress.py --port COM7 --mode 0x02 --trials 5
#
# 断言（对标 Arduino V2_5 + Unity LickingCheck）：
#   1. 每 trial 恰好 3 帧 li:-4:1；首帧距 2=1 echo 约 500ms(±120ms)，后续间隔约 50ms(±80ms)
#   2. 回写序列符合 trialMode 语义（mode>>4==2: 第1个-4在%16==1时回2=2；第2个-4在%16==2时回2=2 等）
#   3. 每 trial 的 +1s statusValue 均收到 stv 帧（含 0=~10= 与 a0~a2 段）
#   4. autoStressTestStop 后新 trial 不再出现 li:-4
#   5. 全程 CommandVerify 回显无缺失
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from unitysim import BoardLink, UnitySimulator

FAILS = []
FIRST_DELAY = (0.38, 0.62)     # 500ms ± 120ms
SPACING = (-0.03, 0.13)        # 50ms ± 80ms


def check(ok, name, detail=""):
    print("[{0}] {1}{2}".format("PASS" if ok else "FAIL", name, ("  | " + detail) if detail else ""))
    if not ok:
        FAILS.append(name)


def expected_writes(mode):
    m = mode >> 4
    if m == 0:
        return []
    if m == 1:
        return ["2=2"] if mode == 0x11 else ["2=0"]
    if mode % 0x10 == 1:
        return ["2=2"]
    if mode % 0x10 == 2:
        return ["2=2"]
    return []


def main():
    ap = argparse.ArgumentParser(description="pyboard autoStressTest 完整验收")
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mode", type=lambda s: int(s, 0), default=0x00, help="trialMode，如 0x00/0x11/0x02")
    ap.add_argument("--trials", type=int, default=5)
    ap.add_argument("--interval", type=float, default=1.0, help="mode>>4==0/1 时自动下一 trial 间隔(s)")
    args = ap.parse_args()

    link = BoardLink(args.port, args.baud)
    try:
        sim = UnitySimulator(link, trial_mode=args.mode, trial_interval=args.interval)
        check(sim.connect(), "握手")
        check(sim.context_init_sync(), "ContextInitSync（clear + 0=/1= 回显）")
        sim.start_stress()
        check(sim.start_trial(0, pos=0), "trial 0 ContextStartSync（1=/3=/2=1 回显）")

        # 跑满 N 轮：每轮记录需凑齐 3 个 -4
        deadline = time.monotonic() + args.trials * (args.interval + 6) + 20
        while time.monotonic() < deadline:
            sim.poll(0.05)
            recs = sim.trial_records[:args.trials]
            if len(recs) >= args.trials and all(
                    sum(1 for _, i, r in x["licks"] if i == -4) == 3 for x in recs):
                break
        recs = sim.trial_records[:args.trials]
        check(len(recs) >= args.trials, "trial 推进到 {0} 轮不卡死".format(args.trials),
              "got {0}".format(len(recs)))

        for x in recs:
            t4 = [t for t, i, r in x["licks"] if i == -4 and r == 1]
            tag = "trial {0}".format(x["trial"])
            check(len(t4) == 3, tag + ": 恰好 3 帧 li:-4:1", "got {0}".format(len(t4)))
            if len(t4) == 3 and x["t_set"]:
                d0 = t4[0] - x["t_set"]
                d1 = t4[1] - t4[0]
                d2 = t4[2] - t4[1]
                check(FIRST_DELAY[0] <= d0 <= FIRST_DELAY[1], tag + ": 首帧距 2=1 约 500ms",
                      "{0:.0f}ms".format(d0 * 1000))
                check(SPACING[0] <= d1 <= SPACING[1] and SPACING[0] <= d2 <= SPACING[1],
                      tag + ": 后续间隔约 50ms", "{0:.0f}/{1:.0f}ms".format(d1 * 1000, d2 * 1000))
            check(x["writes"] == expected_writes(args.mode), tag + ": 板端回写序列符合 mode 语义",
                  "got {0} want {1}".format(x["writes"], expected_writes(args.mode)))

        stv_ok_pre = sum(1 for _, k, _ in sim.events if k == "stv_ok")
        # 主循环 break 可能早于最后一 trial 的 +1s statusValue 截止：补一个尾窗让其完成
        end = time.monotonic() + 1.6
        while time.monotonic() < end:
            sim.poll(0.05)
        stv_ok = sum(1 for _, k, _ in sim.events if k == "stv_ok")
        check(stv_ok >= args.trials, "每 trial +1s 的 statusValue 均回 stv",
              "got {0} (break 时 {1})".format(stv_ok, stv_ok_pre))
        last_stv = sim.status_backup or ""
        check(all(k in last_stv for k in ("0=", "10=", "a0=", "a1=", "a2=")),
              "stv 体含 0=~10= 与 a0~a2 段", last_stv[:60])

        # 停止压测：新 trial 不应再出现 li:-4
        sim.stop_stress()
        sim.start_trial(args.trials, pos=0)
        end = time.monotonic() + 2.5
        while time.monotonic() < end:
            sim.poll(0.05)
        rec = sim.trial_records[-1]
        n4 = sum(1 for _, i, r in rec["licks"] if i == -4)
        check(n4 == 0, "autoStressTestStop 后新 trial 无 li:-4", "got {0}".format(n4))

        miss = [d for _, k, d in sim.events if k == "echo_miss"]
        check(not miss, "全程 echo 校验无缺失", "miss={0}".format(miss[:3]))
    finally:
        link.close()

    print("\n{0}".format("ALL PASS" if not FAILS else "FAILED: {0}".format(FAILS)))
    sys.exit(0 if not FAILS else 1)


if __name__ == "__main__":
    main()
