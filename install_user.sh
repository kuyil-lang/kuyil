#!/usr/bin/env bash
# Install Kuyil binary and libs into the current user's home directory.
# - Copies ./kuyil and ./libs/ into ~/.kuyil
# - Writes a wrapper at ~/.kuyil/bin/kuyil that ensures libraries load
# - Generates ~/.kuyil/kuyil-env.sh to add ~/.kuyil/bin to PATH when sourced
#
# Usage:
#   ./install_user.sh                # installs to ~/.kuyil
#   ./install_user.sh /custom/path   # installs to /custom/path
set -euo pipefail

COLOR_GREEN="\033[0;32m"
COLOR_YELLOW="\033[1;33m"
COLOR_RED="\033[0;31m"
COLOR_RESET="\033[0m"

say() { printf "%b\n" "$*"; }
info() { say "${COLOR_GREEN}➤${COLOR_RESET} $*"; }
warn() { say "${COLOR_YELLOW}⚠${COLOR_RESET} $*"; }
err()  { say "${COLOR_RED}✖${COLOR_RESET} $*"; }

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRC_ROOT="$SCRIPT_DIR"
SRC_BIN="$SRC_ROOT/kuyil"
SRC_LIBS_DIR="$SRC_ROOT/libs"
SRC_INTERFACES_DIR="$SRC_ROOT/interfaces"

INSTALL_BASE=${1:-"$HOME/.kuyil"}
INSTALL_BIN_DIR="$INSTALL_BASE/bin"
INSTALL_LIBS_DIR="$INSTALL_BASE/libs"
INSTALL_INTERFACES_DIR="$INSTALL_BASE/interfaces"
INSTALL_WRAPPED_BIN="$INSTALL_BIN_DIR/kuyil"
INSTALL_REAL_BIN="$INSTALL_BASE/kuyil"
INSTALL_ENV_FILE="$INSTALL_BASE/kuyil-env.sh"

info "Installing Kuyil to $INSTALL_BASE"

# Basic validations
if [[ ! -x "$SRC_BIN" ]]; then
  err "Binary not found at $SRC_BIN. Build it first (e.g., make or ./make_bins.sh)."
  exit 1
fi
if [[ ! -d "$SRC_LIBS_DIR" ]]; then
  warn "libs/ directory not found at $SRC_LIBS_DIR. Creating empty libs directory."
fi

mkdir -p "$INSTALL_BIN_DIR" "$INSTALL_LIBS_DIR" "$INSTALL_INTERFACES_DIR"

# Copy binary into base dir so relative ./libs and ./interfaces resolve after cd
info "Copying binary to $INSTALL_REAL_BIN"
install -m 0755 "$SRC_BIN" "$INSTALL_REAL_BIN"

# Copy libraries (if any)
if [[ -d "$SRC_LIBS_DIR" ]]; then
  info "Syncing libraries to $INSTALL_LIBS_DIR"
  rsync -a --delete "$SRC_LIBS_DIR/" "$INSTALL_LIBS_DIR/" || cp -a "$SRC_LIBS_DIR/." "$INSTALL_LIBS_DIR/" || true
fi

# Copy interfaces (if any)
if [[ -d "$SRC_INTERFACES_DIR" ]]; then
  info "Syncing interfaces to $INSTALL_INTERFACES_DIR"
  rsync -a --delete "$SRC_INTERFACES_DIR/" "$INSTALL_INTERFACES_DIR/" || cp -a "$SRC_INTERFACES_DIR/." "$INSTALL_INTERFACES_DIR/" || true
else
  warn "interfaces/ directory not found at $SRC_INTERFACES_DIR."
fi

# Create wrapper that cd's into install base then execs real binary
info "Creating launcher at $INSTALL_WRAPPED_BIN"
cat > "$INSTALL_WRAPPED_BIN" << 'WRAP'
#!/usr/bin/env bash
set -euo pipefail
# Resolve base dir as the parent of this script's directory, but do NOT cd.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASE_DIR=$(readlink -f "$SCRIPT_DIR/..")

# Prefer installed home for library loading
export KUYIL_HOME="${KUYIL_HOME:-$BASE_DIR}"

exec "$BASE_DIR/kuyil" "$@"
WRAP
chmod +x "$INSTALL_WRAPPED_BIN"

# Create env file to add bin dir to PATH and export KUYIL_HOME for convenience
info "Writing env file to $INSTALL_ENV_FILE"
cat > "$INSTALL_ENV_FILE" <<EOF
# Kuyil user installation environment
# Source this file to add Kuyil to your PATH
#   source "$INSTALL_ENV_FILE"

export KUYIL_HOME="$INSTALL_BASE"
# Prepend to PATH if not already present
case ":$PATH:" in
  *":$INSTALL_BIN_DIR:"*) ;;
  *) export PATH="$INSTALL_BIN_DIR:$PATH" ;;
esac
EOF

say ""
info "Installed Kuyil user-local runtime"
say "  Home:       $INSTALL_BASE"
say "  Binary:     $INSTALL_WRAPPED_BIN (wrapper)"
say "  Real:       $INSTALL_REAL_BIN"
say "  Libs:       $INSTALL_LIBS_DIR"
say "  Interfaces: $INSTALL_INTERFACES_DIR"
say "  Env:        $INSTALL_ENV_FILE"

say ""
info "Next steps"
say "1) Add Kuyil to your current shell PATH:"
say "   source \"$INSTALL_ENV_FILE\""
say "2) Optionally, persist it by adding the above line to ~/.bashrc or ~/.zshrc"

# Quick smoke test (optional)
if command -v timeout >/dev/null 2>&1; then
  say ""
  info "Running quick smoke test..."
  if timeout 3s bash -lc "source \"$INSTALL_ENV_FILE\" && echo 'print(\"ok\")' | kuyil -" >/dev/null 2>&1; then
    info "Smoke test passed"
  else
    warn "Smoke test failed. You can still try: echo 'print(\"ok\")' | $INSTALL_WRAPPED_BIN -"
  fi
fi
