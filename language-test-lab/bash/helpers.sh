#!/usr/bin/env bash

set -euo pipefail

archivist_greeting() {
  local name="${1:?name is required}"
  printf 'Hello, %s!\n' "$name"
}
