#!/usr/bin/env bash
# Install pinned toolchain + Renode + Python env into $TOOLS_DIR and write $TOOLS_DIR/env.sh.
# Usage: scripts/bootstrap.sh   then: source "$(scripts/bootstrap.sh --print-env)"
# Default TOOLS_DIR is shared across worktrees to avoid re-downloading ~300 MB per checkout.
set -euo pipefail

ARM_VER=15.2.rel1
RENODE_VER=1.17.0
TOOLS_DIR=${TOOLS_DIR:-$HOME/.cache/rttelemetry-tools}
ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [[ ${1:-} == --print-env ]]; then echo "$TOOLS_DIR/env.sh"; exit 0; fi

os=$(uname -s); arch=$(uname -m)
case "$os/$arch" in
  Darwin/arm64)  arm_host=darwin-arm64; arm_sha=1938a84b7105c192e3fb4fa5e893ba25f425f7ddab40515ae608cd40f68669a8
                 renode_pkg=renode-$RENODE_VER.osx-arm64-portable.dmg; renode_sha=63b1fb691207f503cea937e4ec8fad3e068a517d0abac3c24c0206c62f6c4d12 ;;
  Linux/x86_64)  arm_host=x86_64; arm_sha=597893282ac8c6ab1a4073977f2362990184599643b4c5ee34870a8215783a16
                 renode_pkg=renode-$RENODE_VER.linux-portable.tar.gz; renode_sha=4ba7c68b59e2447f188ef4b4b112fcccd2802582c460beedceb63b84e5605a5f ;;
  Linux/aarch64) arm_host=aarch64; arm_sha=d061559d814b205ed30c5b7c577c03317ec447ca51cd5a159d26b12a5bbeb20c
                 renode_pkg=renode-$RENODE_VER.linux-arm64-portable.tar.gz; renode_sha=e9051578411441bfb47b77de0381cfba3badce03663367fab561472fd7807f1f ;;
  *) echo "unsupported host $os/$arch" >&2; exit 1 ;;
esac

mkdir -p "$TOOLS_DIR/dl"
fetch() { # url sha dest
  if [[ ! -f $3 ]] || ! echo "$2  $3" | shasum -a 256 -c - >/dev/null 2>&1; then
    echo "downloading $(basename "$3")"
    curl -fsSL -A "Mozilla/5.0" -o "$3.part" "$1"
    echo "$2  $3.part" | shasum -a 256 -c - >/dev/null || { echo "SHA256 mismatch for $1" >&2; exit 1; }
    mv "$3.part" "$3"
  fi
}

# --- Arm GNU toolchain ---
arm_name=arm-gnu-toolchain-$ARM_VER-$arm_host-arm-none-eabi
if [[ ! -x $TOOLS_DIR/$arm_name/bin/arm-none-eabi-gcc ]]; then
  fetch "https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_VER/binrel/$arm_name.tar.xz" "$arm_sha" "$TOOLS_DIR/dl/$arm_name.tar.xz"
  tar -xf "$TOOLS_DIR/dl/$arm_name.tar.xz" -C "$TOOLS_DIR"
  [[ $os == Darwin ]] && xattr -dr com.apple.quarantine "$TOOLS_DIR/$arm_name" 2>/dev/null || true
fi

# --- Renode ---
renode_dir=$TOOLS_DIR/renode-$RENODE_VER
if [[ ! -d $renode_dir ]]; then
  fetch "https://github.com/renode/renode/releases/download/v$RENODE_VER/$renode_pkg" "$renode_sha" "$TOOLS_DIR/dl/$renode_pkg"
  if [[ $os == Darwin ]]; then
    mnt=$(mktemp -d); hdiutil attach -quiet -nobrowse -mountpoint "$mnt" "$TOOLS_DIR/dl/$renode_pkg"
    mkdir -p "$renode_dir"; cp -R "$mnt"/. "$renode_dir"/; hdiutil detach -quiet "$mnt"
    xattr -dr com.apple.quarantine "$renode_dir" 2>/dev/null || true
  else
    mkdir -p "$renode_dir"; tar -xzf "$TOOLS_DIR/dl/$renode_pkg" -C "$renode_dir" --strip-components=1
  fi
fi

# --- Python venv (Robot Framework etc.) ---
if [[ ! -x $TOOLS_DIR/venv/bin/python ]]; then
  if command -v uv >/dev/null; then uv venv -q --python 3.12 "$TOOLS_DIR/venv"; else python3 -m venv "$TOOLS_DIR/venv"; fi
fi
if command -v uv >/dev/null; then uv pip install -q --python "$TOOLS_DIR/venv/bin/python" -r "$ROOT/requirements.txt"
else "$TOOLS_DIR/venv/bin/pip" install -q -r "$ROOT/requirements.txt"; fi

# renode / renode-test entry points differ between the macOS app bundle and Linux portable
renode_bin=$(find "$renode_dir" -maxdepth 4 -type f \( -name renode -o -name Renode \) -perm -u+x | head -1)
renode_test=$(find "$renode_dir" -maxdepth 4 -type f -name renode-test | head -1)
cat > "$TOOLS_DIR/env.sh" <<EOF
export TOOLS_DIR="$TOOLS_DIR"
export PATH="$TOOLS_DIR/$arm_name/bin:$TOOLS_DIR/venv/bin:\$PATH"
export RENODE_BIN="$renode_bin"
export RENODE_TEST="$renode_test"
export RENODE_DIR="$renode_dir"
EOF
echo "tools ready: source $TOOLS_DIR/env.sh"
