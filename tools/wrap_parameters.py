#!/usr/bin/env python3
"""Wrap declarations longer than 100 characters: one parameter per line.

Usage: python3 tools/wrap_parameters.py file...   (rewrites in place)

Only lines that end with ';' or ')' or '{' and hold one parenthesised list with
top-level commas are wrapped. Other long lines are printed for manual work.
"""
import re
import sys

LIMIT = 100


def groups(line):
    stack = []
    found = []
    for i, c in enumerate(line):
        if c == "(":
            stack.append(i)
        elif c == ")" and stack:
            start = stack.pop()
            found.append((len(stack), start, i))
    return found


def split_top(text):
    parts = []
    depth = 0
    current = []
    for c in text:
        if c in "(<[{":
            depth += 1
        elif c in ")>]}":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
            continue
        current.append(c)
    parts.append("".join(current).strip())
    return parts


def wrap(line):
    stripped = line.rstrip()
    if stripped.lstrip().startswith("#"):
        return None
    indent = stripped[: len(stripped) - len(stripped.lstrip())]
    found = groups(stripped)
    candidates = [g for g in found if len(split_top(stripped[g[1] + 1:g[2]])) > 1]
    if not candidates:
        candidates = [g for g in found if stripped[g[1] + 1:g[2]].strip()]
    if not candidates:
        return None
    _, start, end = min(candidates, key=lambda g: (g[0], g[1] - g[2]))
    parameters = split_top(stripped[start + 1:end])
    head = indent + re.sub(r"  +", " ", stripped[len(indent): start + 1])
    inner = indent + "    "
    lines = [head]
    for index, parameter in enumerate(parameters):
        tail = "," if index + 1 < len(parameters) else stripped[end:]
        lines.append(inner + parameter + tail)
    if any(len(item) > LIMIT for item in lines):
        return None
    return lines


def main():
    for path in sys.argv[1:]:
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        newline = "\r\n" if "\r\n" in text else "\n"
        out = []
        for number, line in enumerate(text.split(newline), 1):
            if len(line) <= LIMIT:
                out.append(line)
                continue
            wrapped = wrap(line)
            if wrapped is None:
                print(f"{path}:{number}: left as is")
                out.append(line)
                continue
            out.extend(wrapped)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(newline.join(out))


if __name__ == "__main__":
    main()
