#!/usr/bin/env python3
"""Cria relatórios sem sobrescrever e resume CSV frame_ms usando só stdlib."""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics

SKILL = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    init = commands.add_parser('init')
    init.add_argument('root', type=Path)
    init.add_argument('id')
    summary = commands.add_parser('summarize')
    summary.add_argument('csv', type=Path)
    args = parser.parse_args()
    if args.command == 'init':
        if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,79}', args.id):
            parser.error('ID deve ter 1–80 letras minúsculas, números ou hífens')
        root = args.root.resolve()
        if not (root / 'project.godot').is_file():
            parser.error('raiz sem project.godot')
        target = root / 'docs/performance/caca-as-bruxas' / args.id
        target.mkdir(parents=True, exist_ok=False)
        (target / 'report.md').write_text((SKILL / 'templates/operation.md').read_text(), encoding='utf-8')
        (target / 'handoffs').mkdir()
        (target / 'raw').mkdir()
        print(target)
        return
    with args.csv.open(newline='', encoding='utf-8') as source:
        rows = csv.DictReader(source)
        if not rows.fieldnames or 'frame_ms' not in rows.fieldnames:
            parser.error('CSV exige coluna frame_ms')
        values = []
        for line, row in enumerate(rows, 2):
            try:
                value = float(row['frame_ms'])
            except (TypeError, ValueError):
                parser.error(f'frame_ms inválido na linha {line}')
            if not math.isfinite(value) or value <= 0:
                parser.error(f'frame_ms deve ser positivo e finito na linha {line}')
            values.append(value)
    if not values:
        parser.error('CSV sem amostras')
    values.sort()
    def percentile(p):
        return values[math.ceil(p * len(values)) - 1]
    print(json.dumps({'samples': len(values), 'median_ms': statistics.median(values),
        'p95_ms': percentile(.95), 'p99_ms': percentile(.99), 'max_ms': values[-1],
        'over_16_67_ms': sum(v > 16.67 for v in values),
        'over_33_33_ms': sum(v > 33.33 for v in values),
        'percentile_method': 'nearest-rank',
        'limitation': 'Resume amostras; não valida cenário, GPU, warmup ou causalidade.'}, indent=2))

if __name__ == '__main__':
    main()
