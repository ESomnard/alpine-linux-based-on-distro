#!/bin/sh
# Bascule entre la saisie normale (AZERTY/QWERTY de sway) et le chinois pinyin (IBus).
if ibus engine 2>/dev/null | grep -q '^libpinyin'; then
	ibus engine xkb:us::eng && notify-send -t 1500 "Clavier" "Saisie normale"
else
	ibus engine libpinyin && notify-send -t 1500 "Clavier" "拼音 Pinyin"
fi
