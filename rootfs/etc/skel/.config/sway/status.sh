#!/bin/sh
# Barre d'état ultra-légère : wifi | volume | batterie | date (rafraîchie toutes les 5 s)
while :; do
	wifi=""
	for i in /sys/class/net/*/wireless; do
		[ -e "$i" ] || continue
		dev=$(basename "$(dirname "$i")")
		[ "$(cat "/sys/class/net/$dev/operstate")" = up ] && wifi="wifi ✓" || wifi="wifi ✗"
	done
	vol=$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null | awk '{printf "vol %d%%", $2*100} /MUTED/ {printf " (muet)"}')
	bat=""
	for b in /sys/class/power_supply/BAT*; do
		[ -e "$b/capacity" ] && bat="bat $(cat "$b/capacity")% $(cat "$b/status")"
	done
	kb=$(swaymsg -t get_inputs 2>/dev/null | grep -m1 xkb_active_layout_name | sed 's/.*: "//; s/".*//')
	case "$kb" in French*) kb=AZERTY ;; English*) kb=QWERTY ;; esac
	case "$(ibus engine 2>/dev/null)" in libpinyin*) kb="拼音" ;; esac
	echo "$kb | $wifi | $vol | $bat | $(date '+%a %d %b %H:%M')"
	sleep 5
done
