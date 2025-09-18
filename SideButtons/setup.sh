#!/bin/bash

if [ "$EUID" -ne 0 ]; then
  echo "Please run this script with sudo: sudo ./setup.sh"
  exit 1
fi

BINARIES=("register" "sidebuttons")

for BIN in "${BINARIES[@]}"; do
  if [ -f "$BIN" ]; then
    echo "Setting capabilities for $BIN..."
    setcap cap_dac_read_search+ep "$BIN"
    echo "Done."
  else
    echo "Warning: $BIN not found in current directory."
  fi
done

echo "Setup completed!"
