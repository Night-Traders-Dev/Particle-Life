#!/usr/bin/env python3
"""Update README.md with latest test results."""

import sys
import re
import os

def update_readme(readme_path, timestamp, passed, failed, score, exit_code):
    with open(readme_path, 'r') as f:
        content = f.read()

    status = "PASSING" if exit_code == 0 else "FAILING"
    color = "green" if exit_code == 0 else "red"

    block = f"""
---

## 🧪 Test Suite

| Metric | Value |
|--------|-------|
| **Status** | <span style="color:{color}">**{status}**</span> |
| **Last Run** | {timestamp} |
| **Passed** | {passed} |
| **Failed** | {failed} |
| **Score** | {score}% |

> Tests are run automatically on every build. Run `bash tests/test_suite` manually to re-run.
"""

    # Remove any existing test suite block
    content = re.sub(
        r'\n---\n\n## 🧪 Test Suite\n.*?(\n---\n|$)',
        '',
        content,
        flags=re.DOTALL
    )

    # Append new block before the last section or at end
    content = content.rstrip() + block

    with open(readme_path, 'w') as f:
        f.write(content)
    print(f"[OK] Updated {readme_path} with test results")

if __name__ == '__main__':
    if len(sys.argv) < 7:
        print("Usage: update_readme.py <readme> <timestamp> <passed> <failed> <score> <exit_code>")
        sys.exit(1)
    update_readme(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], int(sys.argv[6]))
