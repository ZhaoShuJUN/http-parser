#!/bin/sh
# GC6511 Buildroot 前台守护示例，异常退出后 5 秒重启。
# 修改下面三个变量后，将脚本与 iot_gateway 放到 /home/htnice/app。
APP=/home/htnice/app/iot_gateway
SERIAL=/dev/ttyS1
AUTH_IP=101.33.253.45
AUTH_PORT=23106
BAUD=115200
trap 'exit 0' INT TERM
while : ; do
    "$APP" "$SERIAL" "$BAUD" "$AUTH_IP" "$AUTH_PORT"
    code=$?
    echo "iot_gateway exited ($code), retry in 5 seconds" >&2
    sleep 5
done
