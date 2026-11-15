#!/usr/bin/env python3
"""Generate the stb-style single header of kiotty-net.

Usage:
  python3 tools/make_single_header.py [--organization Org] [--project Proj] [--output PATH]

  python3 tools/make_single_header.py
      writes single_header/kiotty_net.hpp with the repository names.
  python3 tools/make_single_header.py --organization Acme --project Link --output out/acme_link.hpp
      writes the same library with every kiotty-net name replaced.

Naming rule:
  --organization and --project are PascalCase: an upper-case ASCII letter followed by ASCII
  letters and digits (default Kiotty and Net). Three forms are derived from them:
    Pascal  Organization + Project            KiottyNet  -> AcmeLink
    UPPER   ORGANIZATION + "_" + PROJECT      KIOTTY_NET -> ACME_LINK
    lower   organization and project apart    kiotty::net -> acme::link
  UPPER and lower are plain case conversions; capitals inside a name do not split words,
  so MyCo becomes MYCO and myco. The lower forms must not be C++ keywords or the names
  std, posix, linux and unix; the lower organization must not be net.
  Replaced: KiottyNet* types, KiottyNet_* functions and C++ classes, KIOTTY_NET* macros,
  enum members and include guards, the namespaces kiotty and kiotty::net.

Layout of the output:
  The public part (include/kiotty_net_base.h, kiotty_net_statemachine.h, kiotty_net_http.h,
  kiotty_net_websocket.h) is always visible. The implementation (every source listed in
  KIOTTY_NET_SOURCES of CMakeLists.txt with its internal headers) is compiled only when
  KIOTTY_NET_IMPLEMENTATION is defined before the include, once per program.
  Internal headers are inlined at their first include and wrapped in a generated guard.
  A header first met inside a preprocessor condition is inlined again at the next include,
  so a platform or feature condition never hides a header that another branch needs.
  Platform sources guard themselves (#if defined(_WIN32), KIOTTY_NET_EVENT_*); TLS,
  io_uring and libkqueue are chosen by the macros listed in the generated header comment.
  --line-directives adds #line markers so compiler messages name the repository files;
  it is for debugging the generator and leaves the paths unrenamed.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PUBLIC_DIRECTORY = os.path.join(ROOT, "include")
PRIVATE_DIRECTORY = os.path.join(ROOT, "src", "implementation")
PUBLIC_HEADERS = [
    "kiotty_net_base.h",
    "kiotty_net_statemachine.h",
    "kiotty_net_http.h",
    "kiotty_net_websocket.h",
]
VERSION_PATHS = ["CMakeLists.txt", "include", "src", "tools/make_single_header.py"]

LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
OPEN_CONDITION = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b")
CLOSE_CONDITION = re.compile(r"^\s*#\s*endif\b")
PRAGMA_ONCE = re.compile(r"^\s*#\s*pragma\s+once\b")
PASCAL_NAME = re.compile(r"^[A-Z][A-Za-z0-9]*$")

CPP_KEYWORDS = set("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
char32_t class compl concept const consteval constexpr constinit const_cast continue co_await
co_return co_yield decltype default delete do double dynamic_cast else enum explicit export
extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq
nullptr operator or or_eq private protected public register reinterpret_cast requires return
short signed sizeof static static_assert static_cast struct switch template this thread_local
throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while
xor xor_eq restrict _Bool _Complex _Imaginary
""".split())
RESERVED_LOWER = {"std", "posix", "linux", "unix"}

HEADER_TEMPLATE = """\
/*
    {pascal} single header, generated from commit {version}
    by tools/make_single_header.py{arguments}. Do not edit; regenerate.

    Use
        Include this file wherever the API is needed (C99 or C++17).
        In exactly one C++17 source file of the program, define the implementation macro
        before including it:

            #define {upper}_IMPLEMENTATION
            #include "{file_name}"

    Implementation switches (define them before that include)
        {upper}_USE_TLS=1
            OpenSSL TLS 1.2 and 1.3 (tested with OpenSSL 3). Needs the OpenSSL include
            path and links libssl and libcrypto. Default 0: a stub backend, TLS calls
            return {upper}_CODE_TLS_NOT_SUPPORTED.
        {upper}_WITH_IO_URING
            Linux: io_uring when the kernel allows it, epoll otherwise. Any definition turns
            it on. Needs linux/io_uring.h from kernel 5.11 or newer; no liburing.
            Default: epoll only.
        {upper}_WITH_LIBKQUEUE
            Linux: the kqueue backend through libkqueue (sys/event.h include path, link
            libkqueue). Default off.
        {upper}_SHARED and {upper}_BUILD
            Export ({upper}_BUILD) or import the API of a shared library.

    Link
        Windows: ws2_32 and ntdll (MSVC links them by #pragma comment).
        POSIX: the thread library (-pthread).
        With {upper}_USE_TLS=1: libssl and libcrypto.

    Windows
        The implementation defines WIN32_LEAN_AND_MEAN and NOMINMAX when they are not
        defined. If windows.h is included before this file in the implementation source,
        define both macros first; the implementation stops with #error when min or max
        is a macro.
*/
"""

IMPLEMENTATION_PROLOGUE = """\
#if !defined(__cplusplus)
#error "KIOTTY_NET_IMPLEMENTATION needs a C++17 translation unit"
#endif
#if !defined(KIOTTY_NET_USE_TLS)
#define KIOTTY_NET_USE_TLS 0
#endif
#if defined(_WIN32)
#if defined(min) || defined(max)
#error "KIOTTY_NET_IMPLEMENTATION: define NOMINMAX before including windows.h"
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ntdll.lib")
#endif
#endif
"""


class Names:
    def __init__(self, organization, project):
        self.organization = organization
        self.project = project
        self.pascal = organization + project
        self.upper = organization.upper() + "_" + project.upper()
        self.organization_lower = organization.lower()
        self.project_lower = project.lower()
        self.file_name = self.organization_lower + "_" + self.project_lower + ".hpp"

    def is_default(self):
        return self.organization == "Kiotty" and self.project == "Net"


def validate(organization, project):
    for label, value in (("--organization", organization), ("--project", project)):
        if not PASCAL_NAME.match(value):
            return "%s %r is not PascalCase ([A-Z][A-Za-z0-9]*)" % (label, value)
        lower = value.lower()
        if lower in CPP_KEYWORDS or lower in RESERVED_LOWER:
            return "%s %r gives the reserved lower-case name %r" % (label, value, lower)
    if organization.lower() == "net":
        return "--organization must not be Net: the sources alias namespace net"
    return None


def rename(text, names):
    if names.is_default():
        return text
    namespace = names.organization_lower + "::" + names.project_lower
    text = re.sub(r"\bKiottyNet(?![a-z0-9])", names.pascal, text)
    text = re.sub(r"\bKIOTTY_NET(?![A-Z0-9])", names.upper, text)
    text = re.sub(r"\bkiotty::net\b", namespace, text)
    text = re.sub(r"\bnamespace(\s+)net\b(?!\s*=)", r"namespace\g<1>" + names.project_lower, text)
    text = re.sub(r"\bkiotty\b", names.organization_lower, text)
    return text


def leftovers(text, names):
    pattern = r"\w*(?:KiottyNet|KIOTTY_NET|kiotty_net|kiotty::net)\w*"
    if names.organization_lower != "kiotty":
        pattern = r"\w*(?i:kiotty)\w*"
    return sorted(set(re.findall(pattern, text)))


def read_lines(path):
    with open(path, "r", encoding="utf-8", newline=None) as source:
        return source.read().split("\n")


def relative(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def source_list():
    with open(os.path.join(ROOT, "CMakeLists.txt"), "r", encoding="utf-8") as cmake:
        text = cmake.read()
    match = re.search(r"set\(KIOTTY_NET_SOURCES\s(.*?)\)", text, re.DOTALL)
    if match is None:
        raise SystemExit("CMakeLists.txt has no KIOTTY_NET_SOURCES list")
    return [os.path.join(ROOT, *name.split("/")) for name in match.group(1).split()]


def guard_of(path):
    name = os.path.splitext(os.path.relpath(path, PRIVATE_DIRECTORY))[0].replace(os.sep, "/")
    name = re.sub(r"(^|/)kiotty_", r"\1", name)
    return "KIOTTY_NET_INTERNAL_" + re.sub(r"[^A-Za-z0-9]", "_", name).upper() + "_H"


class Inliner:
    def __init__(self, line_directives):
        self.line_directives = line_directives
        self.public = set()
        self.unconditional = set()
        self.stack = []
        self.out = []

    def resolve(self, name, including):
        for directory in (os.path.dirname(including), PRIVATE_DIRECTORY, PUBLIC_DIRECTORY):
            candidate = os.path.normpath(os.path.join(directory, name))
            if os.path.isfile(candidate):
                return candidate
        raise SystemExit("%s: cannot resolve #include \"%s\"" % (relative(including), name))

    def add_public(self, path):
        if path in self.public:
            return
        self.public.add(path)
        self.stack.append(path)
        for line in read_lines(path):
            match = LOCAL_INCLUDE.match(line)
            if match is None:
                self.out.append(line)
                continue
            target = self.resolve(match.group(1), path)
            if os.path.dirname(target) != PUBLIC_DIRECTORY:
                raise SystemExit("%s includes a private header" % relative(path))
            self.add_public(target)
        self.stack.pop()

    def mark(self, path, number):
        if self.line_directives:
            self.out.append('#line %d "%s"' % (number, relative(path)))

    def add_private(self, path, depth):
        self.stack.append(path)
        self.mark(path, 1)
        for number, line in enumerate(read_lines(path), 1):
            if PRAGMA_ONCE.match(line):
                self.out.append("")
                continue
            match = LOCAL_INCLUDE.match(line)
            if match is not None:
                self.include(self.resolve(match.group(1), path), depth)
                self.mark(path, number + 1)
                continue
            if OPEN_CONDITION.match(line):
                depth += 1
            elif CLOSE_CONDITION.match(line):
                depth -= 1
            self.out.append(line)
        self.stack.pop()

    def include(self, path, depth):
        if path in self.public or path in self.unconditional or path in self.stack:
            return
        if os.path.dirname(path) == PUBLIC_DIRECTORY:
            raise SystemExit("public header %s is not in PUBLIC_HEADERS" % relative(path))
        if depth == 0:
            self.unconditional.add(path)
        guard = guard_of(path)
        self.out.append("#ifndef " + guard)
        self.out.append("#define " + guard)
        self.add_private(path, depth)
        self.out.append("#endif")

    def add_source(self, path):
        self.add_private(path, 0)


def compact(lines):
    result = []
    for line in lines:
        line = line.rstrip()
        if line == "" and (not result or result[-1] == ""):
            continue
        result.append(line)
    return result


def version_of():
    def git(*arguments):
        completed = subprocess.run(["git", "-C", ROOT] + list(arguments),
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                   universal_newlines=True, check=False)
        return completed.stdout.strip() if completed.returncode == 0 else None
    try:
        described = git("describe", "--always", "--tags")
        changed = git("-c", "core.autocrlf=true", "status", "--porcelain", "--", *VERSION_PATHS)
    except OSError:
        return "unknown"
    if described is None:
        return "unknown"
    return described + ("+modified" if changed else "")


def generate(names, arguments, line_directives):
    inliner = Inliner(line_directives)
    for header in PUBLIC_HEADERS:
        inliner.add_public(os.path.join(PUBLIC_DIRECTORY, header))
    public_lines = compact(inliner.out)

    inliner.out = []
    for source in source_list():
        inliner.add_source(source)
    implementation_lines = inliner.out if line_directives else compact(inliner.out)

    header = HEADER_TEMPLATE.format(pascal=names.pascal, upper=names.upper,
                                    version=version_of(), arguments=arguments,
                                    file_name=names.file_name)
    parts = [
        header,
        "#ifndef KIOTTY_NET_SINGLE_HEADER_HPP",
        "#define KIOTTY_NET_SINGLE_HEADER_HPP",
        "",
        "\n".join(public_lines),
        "",
        "#endif",
        "",
        "#if defined(KIOTTY_NET_IMPLEMENTATION) && !defined(KIOTTY_NET_IMPLEMENTATION_INCLUDED)",
        "#define KIOTTY_NET_IMPLEMENTATION_INCLUDED",
        "",
        IMPLEMENTATION_PROLOGUE,
        "\n".join(implementation_lines),
        "",
        "#endif",
        "",
    ]
    return rename("\n".join(parts), names)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--organization", default="Kiotty")
    parser.add_argument("--project", default="Net")
    parser.add_argument("--output", help="default: single_header/<organization>_<project>.hpp")
    parser.add_argument("--line-directives", action="store_true",
                        help="emit #line so compiler messages name the repository files")
    options = parser.parse_args()

    problem = validate(options.organization, options.project)
    if problem is not None:
        parser.error(problem)
    names = Names(options.organization, options.project)

    arguments = ""
    if not names.is_default():
        arguments = " --organization %s --project %s" % (names.organization, names.project)
    text = generate(names, arguments, options.line_directives)

    if not names.is_default() and not options.line_directives:
        remaining = leftovers(text, names)
        if remaining:
            raise SystemExit("names left after renaming: " + ", ".join(remaining))

    output = options.output or os.path.join(ROOT, "single_header", names.file_name)
    directory = os.path.dirname(os.path.abspath(output))
    os.makedirs(directory, exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as target:
        target.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
