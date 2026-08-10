# WeChatCopilot AI Working Context

## Context routing

- Start with `docs/ai/README.md`; load only the context relevant to the task.
- For implementation and human-facing behavior, use `docs/developer/README.md` and source code as authority.
- `docs/ai/REVERSE_ENGINEERING_CONTEXT.md` is the concise reverse-engineering handoff.
- `docs/ai/DATABASE_SCHEMA.md` is a large retrieval index. Search it for a database/table/column; do not load it wholesale.
- `docs/ai/database_schema.json` is generated machine data. Prefer targeted queries over copying it into prompts.
- Detailed evidence and historical procedures live under `docs/developer/`; they are not default prompt context.

## Repository boundaries

- `src/db_explorer/`, `web/`, and `xmake.lua`: current read-only database browser.
- `src/chat_exporter/`: WCDB-only contact/session resolver and full JSONL chat exporter.
- `research/weixin-db/`: standalone imported CMake/Python/IDA tool suite; paths in migrated research documents are relative to this directory unless stated otherwise.
- `local-data/`: private databases, keys, logs, and exports. Do not inspect it unless the task specifically requires local sample validation.
- Never place key bytes, chat content, account identifiers, or raw database rows in documentation, AI context, logs, fixtures, or commits.

## Technical constraints

- Treat every database as read-only. Do not modify a live Weixin database.
- Numeric IDs and rowids are database-local. Cross-database joins must resolve through `username`/`user_name`.
- Revalidate RVAs, table layouts, compression markers, and inferred enum meanings after a Weixin version change.
- Distinguish verified observations from name-based inference and unknowns.
- Keep developer explanations in `docs/developer/`; keep terse routing facts and constraints in `docs/ai/`.

## Verification

- Current applications: `xmake f -m release`, then `xmake`; targeted builds are
  `xmake build db_explorer` and `xmake build chat_exporter`.
- Imported tool suite: follow `research/weixin-db/README.md`; its dependencies and build output are intentionally local-only.
- After schema changes, run `python tools/generate_schema_doc.py` from the repository root.
