#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
"""Generate the preFlight.py type stub from the binding source.

The stub is what script authors see in their editor, so it is derived from the
real contract and never from a hand-maintained copy of it:

  - src/luminary/gcode/scripting/PreProcessor.cpp: the embedded pybind11 module. Every
    public binding carries a docstring whose leading token is the Python type
    (attributes) or the Python signature (methods). The stub is built from
    those docstrings, so the text a script author reads in help() and the text
    the IDE shows are the same string.
  - The headers that define the C++ enums the module exposes: numeric values.
  - src/luminary/config/catalog/PrintConfig.hpp: the Settings class keys.

A public binding without a conforming docstring is a build error, so the stub
cannot drift from the module.

Docstring grammar (the last string-literal argument of the binding call):
  attribute   "<python type>: <description>"
  method      "(<params>) -> <return type>: <description>"
  class/enum  "<description>"            second string argument of py::class_ / py::enum_
  enum value  "<description>"            optional third argument of .value()
  module attr trailing comment           m.attr("x") = ...;  // stub: <python type>  <description>

Usage:
  build_stubs.py --config <PrintConfig.hpp> --bindings <PreProcessor.cpp>
                 --enum-header <hpp> [--enum-header <hpp> ...]
                 --out <dir> [--out <dir> ...]
"""

import argparse
import os
import re
import sys

# ---------------------------------------------------------------------------
# Settings class (PrintConfig.hpp)
# ---------------------------------------------------------------------------

TYPE_MAP = {
    "ConfigOptionFloat": "str  # float",
    "ConfigOptionInt": "str  # int",
    "ConfigOptionBool": "str  # bool (0/1)",
    "ConfigOptionString": "str",
    "ConfigOptionStrings": "str  # semicolon-separated",
    "ConfigOptionFloats": "str  # semicolon-separated floats",
    "ConfigOptionInts": "str  # semicolon-separated ints",
    "ConfigOptionBools": "str  # semicolon-separated bools",
    "ConfigOptionPercent": "str  # percentage",
    "ConfigOptionPercents": "str  # semicolon-separated percentages",
    "ConfigOptionFloatOrPercent": "str  # float or percentage",
    "ConfigOptionPoints": "str  # coordinate pairs",
    "ConfigOptionEnum": "str  # enum name",
    "ConfigOptionEnums": "str  # semicolon-separated enum names",
    "ConfigOptionFloatsOrPercentsNullable": "str",
    "ConfigOptionIntsNullable": "str",
}

PRINT_CONFIG_CLASSES = {
    "MachineEnvelopeConfig",
    "GCodeConfig",
    "PrintConfig",
    "PrintObjectConfig",
    "PrintRegionConfig",
}


def extract_options(hpp_content):
    """Extract config option names and types from PrintConfig.hpp."""
    options = []
    pattern = r'PRINT_CONFIG_CLASS_(?:DERIVED_)?DEFINE\d*\(\s*(\w+)'

    for match in re.finditer(pattern, hpp_content):
        class_name = match.group(1)
        if class_name not in PRINT_CONFIG_CLASSES:
            continue

        start = match.start()
        depth = 0
        pos = hpp_content.index('(', start)
        block = ''
        for i in range(pos, len(hpp_content)):
            if hpp_content[i] == '(':
                depth += 1
            elif hpp_content[i] == ')':
                depth -= 1
                if depth == 0:
                    block = hpp_content[pos:i + 1]
                    break

        # clang-format may break a pair after its opening parenthesis, so allow whitespace there.
        opt_pattern = r'\(\s*(\w+),\s*([a-z_][a-z_0-9]*)\s*\)'
        for opt_match in re.finditer(opt_pattern, block):
            opt_type = opt_match.group(1)
            opt_name = opt_match.group(2)
            if opt_type.startswith("ConfigOption"):
                options.append((opt_type, opt_name))

    return options


def generate_settings_class(options):
    """Generate the Settings class stub."""
    lines = [
        'class Settings:',
        '    """All slicer settings (print, filament and printer) merged into one namespace.',
        '',
        '    Access as gcode.settings.key or gcode.settings["key"]; every value is the',
        '    serialized string form of the option. Generated from PrintConfig.hpp.',
        '    """',
        '',
    ]

    seen = {}
    for opt_type, opt_name in options:
        if opt_name not in seen:
            seen[opt_name] = opt_type

    for opt_name in sorted(seen.keys()):
        base_type = re.sub(r'<\w+>', '', seen[opt_name])
        type_hint = TYPE_MAP.get(base_type, "str")
        lines.append(f'    {opt_name}: {type_hint}')

    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# C++ scanning helpers
# ---------------------------------------------------------------------------

class StubError(Exception):
    pass


def blank_comments(src):
    """Return src with every comment replaced by spaces (same length), so
    offsets into the result index the original text. String literals, raw
    string literals and char literals are left intact."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == '//':
            j = src.find('\n', i)
            if j == -1:
                j = n
            out.append(' ' * (j - i))
            i = j
        elif two == '/*':
            j = src.find('*/', i + 2)
            j = n if j == -1 else j + 2
            out.append(re.sub(r'[^\n]', ' ', src[i:j]))
            i = j
        elif is_literal_start(src, i) and c == 'R':
            # Raw string literal R"delim( ... )delim"
            m = re.match(r'R"([^(\s\\]*)\(', src[i:])
            if not m:
                out.append(c)
                i += 1
                continue
            delim = m.group(1)
            end_tok = ')' + delim + '"'
            j = src.find(end_tok, i + m.end())
            if j == -1:
                raise StubError("unterminated raw string literal")
            j += len(end_tok)
            out.append(src[i:j])
            i = j
        elif c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                if src[j] == '\\':
                    j += 1
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                if src[j] == '\\':
                    j += 1
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def skip_literal(code, i):
    """code[i] opens a string / raw string / char literal; return the index
    just past its end."""
    if code[i] == 'R' and code[i + 1:i + 2] == '"':
        m = re.match(r'R"([^(\s\\]*)\(', code[i:])
        end_tok = ')' + m.group(1) + '"'
        return code.find(end_tok, i + m.end()) + len(end_tok)
    q = code[i]
    j = i + 1
    while j < len(code) and code[j] != q:
        if code[j] == '\\':
            j += 1
        j += 1
    return j + 1


def is_literal_start(code, i):
    c = code[i]
    if c in '"\'':
        return True
    return c == 'R' and code[i + 1:i + 2] == '"' and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == '_'))


def find_matching(code, i):
    """code[i] is an opening bracket; return the index of its match."""
    pairs = {'(': ')', '[': ']', '{': '}'}
    close = pairs[code[i]]
    depth = 0
    j = i
    while j < len(code):
        if is_literal_start(code, j):
            j = skip_literal(code, j)
            continue
        c = code[j]
        if c in pairs:
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                if c != close:
                    raise StubError(f"bracket mismatch near: {code[i:i + 40]!r}")
                return j
        j += 1
    raise StubError(f"unbalanced bracket near: {code[i:i + 40]!r}")


def split_top_level(text, sep=','):
    """Split text on sep at bracket depth zero, honouring literals."""
    parts = []
    depth = 0
    start = 0
    i = 0
    while i < len(text):
        if is_literal_start(text, i):
            i = skip_literal(text, i)
            continue
        c = text[i]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(text[start:i])
            start = i + 1
        i += 1
    parts.append(text[start:])
    return [p.strip() for p in parts if p.strip() != '']


def string_literal_value(arg):
    """If arg is one or more adjacent C++ string literals, return the
    unescaped text; otherwise None."""
    arg = arg.strip()
    if not arg.startswith('"'):
        return None
    pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', arg)
    rest = re.sub(r'"(?:[^"\\]|\\.)*"', '', arg).strip()
    if rest:
        return None
    text = ''.join(pieces)
    return text.encode('utf-8').decode('unicode_escape')


def split_statements(code, raw):
    """Split the module body into top-level statements. Returns a list of
    (code_text, raw_text) where raw_text includes the trailing comment on the
    statement's last line."""
    stmts = []
    depth = 0
    start = 0
    i = 0
    n = len(code)
    while i < n:
        if is_literal_start(code, i):
            i = skip_literal(code, i)
            continue
        c = code[i]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ';' and depth == 0:
            # A trailing comment on the statement's last line belongs to it, so
            # the statement's raw text runs to the end of that line when nothing
            # else follows the semicolon.
            eol = raw.find('\n', i)
            if eol == -1:
                eol = n
            if code[i + 1:eol].strip():
                eol = i + 1
            stmts.append((code[start:i].strip(), raw[start:eol]))
            start = eol
        i += 1
    tail = code[start:].strip()
    if tail:
        stmts.append((tail, raw[start:]))
    return stmts


def parse_call_chain(stmt):
    """Parse `head(args).m1(args).m2(args)` into (head, head_args, [(m, args)])."""
    i = 0
    while i < len(stmt):
        if is_literal_start(stmt, i):
            i = skip_literal(stmt, i)
            continue
        if stmt[i] == '(':
            break
        i += 1
    if i >= len(stmt):
        raise StubError(f"no call in statement: {stmt[:60]!r}")
    head = stmt[:i].strip()
    j = find_matching(stmt, i)
    head_args = split_top_level(stmt[i + 1:j])
    chain = []
    k = j + 1
    while k < len(stmt):
        while k < len(stmt) and stmt[k].isspace():
            k += 1
        if k >= len(stmt):
            break
        if stmt[k] != '.':
            # Not a chain (for example `m.attr("x") = value`); leave the rest alone.
            break
        m = re.match(r'\.\s*([A-Za-z_]\w*)\s*\(', stmt[k:])
        if not m:
            raise StubError(f"cannot parse chained call near: {stmt[k:k + 40]!r}")
        open_idx = k + m.end() - 1
        close_idx = find_matching(stmt, open_idx)
        chain.append((m.group(1), split_top_level(stmt[open_idx + 1:close_idx])))
        k = close_idx + 1
    return head, head_args, chain


# ---------------------------------------------------------------------------
# Enum definitions from headers
# ---------------------------------------------------------------------------

def parse_enum_values(headers, cpp_type):
    """Return {enumerator: int} for the C++ enum named cpp_type (last
    component of a qualified name) found in exactly one of the headers."""
    name = cpp_type.split('::')[-1].strip()
    pattern = re.compile(r'\benum\s+(?:class\s+|struct\s+)?' + re.escape(name) + r'\b[^{;]*\{')
    found = []
    for path, content in headers:
        code = blank_comments(content)
        for m in pattern.finditer(code):
            body_end = find_matching(code, m.end() - 1)
            found.append((path, code[m.end():body_end]))
    if not found:
        raise StubError(f"enum {cpp_type}: definition not found in any --enum-header")
    if len(found) > 1:
        raise StubError(f"enum {cpp_type}: ambiguous, defined in " + ", ".join(p for p, _ in found))
    values = {}
    next_value = 0
    for entry in split_top_level(found[0][1]):
        if '=' in entry:
            ident, expr = [s.strip() for s in entry.split('=', 1)]
            if re.fullmatch(r'0[xX][0-9a-fA-F]+', expr):
                next_value = int(expr, 16)
            elif re.fullmatch(r'-?\d+', expr):
                next_value = int(expr)
            elif expr in values:
                next_value = values[expr]
            else:
                raise StubError(f"enum {cpp_type}: cannot evaluate '{entry}'")
        else:
            ident = entry.strip()
        if not re.fullmatch(r'[A-Za-z_]\w*', ident):
            raise StubError(f"enum {cpp_type}: unexpected enumerator '{entry}'")
        values[ident] = next_value
        next_value += 1
    return values


# ---------------------------------------------------------------------------
# Docstring grammar
# ---------------------------------------------------------------------------

def parse_attribute_doc(doc, where):
    m = re.match(r'\s*([^:()]+?)\s*:\s*(.*)$', doc, re.S)
    if not m:
        raise StubError(f"{where}: docstring must be '<python type>: <description>', got {doc!r}")
    return m.group(1).strip(), ' '.join(m.group(2).split())


def parse_method_doc(doc, where):
    doc = doc.strip()
    if not doc.startswith('('):
        raise StubError(f"{where}: method docstring must start with '(<params>) -> <type>: ...', got {doc!r}")
    close = find_matching(doc, 0)
    params = doc[1:close].strip()
    rest = doc[close + 1:].strip()
    m = re.match(r'->\s*([^:]+?)\s*:\s*(.*)$', rest, re.S)
    if not m:
        raise StubError(f"{where}: method docstring needs '-> <return type>: <description>', got {doc!r}")
    return params, m.group(1).strip(), ' '.join(m.group(2).split())


def param_names(params):
    names = []
    for p in split_top_level(params):
        m = re.match(r'\*{0,2}([A-Za-z_]\w*)', p)
        if not m:
            raise StubError(f"cannot parse parameter {p!r}")
        names.append(m.group(1))
    return names


# ---------------------------------------------------------------------------
# Module parsing
# ---------------------------------------------------------------------------

class Attribute:
    def __init__(self, name, pytype, desc, writable):
        self.name, self.pytype, self.desc, self.writable = name, pytype, desc, writable


class Method:
    def __init__(self, name, params, ret, desc):
        self.name, self.params, self.ret, self.desc = name, params, ret, desc


class ClassStub:
    def __init__(self, name, doc):
        self.name, self.doc = name, doc
        self.attributes = []
        self.methods = []


class EnumStub:
    def __init__(self, name, doc):
        self.name, self.doc = name, doc
        self.values = []  # (name, int, desc)


class ExceptionStub:
    def __init__(self, name, base):
        self.name, self.base, self.doc = name, base, ''


class ModuleStub:
    def __init__(self):
        self.attributes = []  # Attribute
        self.functions = []   # Method
        self.enums = []
        self.classes = []
        self.exceptions = []  # ExceptionStub


def extract_module_body(src):
    m = re.search(r'PYBIND11_EMBEDDED_MODULE\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*\{', src)
    if not m:
        raise StubError("PYBIND11_EMBEDDED_MODULE block not found")
    code = blank_comments(src)
    body_end = find_matching(code, m.end() - 1)
    return m.group(1), m.group(2), code[m.end():body_end], src[m.end():body_end]


def parse_module(src, headers):
    module_name, var, code, raw = extract_module_body(src)
    stub = ModuleStub()
    errors = []
    exception_vars = {}
    attr_re = re.compile(r'^\s*' + re.escape(var) + r'\s*\.\s*attr\s*\(\s*"([^"]+)"\s*\)\s*=')

    for stmt_code, stmt_raw in split_statements(code, raw):
        try:
            am = attr_re.match(stmt_code)
            if am:
                name = am.group(1)
                if name.startswith('_'):
                    continue
                last_line = stmt_raw.rstrip().split('\n')[-1]
                cm = re.search(r'//\s*stub:\s*([^\s]+)\s+(.*)$', last_line)
                if not cm:
                    raise StubError(f"module attribute '{name}': needs a trailing '// stub: <type>  <description>' comment")
                stub.attributes.append(Attribute(name, cm.group(1), ' '.join(cm.group(2).split()), False))
                continue

            if stmt_code.startswith('using ') or stmt_code.startswith('typedef '):
                continue

            # `<var>.attr("__doc__") = "..."` documents an exception registered just before it.
            dm = re.match(r'^\s*([A-Za-z_]\w*)\s*\.\s*attr\s*\(\s*"__doc__"\s*\)\s*=\s*(.*)$', stmt_code, re.S)
            if dm:
                doc = string_literal_value(dm.group(2))
                exc = exception_vars.get(dm.group(1))
                if exc is None or doc is None:
                    raise StubError(f"__doc__ assignment on '{dm.group(1)}' does not follow a py::register_exception")
                exc.doc = ' '.join(doc.split())
                continue

            # Plain C++ assignments (for example caching a type pointer) carry no Python surface.
            if re.match(r'^\s*[A-Za-z_][\w:]*\s*=', stmt_code) and not stmt_code.lstrip().startswith('auto'):
                continue

            # `auto &name = py::register_exception<T>(m, "Name", PyExc_Base)` declares an exception class.
            rm = re.match(r'^\s*auto\s*&?\s*([A-Za-z_]\w*)\s*=\s*(py::register_exception<.*)$', stmt_code, re.S)
            if rm:
                head, head_args, chain = parse_call_chain(rm.group(2))
                py_name = string_literal_value(head_args[1])
                base = head_args[2].strip() if len(head_args) > 2 else 'PyExc_Exception'
                base = base[len('PyExc_'):] if base.startswith('PyExc_') else base
                exc = ExceptionStub(py_name, base)
                exception_vars[rm.group(1)] = exc
                stub.exceptions.append(exc)
                continue

            head, head_args, chain = parse_call_chain(stmt_code)
            head_compact = re.sub(r'\s+', '', head)

            if head_compact.startswith('py::exec') or head_compact == f'{var}.doc':
                continue

            if head_compact == f'{var}.def':
                name = string_literal_value(head_args[0])
                if name is None or name.startswith('_'):
                    continue
                doc = string_literal_value(head_args[-1]) if len(head_args) > 1 else None
                if doc is None:
                    raise StubError(f"module function '{name}': missing docstring")
                params, ret, desc = parse_method_doc(doc, f"module function '{name}'")
                stub.functions.append(Method(name, params, ret, desc))
                continue

            em = re.match(r'py::enum_<(.+)>$', head_compact)
            if em:
                cpp_type = em.group(1)
                py_name = string_literal_value(head_args[1])
                doc = string_literal_value(head_args[2]) if len(head_args) > 2 else None
                if doc is None:
                    raise StubError(f"enum '{py_name}': py::enum_ needs a docstring argument")
                enum = EnumStub(py_name, ' '.join(doc.split()))
                cpp_values = parse_enum_values(headers, cpp_type)
                for method, args in chain:
                    if method == 'export_values':
                        continue
                    if method != 'value':
                        raise StubError(f"enum '{py_name}': unsupported call .{method}()")
                    vname = string_literal_value(args[0])
                    member = args[1].split('::')[-1].strip()
                    if member not in cpp_values:
                        raise StubError(f"enum '{py_name}': C++ enumerator '{args[1]}' not found in {cpp_type}")
                    vdoc = string_literal_value(args[2]) if len(args) > 2 else ''
                    enum.values.append((vname, cpp_values[member], ' '.join(vdoc.split())))
                stub.enums.append(enum)
                continue

            if head_compact.startswith('py::class_<'):
                py_name = string_literal_value(head_args[1])
                doc = string_literal_value(head_args[2]) if len(head_args) > 2 else None
                if doc is None:
                    errors.append(f"class '{py_name}': py::class_ needs a docstring argument")
                    doc = ''
                cls = ClassStub(py_name, ' '.join(doc.split()))
                for method, args in chain:
                    try:
                        name = string_literal_value(args[0]) if args else None
                        if name is None:
                            raise StubError(f"class '{py_name}': .{method}() first argument must be a name literal")
                        if name.startswith('_'):
                            continue
                        where = f"{py_name}.{name}"
                        doc = string_literal_value(args[-1]) if len(args) > 1 else None
                        if doc is None:
                            raise StubError(f"{where}: missing docstring (last argument of .{method}())")
                        if method in ('def_readonly', 'def_readwrite', 'def_property_readonly', 'def_property'):
                            pytype, desc = parse_attribute_doc(doc, where)
                            writable = method in ('def_readwrite', 'def_property')
                            cls.attributes.append(Attribute(name, pytype, desc, writable))
                        elif method == 'def':
                            params, ret, desc = parse_method_doc(doc, where)
                            declared = re.findall(r'py::arg\s*\(\s*"([^"]+)"\s*\)', ' '.join(args[1:-1]))
                            if declared:
                                documented = param_names(params)
                                if declared != documented:
                                    raise StubError(f"{where}: py::arg names {declared} do not match documented parameters {documented}")
                            cls.methods.append(Method(name, params, ret, desc))
                        else:
                            raise StubError(f"{where}: unsupported binding call .{method}()")
                    except StubError as e:
                        errors.append(str(e))
                stub.classes.append(cls)
                continue

            raise StubError(f"unrecognized statement in module body: {stmt_code[:70]!r}")
        except StubError as e:
            errors.append(str(e))

    for exc in stub.exceptions:
        if not exc.doc:
            errors.append(f"exception '{exc.name}': needs a following <var>.attr(\"__doc__\") = \"...\" statement")
    if errors:
        raise StubError("stub contract violations in the pybind11 module:\n  - " + "\n  - ".join(errors))
    return module_name, stub


# ---------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------

def emit_stub(module_name, stub, settings_class):
    out = []
    w = out.append
    w(f'# {module_name} preprocessing API type stub. GENERATED FILE, do not edit.')
    w('#')
    w('# Built by build_stubs.py from the pybind11 module in')
    w('# src/luminary/gcode/scripting/PreProcessor.cpp (bindings and their docstrings),')
    w('# the C++ enum headers (numeric values) and PrintConfig.hpp (Settings).')
    w('#')
    w('# Place this file next to your script so the editor resolves')
    w(f'# "import {module_name}" for autocomplete. At slice time the embedded module')
    w('# takes precedence; this file is never executed by preFlight.')
    w('#')
    w('# Entry points a script may define:')
    w('#   def process(gcode: GCode) -> None       preprocessing, runs after slicing')
    w('#   def export(gcode: ExportGCode) -> None  Export to Script, receives the final text')
    w('')
    w('from __future__ import annotations')
    w('')
    w('from enum import IntEnum')
    w('from typing import Callable, Dict, List, Optional, Tuple')
    w('')
    for a in stub.attributes:
        w(f'{a.name}: {a.pytype}  # {a.desc}')
    for f in stub.functions:
        w('')
        w(f'def {f.name}({f.params}) -> {f.ret}:')
        w(f'    """{f.desc}"""')
        w('    ...')

    for x in stub.exceptions:
        w('')
        w('')
        w(f'class {x.name}({x.base}):')
        w(f'    """{x.doc}"""')

    for e in stub.enums:
        w('')
        w('')
        w(f'class {e.name}(IntEnum):')
        w(f'    """{e.doc}"""')
        for name, value, desc in sorted(e.values, key=lambda v: v[1]):
            w(f'    {name} = {value}' + (f'  # {desc}' if desc else ''))

    for c in stub.classes:
        if c.name == 'GCode':
            w('')
            w('')
            w(settings_class)
        w('')
        w('')
        w(f'class {c.name}:')
        w(f'    """{c.doc}"""')
        rw = [a for a in c.attributes if a.writable]
        ro = [a for a in c.attributes if not a.writable]
        if rw:
            w('')
            w('    # Read/write')
            for a in rw:
                w(f'    {a.name}: {a.pytype}  # {a.desc}')
        if ro:
            w('')
            w('    # Read-only')
            for a in ro:
                w(f'    {a.name}: {a.pytype}  # {a.desc}')
        for m in c.methods:
            w('')
            params = f'self, {m.params}' if m.params else 'self'
            w(f'    def {m.name}({params}) -> {m.ret}:')
            w(f'        """{m.desc}"""')
            w('        ...')
    w('')
    return '\n'.join(out)


def write_if_changed(path, content):
    try:
        with open(path, 'r', encoding='utf-8') as f:
            if f.read() == content:
                return False
    except FileNotFoundError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--config', required=True, help='PrintConfig.hpp')
    ap.add_argument('--bindings', required=True, help='PreProcessor.cpp (the pybind11 module)')
    ap.add_argument('--enum-header', action='append', default=[], help='header defining an exposed C++ enum')
    ap.add_argument('--out', action='append', required=True, help='output directory for preFlight.py')
    args = ap.parse_args()

    with open(args.config, 'r', encoding='utf-8') as f:
        hpp_content = f.read()
    with open(args.bindings, 'r', encoding='utf-8') as f:
        bindings = f.read()
    headers = []
    for path in args.enum_header:
        with open(path, 'r', encoding='utf-8') as f:
            headers.append((path, f.read()))

    options = extract_options(hpp_content)
    if not options:
        print("ERROR: no config options found in PrintConfig.hpp", file=sys.stderr)
        sys.exit(1)

    try:
        module_name, stub = parse_module(bindings, headers)
    except StubError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    content = emit_stub(module_name, stub, generate_settings_class(options))

    bindings_count = sum(len(c.attributes) + len(c.methods) for c in stub.classes)
    enum_count = sum(len(e.values) for e in stub.enums)
    written = 0
    for out_dir in args.out:
        path = os.path.join(out_dir, f"{module_name}.py")
        if write_if_changed(path, content):
            written += 1
            print(f"Generated {module_name}.py: {len(stub.classes)} classes, {bindings_count} members, "
                  f"{len(stub.enums)} enums ({enum_count} values), {len(options)} settings -> {path}")
    if written == 0:
        print("All stubs up to date")


if __name__ == "__main__":
    main()
