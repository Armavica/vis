#!/bin/sh

[ -z "$VIS" ] && VIS="../../vis"
[ -z "$VIM" ] && VIM="vim"

EDITORS="$VIM $VIS"

TESTS=$1
if [ -z "$TESTS" ]; then
	TESTS=$(find . -name '*.in' | sed 's/\.in$//g')
fi

TESTS_RUN=0
TESTS_OK=0

# TODO find a way to reliably goto normal mode in both editors
CTRLC=""
ESC="\e" # not portable

$VIM --version | head -1
$VIS -v

for t in $TESTS; do
	for EDITOR in $EDITORS; do
		e=$(basename "$EDITOR");
		ERR="$t.$e.err"
		OUT="$t.$e.out"
		VIM_OUT="$t.$VIM.out"
		printf "Running test %s with %s ... " "$t" "$e"
		rm -f "$OUT" "$ERR"
		if [ "$e" = "$VIM" ]; then
			NORMAL=$ESC
		else
			NORMAL=$CTRLC
		fi
		{ cat "$t.keys"; printf "$NORMAL:wq! $OUT\n"; } | $EDITOR "$t.in" 2> /dev/null
		if [ "$e" = "$VIM" ]; then
			if [ -e "$VIM_OUT" ]; then
				printf "OK\n"
			else
				printf "FAIL\n"
			fi
		elif [ -e "$VIM_OUT" ]; then
			if cmp -s "$VIM_OUT" "$OUT"; then
				printf "OK\n"
				TESTS_OK=$((TESTS_OK+1))
			else
				printf "FAIL\n"
				diff -u "$VIM_OUT" "$OUT" > "$ERR"
			fi
			TESTS_RUN=$((TESTS_RUN+1))
		fi
	done
done

printf "Tests ok %d/%d\n" $TESTS_OK $TESTS_RUN

# set exit status
[ $TESTS_OK -eq $TESTS_RUN ]
