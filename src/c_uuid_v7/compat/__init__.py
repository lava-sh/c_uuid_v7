from typing import Literal
from uuid import UUID

from c_uuid_v7.__core import _uuid7


def uuid7(
    timestamp: int | None = None,
    nanos: int | None = None,
    mode: Literal["fast", "secure"] = "fast",
) -> UUID:
    uuid7_int = _uuid7(timestamp, nanos, mode).int
    return UUID._from_int(uuid7_int)  # noqa: SLF001
