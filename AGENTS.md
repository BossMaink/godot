# AGENTS.md

## Workspace Boundary
- Never read, search, or reference files outside `e:\Godot\godot` unless the user explicitly asks for it.
- If a symbol, dependency, or related file is missing from this workspace, stop and ask the user instead of searching parent folders, sibling projects, or other directories.
- Treat every path outside `e:\Godot\godot` as out of scope even if the tool can technically access it.
