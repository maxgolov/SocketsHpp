# Notes for AI coding assistants

## Commit identity (required)

All commits in this repository are authored by the maintainer. Before the first
commit in a new clone or session, run:

```sh
git config user.name  "Max Golovanov"
git config user.email "max.golovanov+github@gmail.com"
git config core.hooksPath .githooks
```

- Never commit as an assistant identity (e.g. `Claude <noreply@anthropic.com>`).
- Never add `Co-Authored-By`, `Claude-Session`, "Generated with Claude Code" or
  similar attribution lines to commit messages, PR descriptions, or files.
- The hooks in `.githooks/` enforce this: `commit-msg` strips attribution
  trailers, `pre-commit` rejects an assistant author or committer, and
  `pre-push` rejects any new commit that still carries either.
