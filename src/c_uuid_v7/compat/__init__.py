from uuid import UUID

from c_uuid_v7.__core import _uuid7


def uuid7(timestamp: int | None = None, nanos: int | None = None) -> UUID:
    return UUID(int=_uuid7(timestamp, nanos).int)
