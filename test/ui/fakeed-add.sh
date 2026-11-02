#!/bin/sh
# stands in for $EDITOR under check-ui: appends three capture lines, one of them broken
grep -q "could not be read" "$1" && exit 0
cat >> "$1" << 'LINES'
Eye exam 2.11 9:00 #medical
Pick up meds tomorrow #medical
badline every blurgh
LINES
