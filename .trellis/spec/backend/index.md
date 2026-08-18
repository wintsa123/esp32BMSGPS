# Backend Development Guidelines

> Best practices for backend development in this project.

---

## Overview

This directory contains guidelines for backend development. Fill in each file with your project's specific conventions.

---

## Guidelines Index

| Guide | Description | Status |
|-------|-------------|--------|
| [Directory Structure](./directory-structure.md) | Module organization and file layout | To fill |
| [Database Guidelines](./database-guidelines.md) | ORM patterns, queries, migrations | To fill |
| [Error Handling](./error-handling.md) | Error types, handling strategies | To fill |
| [Quality Guidelines](./quality-guidelines.md) | Runtime, display, backlight, and code safety contracts | Active |
| [Logging Guidelines](./logging-guidelines.md) | Structured logging, log levels | Started |
| [Hardware, Build, and Flash](./hardware-build-flash.md) | GPIO ownership, board conflicts, toolchains, partitions, and supported flash paths | Active |
| [FarDriver Gear Telemetry](./fardriver-gear-telemetry.md) | Compact-frame gear extraction and dashboard display contract | Active |
| [LVGL Carousel Drag Diagnostics](./lvgl-carousel-drag-diagnostics.md) | Default invalidation policy and A/B diagnostic build contract | Active |
| [GPS Diagnostic Telemetry](./gps-diagnostic-telemetry.md) | GSA/GSV parsing, snapshot projection, and settings-page status contract | Active |
| [FlashDB History Storage](./flashdb-history.md) | Board-flash session, sample, fault, and Android API contract | Active |

---

## How to Fill These Guidelines

For each guideline file:

1. Document your project's **actual conventions** (not ideals)
2. Include **code examples** from your codebase
3. List **forbidden patterns** and why
4. Add **common mistakes** your team has made

The goal is to help AI assistants and new team members understand how YOUR project works.

---

**Language**: All documentation should be written in **English**.
