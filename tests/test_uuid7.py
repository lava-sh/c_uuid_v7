import os
import sys
import uuid

import pytest

import c_uuid_v7


def test_uuid7_returns_fast_uuid() -> None:
    value = c_uuid_v7.uuid7()
    assert isinstance(value, c_uuid_v7.UUID)
    assert value.version == 7


def test_uuid7_string_and_repr_shape() -> None:
    value = c_uuid_v7.uuid7()
    text = str(value)
    assert len(text) == 36
    assert text.count("-") == 4
    assert repr(value) == f"UUID('{text}')"


def test_uuid7_hex_and_int_are_consistent() -> None:
    value = c_uuid_v7.uuid7()
    assert len(value.hex) == 32
    assert value.int == int(value.hex, 16)


def test_uuid7_timestamp_from_explicit_seconds() -> None:
    value = c_uuid_v7.uuid7(1_679_665_408)
    assert value.timestamp == 1_679_665_408_000


def test_uuid7_timestamp_from_explicit_seconds_and_nanos() -> None:
    value = c_uuid_v7.uuid7(1_704_164_645, 123_000_000)
    assert value.timestamp == 1_704_164_645_123


def test_uuid7_accepts_valid_nanos_bounds() -> None:
    assert c_uuid_v7.uuid7(nanos=0).version == 7
    assert c_uuid_v7.uuid7(nanos=999_999_999).version == 7


def test_uuid7_rejects_nanos_out_of_range() -> None:
    with pytest.raises(ValueError, match=r"nanos must be in range 0\.\.999999999"):
        c_uuid_v7.uuid7(nanos=1_000_000_000)


def test_uuid7_rejects_negative_timestamp() -> None:
    with pytest.raises(TypeError, match="timestamp must be a non-negative int or None"):
        c_uuid_v7.uuid7(timestamp=-1)


def test_uuid7_rejects_negative_nanos() -> None:
    with pytest.raises(TypeError, match="nanos must be a non-negative int or None"):
        c_uuid_v7.uuid7(nanos=-1)


def test_uuid7_rejects_invalid_timestamp_type() -> None:
    with pytest.raises(TypeError, match="timestamp must be a non-negative int or None"):
        c_uuid_v7.uuid7(timestamp="bad")


def test_uuid7_rejects_invalid_nanos_type() -> None:
    with pytest.raises(TypeError, match="nanos must be a non-negative int or None"):
        c_uuid_v7.uuid7(nanos="bad")


def test_uuid7_rejects_timestamp_above_supported_range() -> None:
    with pytest.raises(ValueError, match="timestamp is too large"):
        c_uuid_v7.uuid7(timestamp=281_474_976_711)


def test_uuid_objects_compare_and_hash() -> None:
    a = c_uuid_v7.uuid7(1_700_000_000, 1)
    b = c_uuid_v7.uuid7(1_700_000_000, 2)
    value_hash = hash(a)
    assert a < b
    assert a <= b
    assert a != b
    assert b > a
    assert b >= a
    assert not (a != a)
    assert hash(a) == value_hash


def test_compat_uuid7_returns_stdlib_uuid() -> None:
    value = c_uuid_v7.compat.uuid7()
    assert isinstance(value, uuid.UUID)
    assert value.version == 7


def test_compat_uuid7_preserves_timestamp() -> None:
    value = c_uuid_v7.compat.uuid7(1_679_665_408, 123_000_000)
    assert isinstance(value, uuid.UUID)
    assert value.version == 7


@pytest.mark.skipif(sys.platform == "win32", reason="Does not run on Windows")
def test_reseed_is_called_when_forking() -> None:
    read_end, write_end = os.pipe()
    c_uuid_v7.uuid7()

    pid = os.fork()
    if pid == 0:
        os.close(read_end)
        next_uuid_child = str(c_uuid_v7.uuid7())
        with os.fdopen(write_end, "w") as write_pipe:
            write_pipe.write(next_uuid_child)
        os._exit(0)

    os.close(write_end)
    next_parent_uuid = c_uuid_v7.uuid7()
    os.waitpid(pid, 0)
    with os.fdopen(read_end) as read_pipe:
        uuid_from_pipe = read_pipe.read().strip()

    assert str(next_parent_uuid) != uuid_from_pipe
