#!/bin/sh
# stands in for $EDITOR under check-ui: renames the item and gives it a note
sed -e 's/Daily stretch$/Daily stretch EDITED/' "$1" > "$1.new" && printf '  note: added from the editor\n' >> "$1.new" && mv "$1.new" "$1"
