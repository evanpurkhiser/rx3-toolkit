"""Firmware-derived physical-control names shared by host tooling."""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any


CATALOG_PATH = Path(__file__).with_name("controls.json")


@dataclass(frozen=True)
class Control:
    name: str
    key_code: int
    scope: str
    kind: str
    raw_min: int | None = None
    raw_max: int | None = None

    def as_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "name": self.name,
            "keyCode": self.key_code,
            "scope": self.scope,
            "kind": self.kind,
        }
        if self.raw_min is not None:
            result["rawMin"] = self.raw_min
        if self.raw_max is not None:
            result["rawMax"] = self.raw_max
        return result


def load_catalog(path: Path = CATALOG_PATH) -> tuple[dict[str, Any], tuple[Control, ...]]:
    document = json.loads(path.read_text())
    controls = tuple(
        Control(
            item["name"],
            item["keyCode"],
            item["scope"],
            item["kind"],
            item.get("rawMin"),
            item.get("rawMax"),
        )
        for item in document["controls"]
    )
    return document, controls


CATALOG, CONTROLS = load_catalog()
BY_NAME = {control.name: control for control in CONTROLS}
BY_CODE = {control.key_code: control for control in CONTROLS}


def resolve_control(value: str | int) -> Control:
    if isinstance(value, int):
        code = value
    else:
        try:
            return BY_NAME[value]
        except KeyError:
            try:
                code = int(value, 0)
            except ValueError as error:
                raise KeyError(f"unknown control {value!r}") from error

    try:
        return BY_CODE[code]
    except KeyError as error:
        raise KeyError(f"unknown control key code 0x{code:04x}") from error
