from typing import Literal
from uuid import UUID

from c_uuid_v7.__core import _uuid7

_uuid_from_int = getattr(UUID, "_from_int", None)


def uuid7(
    timestamp: int | None = None,
    nanos: int | None = None,
    mode: Literal["fast", "secure"] = "fast",
) -> UUID:
    uuid7_int = _uuid7(timestamp, nanos, mode).int
    if _uuid_from_int is not None:
        return _uuid_from_int(uuid7_int)
    return UUID(int=uuid7_int)
