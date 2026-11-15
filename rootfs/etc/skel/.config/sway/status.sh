#!/bin/sh
# Barre d'état ultra-légère : clavier | wifi | volume | batterie | date (toutes les 5 s)
JOURS="dim lun mar mer jeu ven sam"
MOIS="janv févr mars avr mai juin juil août sept oct nov déc"
nth() { echo "$2" | cut -d' ' -f"$1"; }

while :; do
	parts=""
	add() { [ -n "$1" ] && parts="${parts:+$parts  |  }$1"; }

	kb=$(swaymsg -t get_inputs 2>/dev/null | grep -m1 xkb_active_layout_name | sed 's/.*: "//; s/".*//')
	case "$kb" in French*) kb=AZERTY ;; English*) kb=QWERTY ;; esac
	case "$(ibus engine 2>/dev/null)" in libpinyin*) kb="拼音" ;; esac
	add "$kb"

	for i in /sys/class/net/*/wireless; do
		[ -e "$i" ] || continue
		dev=$(basename "$(dirname "$i")")
		[ "$(cat "/sys/class/net/$dev/operstate")" = up ] && add "wifi ✓" || add "wifi ✗"
	done

	add "$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null | awk '{printf "vol %d%%", $2*100} /MUTED/ {printf " (muet)"}')"

	for b in /sys/class/power_supply/BAT*; do
		[ -e "$b/capacity" ] || continue
		case "$(cat "$b/status")" in Charging|Full) st="⚡" ;; *) st="" ;; esac
		add "bat $(cat "$b/capacity")%$st"
	done

	add "$(nth "$(( $(date +%w) + 1 ))" "$JOURS") $(date +%d) $(nth "$(date +%-m)" "$MOIS") $(date +%H:%M)"
	echo "$parts"
	sleep 5
done
