# Directory Structure

> How backend code is organized in this project.

---

## Overview

<!--
Document your project's backend directory structure here.

Questions to answer:
- How are modules/packages organized?
- Where does business logic live?
- Where are API endpoints defined?
- How are utilities and helpers organized?
-->

(To be filled by the team)

---

## Directory Layout

```
main/
├── idf_main.c
├── idf_component.yml
└── web/index.html

components/
├── esp_bms_idf_runtime/
├── esp_bms_lvgl_bridge/
├── esp_bms_lvgl_contract/
└── esp_bms_lvgl_ui/
```

---

## Module Organization

<!-- How should new features/modules be organized? -->

- `main/idf_main.c` owns boot orchestration only.
- Runtime, hardware, UI, and contract logic lives in ESP-IDF components.
- Embedded Web UI assets live under `main/web/`.
- New large subsystems should become focused components rather than growing
  `main/idf_main.c`.

---

## Naming Conventions

<!-- File and folder naming rules -->

- Component names use the `esp_bms_*` prefix.
- Public headers live under each component's `include/` directory.
- Component source files use lower snake case.

---

## Examples

<!-- Link to well-organized modules as examples -->

(To be filled by the team)
