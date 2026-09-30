#!/usr/bin/env bash
# ElenixOS fork → 上传到你自己新建的 GitHub 仓库（绝不推官方 origin）
# 用法 (Git Bash):  bash publish.sh https://github.com/<你的用户名>/<仓库名>.git
set -e

REPO_URL="${1:-}"
if [ -z "$REPO_URL" ]; then
  echo "用法: bash publish.sh <你的GitHub仓库URL>"
  echo "示例: bash publish.sh https://github.com/alice/my-elenixos.git"
  exit 1
fi

# 安全护栏：禁止误推官方仓库
if echo "$REPO_URL" | grep -qi "ElenixOS/ElenixOS"; then
  echo "❌ 拒绝：检测到官方仓库地址 (github.com/ElenixOS/ElenixOS)。"
  echo "   请新建你自己的仓库，不要把 fork 推到官方。"
  exit 1
fi

# 切到仓库根（脚本应放在 ElenixOS 根目录）
cd "$(dirname "$0")"

echo "[1/5] 清理嵌套仓库 cyberpunk2077-breach-protocol/.git (避免被当 submodule) ..."
rm -rf cyberpunk2077-breach-protocol/.git
echo "      done (其 LICENSE / 源码仍保留为普通文件)"

echo "[2/5] 暂存所有改动 (.gitignore 已排除 .workbuddy/ 与截图垃圾) ..."
git add -A

if git diff --cached --quiet; then
  echo "      无新改动，跳过 commit"
else
  git commit -m "fork: add Breach Protocol app + icon adaptation + fork notice (MIT attribution)"
  echo "      committed"
fi

echo "[3/5] 配置 remote (只建 myfork，绝不碰 origin) ..."
git remote remove myfork 2>/dev/null || true
git remote add myfork "$REPO_URL"

echo "[4/5] 推送当前分支到 myfork (当前分支: $(git branch --show-current)) ..."
git push -u myfork "$(git branch --show-current)"

echo "[5/5] ✅ 完成。仓库已上传到: $REPO_URL"
echo "      提示: 在 GitHub 仓库 About 注明 'Unofficial fork of ElenixOS'。"
