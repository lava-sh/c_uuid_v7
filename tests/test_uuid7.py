import ctypes
import itertools
import os
import sys
import uuid

import pytest

import c_uuid_v7


class _UUIDObject(ctypes.Structure):
    _fields_ = [
        ("ob_refcnt", ctypes.c_ssize_t),
        ("ob_type", ctypes.c_void_p),
        ("hi", ctypes.c_uint64),
        ("lo", ctypes.c_uint64),
    ]


def test_uuid7_returns_fast_uuid() -> None:
    uuid_ = c_uuid_v7.uuid7()
    assert isinstance(uuid_, c_uuid_v7.UUID)
    assert uuid_.version == 7


def test_uuid7_string_and_repr_shape() -> None:
    uuid_ = c_uuid_v7.uuid7()
    text = str(uuid_)
    assert len(text) == 36
    assert text.count("-") == 4
    assert repr(uuid_) == f"UUID('{text}')"


def test_uuid7_hex_and_int_are_consistent() -> None:
    uuid_ = c_uuid_v7.uuid7()
    assert len(uuid_.hex) == 32
    assert uuid_.int == int(uuid_.hex, 16)


def test_uuid7_consecutive_values_change_more_than_the_last_bit() -> None:
    first = c_uuid_v7.uuid7()
    second = c_uuid_v7.uuid7()
    xor = first.int ^ second.int

    assert first != second
    assert (first.int & ((1 << 62) - 1)) != (second.int & ((1 << 62) - 1))
    assert xor > 1


def test_uuid7_batch_is_strictly_increasing() -> None:
    values = [c_uuid_v7.uuid7() for _ in range(1024)]

    assert len({value.int for value in values}) == len(values)
    assert all(
        left < right
        for left, right
        in itertools.pairwise(values)
    )  # fmt: skip


def test_uuid7_explicit_timestamp_batch_is_valid() -> None:
    values = [c_uuid_v7.uuid7(1_704_164_645, 123_000_000) for _ in range(256)]

    assert all(value.version == 7 for value in values)
    assert all(value.timestamp == 1_704_164_645_123 for value in values)
    assert len({value.int for value in values}) == len(values)


def test_uuid7_timestamp_from_explicit_seconds() -> None:
    uuid_ = c_uuid_v7.uuid7(1_679_665_408)
    assert uuid_.timestamp == 1_679_665_408_000


def test_uuid7_timestamp_from_explicit_seconds_and_nanos() -> None:
    uuid_ = c_uuid_v7.uuid7(1_704_164_645, 123_000_000)
    assert uuid_.timestamp == 1_704_164_645_123


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
    b = c_uuid_v7.uuid7(1_700_000_001, 1)
    value_hash = hash(a)
    assert a < b
    assert a <= b
    assert a != b
    assert b > a
    assert b >= a
    assert hash(a) == value_hash


@pytest.mark.skipif(
    sys.implementation.name != "cpython",
    reason="Relies on CPython object layout via ctypes.from_address",
)
def test_uuid_hash_never_returns_error_sentinel() -> None:
    uuid_ = c_uuid_v7.uuid7()
    raw_uuid = _UUIDObject.from_address(id(uuid_))
    original_hi = raw_uuid.hi
    original_lo = raw_uuid.lo

    raw_uuid.hi = 0x00000000FFFFFFFF
    raw_uuid.lo = 0xFFFFFFFFFFFFFFFF

    try:
        assert hash(uuid_) == -2

        mapping = {uuid_: "value"}
        assert mapping[uuid_] == "value"
    finally:
        raw_uuid.hi = original_hi
        raw_uuid.lo = original_lo


def test_compat_uuid7_returns_stdlib_uuid() -> None:
    uuid_ = c_uuid_v7.compat.uuid7()

    assert isinstance(uuid_, uuid.UUID)
    assert uuid_.version == 7


def test_compat_uuid7_preserves_timestamp() -> None:
    uuid_ = c_uuid_v7.compat.uuid7(1_679_665_408, 123_000_000)

    assert isinstance(uuid_, uuid.UUID)
    assert uuid_.version == 7


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
