#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Decode two usbmon text captures into their ordered control/interface init
# sequences and show them side by side, so we can see exactly where our
# module's init diverges from the working DMJC fork's.
#
#   scripts/diff-usb-init.sh <fork-usbmon.txt> <our-usbmon.txt>
#   e.g. scripts/diff-usb-init.sh /tmp/fork-usbmon.txt /tmp/our-usbmon.txt
set -u

A="${1:-/tmp/fork-usbmon.txt}"
B="${2:-/tmp/our-usbmon.txt}"
[ -f "$A" ] || { echo "error: $A not found" >&2; exit 1; }
[ -f "$B" ] || { echo "error: $B not found" >&2; exit 1; }

# Decode the SUBMISSION lines of vendor/standard control transfers into one
# human-readable step per line, prefixed with a time relative to the first step.
# usbmon control submission layout (after tag+ts): S Co/Ci:b:d:e s RT RQ wVal wIdx wLen [= data]
decode() {
	grep -E " S (Co|Ci):" "$1" | awk '
	NR==1 { t0=$2 }
	{
		rel=($2-t0)/1000.0;            # us -> ms
		rt=$6; rq=$7; wval=$8; widx=$9; wlen=$10;
		# camera-mode SET carries the mode in payload byte 12 (after "= ").
		mode="";
		if (rq=="09" && wval=="000b") {
			data="";
			for (i=11;i<=NF;i++) if($i=="="){ for(j=i+1;j<=NF;j++) data=data $j; break }
			mode=" mode=0x" substr(data,25,2);
		}
		if      (rq=="09") step="VEND_SET  report=" wval " len=" wlen mode;
		else if (rq=="01") step="VEND_GET  report=" wval " len=" wlen;
		else if (rq=="0b") step="SET_INTERFACE if=" widx " alt=" wval;
		else if (rq=="06") step="GET_DESCRIPTOR";
		else if (rq=="0a") step="GET_INTERFACE if=" widx;
		else if (rq=="03") step="SET_FEATURE wVal=" wval " wIdx=" widx;
		else if (rq=="01" && rt ~ /^0/) step="CLEAR_FEATURE wVal=" wval " wIdx=" widx;
		else               step="RQ=" rq " wVal=" wval " wIdx=" widx;
		printf "%8.2fms RT=%s %s\n", rel, rt, step;
	}'
}

FA=$(mktemp); FB=$(mktemp)
trap 'rm -f "$FA" "$FB"' EXIT
decode "$A" > "$FA"
decode "$B" > "$FB"

echo "LEFT  = $A  ($(wc -l < "$FA") control steps)"
echo "RIGHT = $B  ($(wc -l < "$FB") control steps)"
echo
paste -d'\t' "$FA" "$FB" | column -t -s $'\t' \
	-N "FORK / LEFT,OURS / RIGHT" -o ' | ' 2>/dev/null \
	|| paste -d'|' "$FA" "$FB"
