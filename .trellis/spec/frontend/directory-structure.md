# Directory Structure

> How frontend code is organized in this project.

---

## Overview

<!--
Document your project's frontend directory structure here.

Questions to answer:
- Where do components live?
- How are features/modules organized?
- Where are shared utilities?
- How are assets organized?
-->

(To be filled by the team)

---

## Directory Layout

```
main/
└── web/
    └── index.html
```

---

## Module Organization

<!-- How should new features be organized? -->

- The embedded Web UI is a single framework-free HTML/CSS/vanilla JS file.
- Keep the local device UI in `main/web/index.html` so the IDF runtime can embed
  it directly into the firmware image.

---

## Naming Conventions

<!-- File and folder naming rules -->

- Keep embedded Web UI assets small and local.
- Do not introduce remote assets, CDN calls, or framework bundles.

---

## Examples

<!-- Link to well-organized modules as examples -->

(To be filled by the team)
