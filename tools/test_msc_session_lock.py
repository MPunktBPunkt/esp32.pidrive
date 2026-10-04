#!/usr/bin/env python3
"""Unit tests for MscSessionLock (no hardware)."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pump_bridge import MscSessionLock, menu_items_identity  # noqa: E402


def items(*pairs: tuple[str, str]) -> list[dict]:
    return [{"uid": u, "name": n, "kind": "station"} for u, n in pairs]


def main() -> int:
    lock = MscSessionLock(enabled=True)
    fav = items(("fav0", "Rock"), ("fav1", "Bayern"), ("fav2", "BOB"))
    ui = items(
        ("1977", "Zurueck"),
        ("1833", "Ausgang klinke"),
        ("1667", "Auto"),
    )

    assert lock.allow_menu_set(fav)[0]  # unplugged
    lock.on_plug()
    assert lock.sealing
    ok, why = lock.allow_menu_set(fav)
    assert ok and why == "seal"
    lock.note_published(fav)
    assert not lock.sealing
    assert lock.frozen_identity == menu_items_identity(fav)

    ok, why = lock.allow_menu_set(fav)
    assert ok and why == "idempotent"

    ok, why = lock.allow_menu_set(ui)
    assert not ok and "frozen_reject" in why
    assert lock.reject_count == 1

    lock.on_unplug()
    assert lock.frozen_identity is None
    assert lock.allow_menu_set(ui)[0]

    # disabled
    off = MscSessionLock(enabled=False)
    off.on_plug()
    assert off.allow_menu_set(ui)[0]

    print("PASS test_msc_session_lock")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
