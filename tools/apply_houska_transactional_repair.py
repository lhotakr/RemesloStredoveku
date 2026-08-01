#!/usr/bin/env python3
from __future__ import annotations

import json
import shutil
import sys
from datetime import datetime
from pathlib import Path

CANONICAL_MAP = Path('data/castles/houska_1400/maps/houska_courtyard_ground.map.json')
CASTLE_ROOT = Path('data/castles/houska_1400')
EXPECTED_LABELS = {'A'} | {str(i) for i in range(1, 16)}


def die(message: str) -> None:
    print(f'ERROR: {message}')
    raise SystemExit(1)


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except Exception as exc:
        die(f'Nelze načíst JSON {path}: {exc}')


def main() -> None:
    root = Path.cwd().resolve()
    package_root = Path(__file__).resolve().parents[1]
    payload = package_root / 'repair_payload'

    required = [
        root / 'src/game/BuildInteriorEngine.cpp',
        root / 'src/game/BuildInteriorEngine.h',
        root / CASTLE_ROOT,
        payload / 'src/game/BuildInteriorEngine.cpp',
        payload / 'src/game/BuildInteriorEngine.h',
    ]
    missing = [str(p) for p in required if not p.exists()]
    if missing:
        die('Spusť skript z kořene projektu. Chybí: ' + ', '.join(missing))

    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    backup = root / f'backup_houska_transactional_{stamp}'
    backup.mkdir(parents=True, exist_ok=True)

    changed: list[str] = []
    removed: list[str] = []

    def backup_path(path: Path) -> None:
        if not path.exists() or not path.is_file():
            return
        try:
            rel = path.relative_to(root)
        except ValueError:
            return
        dst = backup / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dst)

    # Copy exact replacements prepared against the uploaded current version.
    for src in sorted(payload.rglob('*')):
        if not src.is_file():
            continue
        rel = src.relative_to(payload)
        dst = root / rel
        backup_path(dst)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        changed.append(rel.as_posix())

    # Remove obsolete courtyard prototypes and generated cache. Everything is backed up first.
    obsolete: set[Path] = set()
    exact_obsolete = [
        root / 'src/game/BuildInteriorEngine.cpp.before_houska_v3_editor_fix',
        root / CASTLE_ROOT / 'maps/houska_plan_angled_prototype.map.json',
        root / CASTLE_ROOT / 'entities/houska_plan_angled_prototype.entities.json',
        root / CASTLE_ROOT / 'geometry/houska_plan_angled_prototype.geometry.json',
        root / CASTLE_ROOT / 'navigation/houska_plan_angled_prototype.navigation.json',
        root / CASTLE_ROOT / 'narrative/houska_plan_angled_prototype.narrative.json',
    ]
    obsolete.update(exact_obsolete)

    patterns = [
        'data/interiors/houska_courtyard*.json',
        'data/castles/houska_1400/maps/*courtyard*proposal*.json',
        'data/castles/houska_1400/maps/houska_courtyard*_v*.json',
        'data/castles/houska_1400/entities/houska_courtyard*_v*.json',
        'data/castles/houska_1400/navigation/houska_courtyard*_v*.json',
        'data/castles/houska_1400/narrative/houska_courtyard*_v*.json',
        'data/castles/houska_1400/geometry/houska_courtyard*_v*.json',
    ]
    for pattern in patterns:
        obsolete.update(root.glob(pattern))

    canonical_files = {
        (root / CANONICAL_MAP).resolve(),
        (root / CASTLE_ROOT / 'entities/houska_courtyard_ground.entities.json').resolve(),
        (root / CASTLE_ROOT / 'navigation/houska_courtyard_ground.navigation.json').resolve(),
        (root / CASTLE_ROOT / 'narrative/houska_courtyard_ground.narrative.json').resolve(),
        (root / CASTLE_ROOT / 'geometry/houska_courtyard_ground.geometry.json').resolve(),
    }

    for path in sorted(obsolete):
        if not path.exists() or not path.is_file() or path.resolve() in canonical_files:
            continue
        backup_path(path)
        path.unlink()
        removed.append(path.relative_to(root).as_posix())

    compiled = root / CASTLE_ROOT / '.compiled'
    if compiled.exists():
        for path in sorted(compiled.glob('*.json')):
            backup_path(path)
            path.unlink()
            removed.append(path.relative_to(root).as_posix())

    # Validation against the installed files.
    materials_data = read_json(root / CASTLE_ROOT / 'materials.json')
    material_ids = {
        item.get('id') for item in materials_data.get('materials', [])
        if isinstance(item, dict) and item.get('id')
    }
    if 'campfire' not in material_ids:
        die('Materiál campfire po instalaci chybí.')

    forecourt_entities = read_json(root / CASTLE_ROOT / 'entities/houska_forecourt.entities.json')
    unknown = sorted({
        entity.get('material') for entity in forecourt_entities.get('entities', [])
        if isinstance(entity, dict) and entity.get('material') not in material_ids
    })
    if unknown:
        die('Předhradí stále používá neznámé materiály: ' + ', '.join(unknown))

    courtyard = read_json(root / CANONICAL_MAP)
    labels = {
        str(room.get('label')) for room in courtyard.get('rooms', [])
        if isinstance(room, dict)
    }
    if courtyard.get('geometry_mode') != 'polygon' or not EXPECTED_LABELS.issubset(labels):
        die('Kanonická mapa nádvoří nemá polygonální místnosti A a 1–15.')

    forecourt = read_json(root / CASTLE_ROOT / 'maps/houska_forecourt.map.json')
    portal_ok = any(
        isinstance(door, dict)
        and door.get('target_location') == 'castle:houska_1400/houska_courtyard_ground'
        and door.get('target_spawn') in {'south_gate', 'south_gate_spawn'}
        and not door.get('locked', False)
        for door in forecourt.get('doors', [])
    )
    if not portal_ok:
        die('Portál z předhradí do kanonického nádvoří není správně nastaven.')

    overlay = read_json(root / CASTLE_ROOT / 'geometry/houska_courtyard_ground.geometry.json')
    polygon_ids = {
        p.get('id') for p in overlay.get('polygon_sectors', [])
        if isinstance(p, dict)
    }
    room_ids = {
        room.get('id') for room in courtyard.get('rooms', [])
        if isinstance(room, dict)
    }
    if polygon_ids != room_ids:
        die('Editorový polygonální overlay neodpovídá místnostem kanonické mapy.')

    report = root / 'HOUSKA_TRANSACTIONAL_REPAIR_REPORT.txt'
    report.write_text(
        '\n'.join([
            'Houska transactional editor repair',
            f'Backup: {backup.name}',
            '',
            'Changed:',
            *[f'  + {x}' for x in changed],
            '',
            'Removed obsolete/cache:',
            *([f'  - {x}' for x in removed] or ['  (none)']),
            '',
            'Validation:',
            '  OK campfire material',
            '  OK forecourt entity materials',
            '  OK forecourt -> courtyard portal',
            '  OK canonical rooms A + 1..15',
            f'  OK editor polygon overlay ({len(polygon_ids)} rooms)',
            '',
            'Next: rebuild the project, then load Houska – nádvoří V3 podle půdorysu místností 1–15.',
        ]) + '\n',
        encoding='utf-8'
    )

    print('Hotovo.')
    print(f'Záloha: {backup}')
    print(f'Změněno: {len(changed)} souborů')
    print(f'Odstraněno: {len(removed)} starých/cache souborů')
    print(f'Report: {report}')


if __name__ == '__main__':
    main()
