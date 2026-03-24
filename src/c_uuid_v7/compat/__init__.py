import sys
from typing import Literal
from uuid import UUID

from c_uuid_v7.__core import _uuid7

if sys.version_info >= (3, 14):

    def from_int(value: int) -> UUID:
        return UUID._from_int(value)  # noqa: SLF001

else:

    def from_int(value: int) -> UUID:
        obj = object.__new__(UUID)
        object.__setattr__(obj, "int", value)  # noqa: PLC2801
        object.__setattr__(obj, "is_safe", None)  # noqa: PLC2801
        return obj


def uuid7(
    timestamp: int | None = None,
    nanos: int | None = None,
    mode: Literal["fast", "secure"] = "fast",
) -> UUID:
    uuid7_int = _uuid7(timestamp, nanos, mode).int
    return from_int(uuid7_int)
