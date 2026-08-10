#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SKILL_SOURCE="$PROJECT_ROOT/skills/ohospatch"
DECLARATION_SOURCE="$PROJECT_ROOT/fixit.d.js"

install_codex=false
install_claude=false
selection_given=false
force=false

usage() {
  cat <<'EOF'
Usage: ./scripts/install-skill.sh [--codex | --claude | --all] [--force]

Install the OhosPatch authoring Skill for Codex, Claude Code, or both.

Options:
  --codex   Install only for Codex.
  --claude  Install only for Claude Code.
  --all     Install for both tools (default).
  --force   Replace an existing OhosPatch Skill installation.
  -h, --help
            Show this help.

Environment:
  CODEX_HOME   Codex configuration root (default: ~/.codex).
  CLAUDE_HOME  Claude Code configuration root (default: ~/.claude).
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --codex)
      install_codex=true
      selection_given=true
      ;;
    --claude)
      install_claude=true
      selection_given=true
      ;;
    --all)
      install_codex=true
      install_claude=true
      selection_given=true
      ;;
    --force)
      force=true
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

if [ "$selection_given" = false ]; then
  install_codex=true
  install_claude=true
fi

if [ ! -f "$SKILL_SOURCE/SKILL.md" ] || [ ! -f "$DECLARATION_SOURCE" ]; then
  printf 'OhosPatch Skill sources are incomplete under %s\n' "$PROJECT_ROOT" >&2
  exit 1
fi

CODEX_ROOT=${CODEX_HOME:-"$HOME/.codex"}
CLAUDE_ROOT=${CLAUDE_HOME:-"$HOME/.claude"}
CODEX_DESTINATION="$CODEX_ROOT/skills/ohospatch"
CLAUDE_DESTINATION="$CLAUDE_ROOT/skills/ohospatch"

check_destination() {
  destination=$1
  if [ -e "$destination" ] && [ "$force" = false ]; then
    printf 'Skill already exists at %s; rerun with --force to replace it.\n' "$destination" >&2
    exit 1
  fi
}

install_to() {
  tool_name=$1
  destination=$2
  parent=$(dirname -- "$destination")
  temporary="$parent/.ohospatch.install.$$"

  mkdir -p "$parent"
  rm -rf "$temporary"
  mkdir -p "$temporary/references"
  cp -R "$SKILL_SOURCE"/. "$temporary/"
  cp "$DECLARATION_SOURCE" "$temporary/references/fixit.d.js"

  if [ -e "$destination" ]; then
    rm -rf "$destination"
  fi
  mv "$temporary" "$destination"
  printf 'Installed OhosPatch Skill for %s: %s\n' "$tool_name" "$destination"
}

if [ "$install_codex" = true ]; then
  check_destination "$CODEX_DESTINATION"
fi
if [ "$install_claude" = true ]; then
  check_destination "$CLAUDE_DESTINATION"
fi

if [ "$install_codex" = true ]; then
  install_to "Codex" "$CODEX_DESTINATION"
fi
if [ "$install_claude" = true ]; then
  install_to "Claude Code" "$CLAUDE_DESTINATION"
fi
