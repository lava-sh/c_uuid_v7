import uuid

from c_uuid_v7.compat import uuid7


def test_uuid() -> None:
    assert isinstance(uuid7(), uuid.UUID)
