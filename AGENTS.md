# Component development

This repository builds independently and as a subproject of metamod-fallguys.
Public interface headers belong in include/, implementation and private headers in src/.
Use dependency SOURCE_PATH variables for local clones; defaults fetch fixed commits.
Do not expose a dependency's src/ tree or change Sven Co-op ABI layouts during refactors.
Preserve third-party source and licensing. Run native builds and format-check before delivery.
Formatting uses shared FormatValidation 13c9fabe058e1f887ad1b03bb6884de911192c6a;
clang-format 23.1.3 is required. The generated .clang-format is ignored.
