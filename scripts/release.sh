#!/usr/bin/env bash
# release.sh — Tag a new RAMMB-NEOVIS release and push it to trigger CI
#
# Usage:
#   ./scripts/release.sh 0.2.0          # tags v0.2.0, pushes, CI builds .deb + AppImage
#   ./scripts/release.sh 0.2.0 --dry-run  # shows what would happen, makes no changes
#
# Prerequisites:
#   git, gh (GitHub CLI, authenticated)
#   The remote 'origin' must be set to your GitHub repo.

set -euo pipefail

VERSION="${1:-}"
DRY_RUN=0
[[ "${2:-}" == "--dry-run" ]] && DRY_RUN=1

# ── Validate ─────────────────────────────────────────────────────────────────

if [[ -z "$VERSION" ]]; then
    echo "Usage: $0 <version> [--dry-run]"
    echo "  Example: $0 0.2.0"
    exit 1
fi

# Strip leading 'v' if supplied
VERSION="${VERSION#v}"
TAG="v${VERSION}"

if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: version must be X.Y.Z (e.g. 0.2.0)"
    exit 1
fi

# ── Sanity checks ────────────────────────────────────────────────────────────

if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    echo "Error: working tree has uncommitted changes. Commit or stash first."
    exit 1
fi

CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [[ "$CURRENT_BRANCH" != "main" ]]; then
    echo "Warning: you are on branch '$CURRENT_BRANCH', not 'main'."
    read -r -p "Continue anyway? [y/N] " answer
    [[ "$answer" =~ ^[Yy]$ ]] || exit 0
fi

if git rev-parse "$TAG" &>/dev/null; then
    echo "Error: tag $TAG already exists."
    exit 1
fi

# ── Check CMakeLists version matches ────────────────────────────────────────

CMAKE_VERSION=$(grep -oP '(?<=VERSION )\d+\.\d+\.\d+' CMakeLists.txt | head -1)
if [[ "$CMAKE_VERSION" != "$VERSION" ]]; then
    echo "Warning: CMakeLists.txt has version $CMAKE_VERSION, but you are tagging $VERSION."
    read -r -p "Continue anyway? [y/N] " answer
    [[ "$answer" =~ ^[Yy]$ ]] || exit 0
fi

# ── Summary ──────────────────────────────────────────────────────────────────

echo ""
echo "  Repo   : $(git remote get-url origin)"
echo "  Tag    : $TAG"
echo "  Commit : $(git rev-parse --short HEAD)"
echo ""

if [[ $DRY_RUN -eq 1 ]]; then
    echo "[dry-run] Would create tag $TAG and push — stopping here."
    exit 0
fi

read -r -p "Create and push tag $TAG? [y/N] " confirm
[[ "$confirm" =~ ^[Yy]$ ]] || exit 0

# ── Tag and push ─────────────────────────────────────────────────────────────

echo ""
echo "Creating annotated tag $TAG..."
git tag -a "$TAG" -m "Release $TAG"

echo "Pushing tag to origin..."
git push origin "$TAG"

echo ""
echo "Tag pushed. GitHub Actions will now:"
echo "  1. Build rammb-neovis on Ubuntu 22.04"
echo "  2. Package a .deb via CPack"
echo "  3. Build a portable AppImage via linuxdeploy"
echo "  4. Create a GitHub Release at:"
echo "     $(git remote get-url origin | sed 's/\.git$//')/releases/tag/$TAG"
echo ""
echo "Monitor progress:"
echo "  gh run list --repo $(git remote get-url origin | sed 's|.*github.com[:/]||;s|\.git$||')"
