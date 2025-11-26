#!/usr/bin/env python3
"""
compare_libvita2d.py

Compare API symbols between reference/libvita2d_sys and third_party/libvita2d
Usage:
    python3 scripts/compare_libvita2d.py [--reference PATH] [--third PATH] [--report OUT]

Produces a report (text/JSON) of missing function prototypes and mismatching signatures.
It focuses on functions prefixed with 'vita2d_' (lib API) and other Sce APIs.
"""

import os
import re
import argparse
import json

# heuristics for extracting prototypes from header files
PROTOTYPE_REGEX = re.compile(r"^\s*(?:PRX_INTERFACE\s+)?([A-Za-z_][A-Za-z0-9_\s\*]+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*;\s*$")
# also capture C-style functions in general from headers
GENERIC_PROTOTYPE_REGEX = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_\s\*]+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{]*)\)\s*;\s*$")
# function defs in source - simple
FUNC_DEF_REGEX = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_\s\*]+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;]*)\)\s*\{\s*$")

# filter only relevant functions (vita2d_* and those we care about)
def is_relevant(name):
    return name.startswith('vita2d_') or name.startswith('sceGxm') or name.startswith('sceGxt') or name.startswith('scePng')


def parse_header_prototypes(header_path):
    prototypes = {}
    with open(header_path, 'r', encoding='utf-8', errors='ignore') as fh:
        for line in fh:
            m = PROTOTYPE_REGEX.match(line) or GENERIC_PROTOTYPE_REGEX.match(line)
            if not m:
                continue
            ret_type = m.group(1).strip()
            name = m.group(2).strip()
            args = m.group(3).strip()
            if is_relevant(name):
                prototypes[name] = {
                    'ret_type': ret_type,
                    'args': args,
                    'header': header_path
                }
    return prototypes


def parse_dir_headers(root):
    prototypes = {}
    for dirpath, _, filenames in os.walk(root):
        for fname in filenames:
            if fname.endswith('.h') or fname.endswith('.hpp'):
                path = os.path.join(dirpath, fname)
                try:
                    p = parse_header_prototypes(path)
                    prototypes.update(p)
                except Exception as e:
                    print(f"Warning: failed to parse {path}: {e}")
    return prototypes


def parse_source_definitions(root):
    defs = {}
    for dirpath, _, filenames in os.walk(root):
        for fname in filenames:
            if fname.endswith('.c') or fname.endswith('.cpp') or fname.endswith('.cc'):
                path = os.path.join(dirpath, fname)
                try:
                    with open(path, 'r', encoding='utf-8', errors='ignore') as fh:
                        for i, line in enumerate(fh):
                            m = FUNC_DEF_REGEX.match(line)
                            if m:
                                ret_type = m.group(1).strip()
                                name = m.group(2).strip()
                                args = m.group(3).strip()
                                if is_relevant(name):
                                    defs[name] = {
                                        'ret_type': ret_type,
                                        'args': args,
                                        'file': path,
                                        'line': i+1
                                    }
                except Exception as e:
                    print(f"Warning: failed to scan {path}: {e}")
    return defs


def diff_apis(ref_headers, third_headers, third_defs):
    missing_in_third = []
    mismatched_sigs = []

    for name, proto in ref_headers.items():
        if name not in third_headers and name not in third_defs:
            missing_in_third.append({'name': name, 'ref_header': proto['header'], 'ref_ret': proto['ret_type'], 'ref_args': proto['args']})
        else:
            # check for signature mismatch (if present in third header)
            if name in third_headers:
                th = third_headers[name]
                if proto['ret_type'] != th['ret_type'] or canonicalize_args(proto['args']) != canonicalize_args(th['args']):
                    mismatched_sigs.append({'name': name, 'ref': proto, 'third': th})

    return missing_in_third, mismatched_sigs


def canonicalize_args(args):
    # Remove comments, multiple spaces, parameter names
    # Very heuristic but enough for rough comparisons
    args = re.sub(r"/\*.*?\*/", '', args)
    args = re.sub(r"//.*$", '', args)
    args = re.sub(r"\b[A-Za-z_][A-Za-z0-9_]*\s*(,|$)", ',', args)  # naive strip names
    args = re.sub(r"\s+", ' ', args).strip()
    return args


def build_report(missing, mismatched, out_file=None):
    report = {
        'missing_count': len(missing),
        'missing': missing,
        'mismatched_count': len(mismatched),
        'mismatched': mismatched
    }
    if out_file:
        with open(out_file, 'w', encoding='utf-8') as fh:
            json.dump(report, fh, indent=2)
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', '-r', help='Reference lib path', default='reference/libvita2d_sys/libvita2d_sys')
    parser.add_argument('--third', '-t', help='Third party path', default='third_party/libvita2d/libvita2d')
    parser.add_argument('--report', default='reports/compare_libvita2d.json')
    parser.add_argument('--gen-stubs', action='store_true', help='Generate stub source file for missing functions (into reports/patches)')
    parser.add_argument('--append-prototypes', action='store_true', help='Generate an append file that includes missing prototypes (reports/patches/prototypes.h)')
    parser.add_argument('--threshold', type=int, default=1000, help='Max symbols to inspect to avoid huge runs')
    args = parser.parse_args()

    ref_path = os.path.abspath(args.reference)
    third_path = os.path.abspath(args.third)

    print(f"Scanning reference headers under {ref_path}/include...")
    ref_headers = parse_dir_headers(os.path.join(ref_path, 'include'))
    print(f"Found {len(ref_headers)} relevant function prototypes in reference headers.")

    print(f"Scanning third_party headers under {third_path}/include...")
    third_headers = parse_dir_headers(os.path.join(third_path, 'include'))
    print(f"Found {len(third_headers)} relevant function prototypes in third_party headers.")

    print(f"Scanning third_party sources under {third_path}/source for definitions...")
    third_defs = parse_source_definitions(os.path.join(third_path, 'source'))
    print(f"Found {len(third_defs)} relevant function definitions in third_party sources.")

    missing, mismatched = diff_apis(ref_headers, third_headers, third_defs)

    print(f"Missing in third_party: {len(missing)}; Mismatched signature entries: {len(mismatched)}")

    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    report = build_report(missing, mismatched, args.report)
    print(f"Report generated: {args.report}")

    # print a concise summary to stdout
    if missing:
        print("\nMissing functions:")
        for m in missing:
            print(f"- {m['name']} declared in {m['ref_header']}")
    if mismatched:
        print("\nMismatched signatures:")
        for mm in mismatched:
            print(f"- {mm['name']}\n  ref: {mm['ref']['ret_type']} {mm['name']}({mm['ref']['args']})\n  third: {mm['third']['ret_type']} {mm['name']}({mm['third']['args']})")

    # optional stubs and prototype generation
    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    patches_dir = os.path.join(os.path.dirname(args.report), 'patches')
    if args.gen_stubs and missing:
        os.makedirs(patches_dir, exist_ok=True)
        stub_file = os.path.join(patches_dir, 'vita2d_compat_stubs.c')
        with open(stub_file, 'w', encoding='utf-8') as fh:
            fh.write('/* Auto-generated stubs for missing vita2d API functions. */\n')
            fh.write('#include "vita2d.h"\n')
            fh.write('#include <stdio.h>\n\n')
            for m in missing:
                name = m['name']
                ret = m['ref_ret']
                proto_args = m['ref_args']
                # simplify empty args
                if not proto_args or proto_args.strip() == 'void':
                    args_form = 'void'
                else:
                    args_form = proto_args
                fh.write(f"{ret} {name}({args_form}) {{\n")
                # if int return, return -1, else if void, empty; else return 0
                if ret.strip().startswith('int'):
                    fh.write('    /* TODO: implement or map to existing function */\n')
                    fh.write('    return -1;\n')
                elif ret.strip().startswith('void'):
                    fh.write('    /* TODO: implement or map to existing function */\n')
                else:
                    fh.write('    /* TODO: implement or map to existing function */\n')
                    fh.write('    return 0;\n')
                fh.write('}\n\n')
        print(f"Stub source generated: {stub_file}")

    if args.append_prototypes and missing:
        os.makedirs(patches_dir, exist_ok=True)
        prototypes_file = os.path.join(patches_dir, 'prototypes_to_add.h')
        with open(prototypes_file, 'w', encoding='utf-8') as fh:
            fh.write('/* Prototypes to append to vita2d.h - verify duplicates and ordering. */\n\n')
            for m in missing:
                name = m['name']
                ret = m['ref_ret']
                proto_args = m['ref_args']
                if not proto_args or proto_args.strip() == 'void':
                    args_form = 'void'
                else:
                    args_form = proto_args
                fh.write(f"{ret} {name}({args_form});\n")
        print(f"Prototypes to append generated: {prototypes_file}")
    # Generate suggestions for mismatched signatures
    if mismatched:
        os.makedirs(patches_dir, exist_ok=True)
        suggestions_file = os.path.join(patches_dir, 'mismatched_suggestions.txt')
        with open(suggestions_file, 'w', encoding='utf-8') as fh:
            fh.write('Mismatched signatures and suggested changes:\n\n')
            for mm in mismatched:
                name = mm['name']
                ref_sig = f"{mm['ref']['ret_type']} {name}({mm['ref']['args']})"
                third_sig = f"{mm['third']['ret_type']} {name}({mm['third']['args']})"
                fh.write(f"Function: {name}\n")
                fh.write(f"  Reference: {ref_sig}\n")
                fh.write(f"  Third party: {third_sig}\n")
                fh.write('  Suggestion: adjust third-party signature to match reference and provide compatibility wrapper if needed.\n\n')
        print(f"Mismatched signature suggestions generated: {suggestions_file}")


if __name__ == '__main__':
    main()
