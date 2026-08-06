#!/usr/bin/env bash

set -euo pipefail

if (( EUID != 0 )); then
    echo "This script must be run as root." >&2
    echo "Usage: sudo $0" >&2
    exit 1
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
locale_dir=/usr/local/share/locale

for po_file in "$script_dir"/*.po; do
    language=$(basename -- "${po_file%.po}")
    message_dir="$locale_dir/$language/LC_MESSAGES"
    mo_file="$message_dir/docklight6.mo"

    echo "$mo_file"
    mkdir -p -- "$message_dir"
    msgfmt --check --output-file="$mo_file" "$po_file"
done

echo "Done!"
