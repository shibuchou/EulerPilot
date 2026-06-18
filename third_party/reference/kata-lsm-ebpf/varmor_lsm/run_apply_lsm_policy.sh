#!/usr/bin/env bash
set -euo pipefail

NS="${NS:-default}"
POD="${POD:-test-lsm-pod}"
MODE="${MODE:-enforce}"
BPF_OBJ="${BPF_OBJ:-./varmor_lsm.bpf.o}"
APPLY_BIN="${APPLY_BIN:-./apply_lsm_policy}"

if [[ "$(id -u)" != "0" ]]; then
  echo "请在 Pod 所在节点上用 root 运行：sudo $0"
  exit 1
fi

if [[ ! -f "$BPF_OBJ" ]]; then
  echo "BPF object 不存在: $BPF_OBJ"
  exit 1
fi

if [[ ! -x "$APPLY_BIN" ]]; then
  echo "apply binary 不存在或不可执行: $APPLY_BIN"
  exit 1
fi

if [[ "$MODE" != "enforce" && "$MODE" != "complain" ]]; then
  echo "MODE 只能是 enforce 或 complain"
  exit 1
fi

echo "[+] namespace=$NS pod=$POD mode=$MODE"

if ! command -v crictl >/dev/null 2>&1; then
  echo "未找到 crictl，请在节点上安装 crictl，或改成 docker inspect 方式取 PID"
  exit 1
fi

POD_ID="$(crictl pods --namespace "$NS" --name "^${POD}$" -q | head -n1 || true)"
if [[ -z "$POD_ID" ]]; then
  echo "未找到 Pod: $NS/$POD"
  exit 1
fi

CID="$(crictl ps --pod "$POD_ID" -q | head -n1 || true)"
if [[ -z "$CID" ]]; then
  echo "未找到 Pod 的业务容器，Pod_ID=$POD_ID"
  exit 1
fi

PID="$(crictl inspect -o json "$CID" | jq -r '.info.pid // .status.pid // empty')"
if [[ -z "$PID" || "$PID" == "0" ]]; then
  echo "无法从 crictl inspect 获取容器宿主机 PID，CID=$CID"
  exit 1
fi

MNTNS="$(readlink "/proc/$PID/ns/mnt" | sed -E 's/.*\[([0-9]+)\].*/\1/')"

echo "[+] pod_id=$POD_ID"
echo "[+] container_id=$CID"
echo "[+] host_pid=$PID"
echo "[+] mnt_ns=$MNTNS"

if [[ ! -d /sys/fs/bpf ]]; then
  mkdir -p /sys/fs/bpf
fi

if ! mount | grep -q ' /sys/fs/bpf '; then
  echo "[+] mounting bpffs"
  mount -t bpf bpffs /sys/fs/bpf
fi

if [[ -r /sys/kernel/security/lsm ]]; then
  if ! tr ',' '\n' < /sys/kernel/security/lsm | grep -qx bpf; then
    echo "当前内核 LSM 列表里没有 bpf："
    cat /sys/kernel/security/lsm
    echo "需要启用 BPF LSM，例如内核启动参数 lsm=...,bpf"
    exit 1
  fi
else
  echo "无法读取 /sys/kernel/security/lsm，继续尝试加载"
fi

exec "$APPLY_BIN" "$BPF_OBJ" "$PID" "$MODE"