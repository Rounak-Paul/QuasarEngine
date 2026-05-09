#!/usr/bin/env python3
"""
Replaces raw malloc/calloc/realloc/free/strdup calls with qs_* equivalents,
adding the specified memory tag as the last argument.
Usage: python3 refactor_allocs.py <file> <TAG>
       python3 refactor_allocs.py --check <file>  (just report remaining raw calls)
"""

import sys
import re
import os

def find_matching_paren(s, start):
    """Find the index of the closing ')' that matches the '(' at s[start]."""
    assert s[start] == '('
    depth = 0
    i = start
    while i < len(s):
        if s[i] == '(':
            depth += 1
        elif s[i] == ')':
            depth -= 1
            if depth == 0:
                return i
        elif s[i] == '"':
            # Skip string literals
            i += 1
            while i < len(s) and s[i] != '"':
                if s[i] == '\\':
                    i += 1
                i += 1
        i += 1
    return -1  # unmatched


def transform_line(line, tag):
    """Transform a single line, replacing raw alloc calls with qs_* equivalents."""
    result = []
    i = 0
    while i < len(line):
        # Look for free(  →  qs_free(
        m = re.match(r'\bfree\(', line[i:])
        if m and (i == 0 or not line[i-1].isalnum() and line[i-1] != '_'):
            # Check it's not qs_free already
            if i >= 3 and line[i-3:i] == 'qs_':
                result.append(line[i])
                i += 1
                continue
            result.append('qs_free(')
            i += len('free(')
            continue

        # Look for malloc(  →  qs_malloc(<args>, TAG)
        m = re.match(r'\bmalloc\(', line[i:])
        if m and (i == 0 or not line[i-1].isalnum() and line[i-1] != '_'):
            paren_start = i + len('malloc')
            paren_end = find_matching_paren(line, paren_start)
            if paren_end != -1:
                inner = line[paren_start+1:paren_end]
                result.append(f'qs_malloc({inner}, {tag})')
                i = paren_end + 1
                continue

        # Look for calloc(  →  qs_calloc(<args>, TAG)
        m = re.match(r'\bcalloc\(', line[i:])
        if m and (i == 0 or not line[i-1].isalnum() and line[i-1] != '_'):
            paren_start = i + len('calloc')
            paren_end = find_matching_paren(line, paren_start)
            if paren_end != -1:
                inner = line[paren_start+1:paren_end]
                result.append(f'qs_calloc({inner}, {tag})')
                i = paren_end + 1
                continue

        # Look for realloc(  →  qs_realloc(<args>, TAG)
        m = re.match(r'\brealloc\(', line[i:])
        if m and (i == 0 or not line[i-1].isalnum() and line[i-1] != '_'):
            paren_start = i + len('realloc')
            paren_end = find_matching_paren(line, paren_start)
            if paren_end != -1:
                inner = line[paren_start+1:paren_end]
                result.append(f'qs_realloc({inner}, {tag})')
                i = paren_end + 1
                continue

        # Look for strdup(  →  qs_strdup(<args>, TAG)
        m = re.match(r'\bstrdup\(', line[i:])
        if m and (i == 0 or not line[i-1].isalnum() and line[i-1] != '_'):
            paren_start = i + len('strdup')
            paren_end = find_matching_paren(line, paren_start)
            if paren_end != -1:
                inner = line[paren_start+1:paren_end]
                result.append(f'qs_strdup({inner}, {tag})')
                i = paren_end + 1
                continue

        result.append(line[i])
        i += 1

    return ''.join(result)


def word_boundary_before(text, pos):
    """Return True if the character before pos is not alphanumeric/underscore."""
    if pos == 0:
        return True
    c = text[pos - 1]
    return not (c.isalnum() or c == '_')


def transform_text(text, tag):
    """Transform entire file text, handling multi-line calls and skipping comments/strings."""
    lines = text.split('\n')
    out = []
    for line in lines:
        # Skip comment lines
        stripped = line.lstrip()
        if stripped.startswith('//') or stripped.startswith('*') or stripped.startswith('/*'):
            out.append(line)
            continue
        out.append(transform_line(line, tag))
    return '\n'.join(out)


def check_remaining(text, filepath):
    """Report any remaining raw malloc/calloc/realloc/free/strdup calls."""
    pattern = re.compile(r'\b(malloc|calloc|realloc|free|strdup)\s*\(')
    found = False
    for i, line in enumerate(text.split('\n'), 1):
        stripped = line.lstrip()
        # Skip comment lines
        if stripped.startswith('//') or stripped.startswith('*') or stripped.startswith('/*'):
            continue
        for m in pattern.finditer(line):
            # Check if it's already prefixed with qs_
            start = m.start()
            if start >= 3 and line[start-3:start] == 'qs_':
                continue
            # Check for ca_ prefix (Causality)
            if start >= 3 and line[start-3:start] == 'ca_':
                continue
            print(f"  {filepath}:{i}: {line.rstrip()}")
            found = True
    return found


def main():
    args = sys.argv[1:]
    
    if args and args[0] == '--check':
        # Check mode: report remaining raw calls
        files = args[1:]
        any_found = False
        for f in files:
            with open(f, 'r') as fh:
                text = fh.read()
            if check_remaining(text, f):
                any_found = True
        if not any_found:
            print("No remaining raw alloc calls found.")
        return

    if len(args) != 2:
        print(f"Usage: {sys.argv[0]} <file> <TAG>")
        print(f"       {sys.argv[0]} --check <file> [<file2> ...]")
        sys.exit(1)

    filepath, tag = args

    with open(filepath, 'r') as f:
        original = f.read()

    transformed = transform_text(original, tag)

    if transformed == original:
        print(f"No changes needed in {filepath}")
        return

    # Write backup
    backup = filepath + '.bak'
    with open(backup, 'w') as f:
        f.write(original)

    with open(filepath, 'w') as f:
        f.write(transformed)

    # Count changes
    orig_lines = original.split('\n')
    new_lines = transformed.split('\n')
    changed = sum(1 for a, b in zip(orig_lines, new_lines) if a != b)
    print(f"Transformed {filepath}: {changed} line(s) changed (backup: {backup})")


if __name__ == '__main__':
    main()
