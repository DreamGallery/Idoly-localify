"""Stable identities for MasterDB records, independent of text and row order."""

from collections.abc import Mapping
import json
import re


class IdentityError(ValueError):
    """A record or persisted key cannot be identified safely."""


# Types are from SolisClient pmaster.proto. Enum identities use numeric values.
BUSINESS_KEY_RULES = {
    "CardEvolutionMessage": (("cardId", "string"), ("evolutionLevel", "integer"), ("number", "integer")),
    "ExcursionGazeReaction": (("characterId", "string"), ("excursionPlaceId", "string"), ("number", "integer")),
    "ExerciseHint": (("exerciseId", "string"),),
    "GachaStamp": (("groupId", "string"), ("sheetNumber", "integer"), ("stampNumber", "integer")),
    "HomeAction": (("homeActionId", "string"),),
    "HomeBackground": (("homeBackgroundId", "string"),),
    "HomeDramaPosition": (("homeDramaPositionId", "string"),),
    "HomeTalk": (("homeTalkId", "string"),),
    "HomeTalkCallPattern": (("characterId", "string"), ("patternId", "string")),
    "MarathonBoxGachaSetting": (("marathonId", "string"), ("boxGachaId", "string")),
    "PhotoExpressionStagePosition": (("stageId", "string"), ("number", "integer")),
    "StaffTraining": (("parameterType", "enum"),),
    "StatusEffectName": (("statusEffectType", "enum"),),
    "Tutorial": (("type", "enum"),),
    "Wording": (("key", "string"),),
}
PREFIX = "bk1:"
_DECIMAL = re.compile(r"[+-]?[0-9]+\Z")


def _field(table, record, name, kind=None):
    if isinstance(record, Mapping):
        if name not in record:
            raise IdentityError(f"{table}.{name}: missing identity field")
        return record[name]
    descriptor = getattr(record, "DESCRIPTOR", None)
    fields = getattr(descriptor, "fields_by_name", {})
    if name not in fields:
        raise IdentityError(f"{table}.{name}: missing identity field")
    field = fields[name]
    # Protobuf scalar defaults are valid; a missing JSON key is not equivalent.
    expected_types = {"string": {9}, "integer": {5}, "enum": {14}}
    repeated = getattr(field, "is_repeated", None)
    if repeated is None:
        repeated = field.label == 3
    if repeated or (kind and field.type not in expected_types[kind]):
        raise IdentityError(f"{table}.{name}: incompatible protobuf field type")
    if getattr(field, "has_presence", False) and not record.HasField(name):
        raise IdentityError(f"{table}.{name}: unset identity field")
    return getattr(record, name)


def _normalize(table, name, value, kind):
    if kind == "string":
        if not isinstance(value, str) or value == "":
            raise IdentityError(f"{table}.{name}: expected nonempty string")
        return value
    if isinstance(value, bool):
        raise IdentityError(f"{table}.{name}: expected integer, got bool")
    if isinstance(value, int):
        result = value
    elif isinstance(value, str) and _DECIMAL.fullmatch(value):
        result = int(value, 10)
    else:
        raise IdentityError(f"{table}.{name}: expected integer or decimal string")
    if not -(2**31) <= result < 2**31:
        raise IdentityError(f"{table}.{name}: value outside protobuf int32 range")
    return result


def business_values(label, record, rule):
    """Read and normalize explicitly declared business fields in schema order."""
    return {name: _normalize(label, name, _field(label, record, name, kind), kind)
            for name, kind in rule}


def record_key(table, record):
    """Return a stable string key for a JSON mapping or protobuf message.

    Known business tables use versioned JSON tuples; all others require ``id``.
    Integer/enum components normalize decimal strings to integers. String
    components retain their exact contents.
    """
    rule = BUSINESS_KEY_RULES.get(table)
    if rule is None:
        value = _field(table, record, "id")
        if isinstance(value, bool) or not isinstance(value, (str, int)) or value == "":
            raise IdentityError(f"{table}.id: expected nonempty string or integer")
        return str(value)
    parts = list(business_values(table, record, rule).items())
    return PREFIX + json.dumps(parts, ensure_ascii=False, separators=(",", ":"))


def index_records(table, records):
    """Index records by identity, rejecting duplicates even if text is equal."""
    if isinstance(records, (Mapping, str, bytes)):
        raise IdentityError(f"{table}: expected an iterable of records")
    result = {}
    for position, record in enumerate(records):
        key = record_key(table, record)
        if key in result:
            raise IdentityError(f"{table}: duplicate identity {key!r} at row {position}")
        result[key] = record
    return result


def validate_keyed_table(table, records):
    """Validate persisted ID-to-value keys without interpreting their values.

    Legacy row-number keys in business tables require an explicit migration.
    """
    if not isinstance(records, Mapping):
        raise IdentityError(f"{table}: expected a keyed mapping")
    for key in records:
        if not isinstance(key, str) or not key:
            raise IdentityError(f"{table}: expected nonempty string keys")
        if table not in BUSINESS_KEY_RULES:
            continue
        if not key.startswith(PREFIX):
            raise IdentityError(f"{table}: legacy or unknown key {key!r}; migrate identities first")
        try:
            parts = json.loads(key[len(PREFIX):])
            names = [name for name, _ in BUSINESS_KEY_RULES[table]]
            if (not isinstance(parts, list) or
                    any(not isinstance(part, list) or len(part) != 2 for part in parts) or
                    [part[0] for part in parts] != names):
                raise ValueError("wrong identity fields")
            canonical = record_key(table, dict(parts))
            if canonical != key:
                raise ValueError("noncanonical identity")
        except (ValueError, TypeError) as exc:
            raise IdentityError(f"{table}: invalid business key {key!r}: {exc}") from exc


def rules_metadata():
    """Return a fresh JSON-serializable description of the identity schema."""
    return {
        "schema_version": 1,
        "business_key_prefix": PREFIX,
        "encoding": "compact-json-field-value-pairs",
        "standard_id": "required string or integer; str(value)",
        "missing_json_fields": "error",
        "business_keys": {
            table: [{"field": name, "type": kind} for name, kind in rule]
            for table, rule in BUSINESS_KEY_RULES.items()
        },
    }
