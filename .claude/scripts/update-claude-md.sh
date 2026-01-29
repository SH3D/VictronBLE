#!/bin/bash
# Auto-update CLAUDE.md at end of session

CLAUDE_MD="$(git rev-parse --show-toplevel)/.claude/CLAUDE.md"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M')

# Get recent git activity from this session (last hour)
RECENT_COMMITS=$(git log --oneline --since="1 hour ago" 2>/dev/null | head -5)
MODIFIED_FILES=$(git diff --name-only HEAD~1 2>/dev/null | head -10)

# Append session summary
{
  echo ""
  echo "### Session: $TIMESTAMP"

  if [ -n "$RECENT_COMMITS" ]; then
    echo "**Commits:**"
    echo "\`\`\`"
    echo "$RECENT_COMMITS"
    echo "\`\`\`"
  fi

  if [ -n "$MODIFIED_FILES" ]; then
    echo "**Modified files:**"
    echo "$MODIFIED_FILES" | sed 's/^/- /'
  fi

  echo ""
} >> "$CLAUDE_MD"

echo "Updated CLAUDE.md with session summary"
