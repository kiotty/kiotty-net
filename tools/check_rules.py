#!/usr/bin/env python3
"""Approximate checker for the numeric and lexical parts of development rule v2.

Usage: python3 tools/check_rules.py [paths...]   (default: src/implementation)

It is a heuristic scanner, not a parser. It reports:
  LINE      line longer than 100 characters        (rule 4.1)
  FILE      file longer than 400 lines              (rule 4.1, core template headers exempt)
  BODY      function body longer than 30 lines      (rule 4.1)
  NEST      control nesting deeper than 2           (rule 4.1)
  ARGS      more than 4 parameters                  (rule 4.1)
  PUBLIC    class with more than 7 public methods   (rule 4.1, overrides counted apart: DV9;
            constructors, operators and static named constructors are not counted, overloads once)
  DEFAULT   switch default                          (rule 2.8)
  BANNED    banned function                         (rule 2.3), c_str() outside infra backend (DV12)
  CAST      reinterpret_cast outside infra backend  (rule 2.3)
  COMMENT   comment in source                       (rule 4.9)
  MACRO     #define in source                       (rule 4.14)
"""
import os
import re
import sys

MAX_LINE = 100
MAX_FILE = 400
MAX_BODY = 30
MAX_NEST = 2
MAX_ARGS = 4
MAX_PUBLIC = 7

BANNED = re.compile(
    r"(?<![\w:.>])(?:std::)?(strcpy|strcat|strlen|gets|sprintf|vsprintf|atoi|atol|strtol|"
    r"memcpy|memset|malloc|free|rand)\s*\(")
CONTROL = {"if", "for", "while", "switch", "catch", "do", "else", "try"}
NOT_FUNCTION = CONTROL | {"return", "sizeof", "alignof", "decltype", "static_assert", "noexcept"}


def strip(text):
    out = []
    i = 0
    n = len(text)
    comments = []
    line = 1
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            comments.append(line)
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            comments.append(line)
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            chunk = text[i:end]
            line += chunk.count("\n")
            out.append("\n" * chunk.count("\n"))
            i = end
            continue
        if c == '"' or c == "'":
            quote = c
            out.append(quote)
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    i += 1
                elif text[i] == "\n":
                    break
                i += 1
            out.append(quote)
            i += 1
            continue
        if c == "\n":
            line += 1
        out.append(c)
        i += 1
    return "".join(out), comments


def line_of(text, index):
    return text.count("\n", 0, index) + 1


def head_before(code, index):
    depth = 0
    i = index - 1
    while i >= 0:
        c = code[i]
        if c == ")":
            depth += 1
        elif c == "(":
            depth -= 1
        elif depth == 0 and c in ";{}":
            break
        i -= 1
    return code[i + 1:index]


def strip_preprocessor(code):
    return re.sub(r"^[ \t]*#.*$", lambda m: " " * len(m.group(0)), code, flags=re.M)


def classify(head):
    h = " ".join(head.split())
    if not h:
        return "block", None
    first = re.match(r"[A-Za-z_]\w*", h)
    if first and first.group(0) in CONTROL:
        return "control", None
    if re.search(r"\b(class|struct|union|enum|namespace)\b[^()]*$", h):
        return "type", h
    if h.endswith("=") or h.endswith("return") or h.endswith(","):
        return "init", None
    m = re.search(r"(~?[A-Za-z_][\w:]*|operator\s*\S+)\s*\((.*)\)\s*"
                  r"((const|override|noexcept|final|mutable|->\s*[\w:<>&*, ]+)\s*)*"
                  r"(:\s*[^{}]*)?$", h)
    if m and m.group(1).split("::")[-1] not in NOT_FUNCTION:
        return "function", (m.group(1), m.group(2))
    if re.search(r"\]\s*(\([^)]*\))?\s*(mutable\s*)?(->\s*[\w:<>&* ]+)?$", h):
        return "lambda", None
    return "block", None


def count_params(params):
    params = params.strip()
    if not params or params == "void":
        return 0
    depth = 0
    count = 1
    for c in params:
        if c in "(<[{":
            depth += 1
        elif c in ")>]}":
            depth -= 1
        elif c == "," and depth == 0:
            count += 1
    return count


def is_core_template(path):
    return "/core/" in path.replace("\\", "/") and path.endswith(".h")


def is_backend(path):
    p = path.replace("\\", "/")
    return "/data/infra/" in p and "/backend/" in p


def check_classes(path, code, report):
    for m in re.finditer(r"(?<![<,]\s)(?<![<,])\b(class|struct)\s+(\w+)[^;{(<>=]*\{", code):
        start = m.end()
        depth = 1
        i = start
        while i < len(code) and depth:
            if code[i] == "{":
                depth += 1
            elif code[i] == "}":
                depth -= 1
            i += 1
        body = code[start:i - 1]
        flat = []
        depth = 0
        for c in body:
            if c == "{":
                depth += 1
                continue
            if c == "}":
                depth -= 1
                flat.append(";")
                continue
            if depth == 0:
                flat.append(c)
        access = "public" if m.group(1) == "struct" else "private"
        own = 0
        overrides = 0
        names = []
        for stmt in re.split(r";|(?<=:)(?=\s)", "".join(flat)):
            s = " ".join(stmt.split())
            a = re.match(r"^(public|private|protected)\s*:$", s)
            if a:
                access = a.group(1)
                continue
            s = re.sub(r"^(public|private|protected)\s*:\s*", "", s)
            if access != "public" or "(" not in s:
                continue
            if re.search(r"=\s*(delete|default)\b", s) or s.startswith("friend") or s.startswith("using"):
                continue
            fm = re.search(r"(~?[A-Za-z_]\w*|operator\s*\S+)\s*\(", s)
            if not fm:
                continue
            name = fm.group(1)
            if name in (m.group(2), "~" + m.group(2)) or name in NOT_FUNCTION:
                continue
            if name.startswith("operator"):
                continue
            if re.match(r"(static\s+)(inline\s+)?(constexpr\s+)?" + m.group(2) + r"\s+\w+\s*\(", s):
                continue
            if "override" in s:
                overrides += 1
            elif name not in names:
                own += 1
                names.append(name)
        if own > MAX_PUBLIC:
            report(path, line_of(code, m.start()), "PUBLIC",
                   f"{m.group(2)} has {own} public methods (+{overrides} overrides): {', '.join(names)}")


def check_file(path, report):
    with open(path, encoding="utf-8") as f:
        raw = f.read()
    lines = raw.split("\n")
    for number, text in enumerate(lines, 1):
        if len(text) > MAX_LINE:
            report(path, number, "LINE", f"{len(text)} characters")
        if re.match(r"\s*#\s*define\b", text):
            report(path, number, "MACRO", text.strip())
    if len(lines) > MAX_FILE and not is_core_template(path):
        report(path, 1, "FILE", f"{len(lines)} lines")
    code, comments = strip(raw)
    for number in comments:
        report(path, number, "COMMENT", lines[number - 1].strip()[:60])
    code = strip_preprocessor(code)
    for m in re.finditer(r"\bdefault\s*:", code):
        report(path, line_of(code, m.start()), "DEFAULT", "switch default")
    for m in BANNED.finditer(code):
        if path.replace("\\", "/").endswith("core/kiotty_span.h") and m.group(1) in ("memcpy", "memset"):
            continue
        report(path, line_of(code, m.start()), "BANNED", m.group(1))
    if not is_backend(path):
        for m in re.finditer(r"\breinterpret_cast\b", code):
            report(path, line_of(code, m.start()), "CAST", "reinterpret_cast")
        for m in re.finditer(r"\.c_str\s*\(", code):
            report(path, line_of(code, m.start()), "BANNED", "c_str outside infra backend")
    check_classes(path, code, report)

    stack = []
    for i, c in enumerate(code):
        if c == "{":
            kind, info = classify(head_before(code, i))
            if kind == "function":
                name, params = info
                if count_params(params) > MAX_ARGS:
                    report(path, line_of(code, i), "ARGS", f"{name}: {count_params(params)} parameters")
            stack.append([kind, info, i, 0])
        elif c == "}" and stack:
            kind, info, opened, _ = stack.pop()
            if kind == "function":
                body = line_of(code, i) - line_of(code, opened) - 1
                if body > MAX_BODY:
                    report(path, line_of(code, opened), "BODY", f"{info[0]}: {body} lines")
            if kind == "control":
                function_depth = 0
                depth = 1
                for frame in reversed(stack):
                    if frame[0] in ("function", "lambda"):
                        function_depth = 1
                        break
                    if frame[0] == "control":
                        depth += 1
                if function_depth and depth > MAX_NEST:
                    report(path, line_of(code, opened), "NEST", f"depth {depth}")


def main():
    roots = sys.argv[1:] or ["src/implementation"]
    found = []

    def report(path, number, kind, message):
        found.append((kind, path, number, message))

    for root in roots:
        if os.path.isfile(root):
            check_file(root, report)
            continue
        for directory, _, files in os.walk(root):
            for name in sorted(files):
                if name.endswith((".h", ".cpp", ".c")):
                    check_file(os.path.join(directory, name), report)
    for kind, path, number, message in sorted(found):
        print(f"{kind:8} {path}:{number}: {message}")
    counts = {}
    for item in found:
        counts[item[0]] = counts.get(item[0], 0) + 1
    print("summary:", ", ".join(f"{k} {v}" for k, v in sorted(counts.items())) or "clean")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
