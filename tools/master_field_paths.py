"""Source-independent identities for explicitly reviewed MasterDB list items.

This is a field address codec, not a query language. Unlisted arrays retain
numeric positions; listed arrays require one unique business key per parent.
"""

from collections.abc import Mapping
import json
import re

from master_identity import IdentityError, business_values


# Paths are protobuf field names relative to one top-level record. A [] in a
# schema path denotes an enclosing list, independent of its actual selector.
NESTED_KEY_RULES = {
    **{(table, "levels"): (("level", "integer"),) for table in (
        "ActivityAbility", "ActivityFanEvent", "ActivityPromotion",
        "ActivityRefresh", "LiveAbility", "Skill")},
    ("Character", "activityFanEventWords"): (("voiceAssetId", "string"),),
    ("Character", "loveInfos"): (("loveId", "string"),),
    ("CharacterGroup", "mappings"): (("characterId", "string"),),
    ("ExerciseHint", "contents"): (("number", "integer"),),
    ("HelpCategory", "contents"): (("helpContentId", "string"),),
    ("HomeTalk", "characterTalks"): (("voiceAssetId", "string"),),
    ("Message", "details"): (("messageDetailId", "string"),),
    ("Stage", "cameraPositions"): (("index", "integer"),),
    ("Story", "branchChoices"): (("index", "integer"),),
    ("Tutorial", "stepInfo"): (("step", "integer"), ("subStep", "integer")),
}
NESTED_TABLES = {table for table, _ in NESTED_KEY_RULES}
NAME = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
INDEX = re.compile(r"(?:0|[1-9][0-9]*)\Z")


def _unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise IdentityError("Duplicate field in a MasterDB item selector")
        result[name] = value
    return result


DECODER = json.JSONDecoder(object_pairs_hook=_unique_object)


def _encoded(values):
    return json.dumps(values, ensure_ascii=False, separators=(",", ":"))


def _parts(path):
    if not isinstance(path, str) or not path or len(path) > 4096:
        raise IdentityError("Invalid MasterDB field path")
    position = 0
    while position < len(path):
        name = NAME.match(path, position)
        if name is None:
            raise IdentityError(f"Invalid MasterDB field path: {path!r}")
        position = name.end()
        selector = None
        if position < len(path) and path[position] == "[":
            position += 1
            if path.startswith("key=", position):
                position += 4
                start = position
                try:
                    selector, position = DECODER.raw_decode(path, position)
                except (ValueError, RecursionError) as exc:
                    raise IdentityError(f"Invalid MasterDB item selector: {path!r}") from exc
                if not isinstance(selector, dict) or path[start:position] != _encoded(selector):
                    raise IdentityError(f"Noncanonical MasterDB item selector: {path!r}")
            else:
                end = path.find("]", position)
                token = path[position:end] if end >= 0 else ""
                if len(token) > 20 or not INDEX.fullmatch(token):
                    raise IdentityError(f"Invalid MasterDB list index: {path!r}")
                selector, position = int(token), end
            if position >= len(path) or path[position] != "]":
                raise IdentityError(f"Unclosed MasterDB item selector: {path!r}")
            position += 1
        yield name.group(), selector
        if position == len(path):
            break
        if path[position] != "." or position + 1 == len(path):
            raise IdentityError(f"Invalid MasterDB field separator: {path!r}")
        position += 1


def _selector(table, schema_path, selector, *, allow_legacy=False):
    rule = NESTED_KEY_RULES.get((table, schema_path))
    label = f"{table}.{schema_path}"
    if isinstance(selector, dict):
        if rule is None:
            raise IdentityError(f"{label}: no reviewed nested identity rule")
        if list(selector) != [name for name, _ in rule]:
            raise IdentityError(f"{label}: unexpected business fields")
        normalized = business_values(label, selector, rule)
        if _encoded(normalized) != _encoded(selector):
            raise IdentityError(f"{label}: noncanonical business values")
    elif rule is not None and (selector is None or not allow_legacy):
        raise IdentityError(f"{label}: legacy positional path; migrate field keys first")
    return rule


def validate_field_path(table, path):
    """Validate stored path syntax and prohibit positional aliases for keyed lists."""
    schema = []
    for name, selector in _parts(path):
        _selector(table, ".".join([*schema, name]), selector)
        schema.append(name + ("[]" if selector is not None else ""))


def validate_field_table(table, records):
    for fields in records.values():
        for path in fields:
            validate_field_path(table, path)


def _value(owner, name):
    if isinstance(owner, Mapping):
        if name in owner:
            return owner[name]
    elif hasattr(owner, name):
        return getattr(owner, name)
    raise IdentityError(f"Unsupported protobuf field path component: {name}")


class FieldPaths:
    """Resolve JSON/protobuf fields using the same explicit nested identities."""

    def __init__(self, table, record):
        self.table = table
        self.record = record
        self._indexes = {}

    def _index(self, values, rule, schema_path, address):
        # Cache by the full parent address, not temporary protobuf wrapper IDs.
        if address not in self._indexes:
            indexed = {}
            for position, item in enumerate(values):
                key = _encoded(business_values(f"{self.table}.{schema_path}", item, rule))
                if key in indexed:
                    raise IdentityError(f"{self.table}.{schema_path}: duplicate nested identity {key}")
                indexed[key] = (position, item)
            self._indexes[address] = indexed
        return self._indexes[address]

    def locate(self, path, *, allow_legacy=False):
        """Return owner, property/index, current string and canonical address.

        Legacy positions for keyed arrays are allowed only when explicitly
        converting an already verified raw snapshot during export/migration.
        """
        current = self.record
        schema, address = [], []
        for name, selector in _parts(path):
            schema_path = ".".join([*schema, name])
            rule = _selector(self.table, schema_path, selector, allow_legacy=allow_legacy)
            owner, key = current, name
            current = _value(owner, name)
            part = name
            if selector is not None:
                if isinstance(current, (str, bytes, Mapping)) or not hasattr(current, "__len__"):
                    raise IdentityError(f"{self.table}.{schema_path}: expected a list")
                values = current
                if rule is not None:
                    indexed = self._index(values, rule, schema_path, ".".join([*address, name]))
                    if isinstance(selector, int):
                        if selector >= len(values):
                            raise IdentityError(f"Protobuf list index out of range: {path}")
                        item_key = _encoded(business_values(
                            f"{self.table}.{schema_path}", values[selector], rule))
                    else:
                        item_key = _encoded(selector)
                    if item_key not in indexed:
                        raise IdentityError(f"{self.table}.{schema_path}: nested identity is absent: {item_key}")
                    key, current = indexed[item_key]
                    part += f"[key={item_key}]"
                else:
                    if not isinstance(selector, int) or selector >= len(values):
                        raise IdentityError(f"Protobuf list index out of range: {path}")
                    key, current = selector, values[selector]
                    part += f"[{selector}]"
                owner = values
            schema.append(name + ("[]" if selector is not None else ""))
            address.append(part)
        if not isinstance(current, str):
            raise IdentityError(f"Protobuf field is not text: {path}")
        return owner, key, current, ".".join(address)

    def canonicalize(self, path):
        return self.locate(path, allow_legacy=True)[3]


def field_rules_metadata():
    return {
        "schema_version": 1,
        "encoding": "field[key=compact-json-object].field",
        "unlisted_arrays": "position with original-text guard",
        "nested_keys": {
            f"{table}.{path}": [{"field": name, "type": kind} for name, kind in rule]
            for (table, path), rule in sorted(NESTED_KEY_RULES.items())
        },
    }
