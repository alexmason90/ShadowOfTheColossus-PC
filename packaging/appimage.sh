#!/usr/bin/env bash
# Use the generic Linux packaging path; does not require Nix.
set -euo pipefail
exec bash "$(dirname -- "$0")/linux/package.sh" "$@"
