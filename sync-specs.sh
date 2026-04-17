#!/bin/bash
# Sync requirements and design documents from .kiro/specs/ to docs/specs/
# Copies only requirements.md and design.md, preserving directory structure.

set -e

SRC=".kiro/specs/"
DST="docs/specs/"

rsync -av --prune-empty-dirs \
  --include='*/' \
  --include='requirements.md' \
  --include='design.md' \
  --exclude='*' \
  "$SRC" "$DST"

echo "Synced to $DST"
