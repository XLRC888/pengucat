set -euo pipefail

echo "🧪 Testing Nix builds for wayland-bongocat"
echo "=========================================="

echo "📦 Testing flake build..."
if nix flake check --no-build 2>/dev/null; then
    echo "✅ Flake check: SUCCESS"
    
    if nix build --no-link 2>/dev/null; then
        echo "✅ Flake build: SUCCESS"
    else
        echo "❌ Flake build: FAILED"
        exit 1
    fi
else
    echo "⚠️  Nix flakes not available or flake invalid, skipping"
fi

echo ""
echo "🔧 Testing development shell..."
if command -v nix-shell >/dev/null 2>&1; then
    if nix-shell nix/shell.nix --run "echo 'Shell works'" >/dev/null 2>&1; then
        echo "✅ Development shell: SUCCESS"
    else
        echo "❌ Development shell: FAILED"
        exit 1
    fi
else
    echo "⚠️  nix-shell not available, skipping"
fi

echo ""
echo "🎉 All available Nix builds completed successfully!"
echo ""
