# Shared patterns for the hooks in this directory (sourced, not executed).
# Commits in this repository are authored by the maintainer only: no AI
# assistant identities and no assistant attribution trailers.

# Author/committer identities that must never be recorded.
FORBIDDEN_IDENT='noreply@anthropic\.com|^Claude( |$)|<[^>]*@anthropic\.com>'

# Commit message lines that are stripped (commit-msg) or rejected (pre-push).
FORBIDDEN_MSG_LINE='^[[:space:]]*Co-authored-by:.*(claude|anthropic)|^[[:space:]]*Claude-Session:|Generated (with|by) \[Claude Code\]|claude\.ai/code/session_'
