import copy
import ctypes
import gc
import itertools
import os
import sys
import uuid
from collections.abc import Callable
from typing import Any, Literal, cast

import c_uuid_v7
import pytest

Mode = Literal["fast", "secure"]
V7_MAX_TIMESTAMP_MS = 0xFFFFFFFFFFFF


class _UUIDObject(ctypes.Structure):
    _fields_ = [
        ("ob_refcnt", ctypes.c_ssize_t),
        ("ob_type", ctypes.c_void_p),
        ("hi", ctypes.c_uint64),
        ("lo", ctypes.c_uint64),
    ]


def _uuid_ints(values: list[c_uuid_v7.UUID]) -> list[int]:
    return [value.int for value in values]


def _assert_v7(value: c_uuid_v7.UUID) -> None:
    assert (value.int >> 76) & 0xF == 0x7
    assert (value.int >> 62) & 0x3 == 0x2


def _assert_inc(values: list[c_uuid_v7.UUID]) -> None:
    ints = _uuid_ints(values)
    assert len(set(ints)) == len(ints)
    assert all(
        left < right
        for left, right in itertools.pairwise(values)
    )  # fmt: skip
    assert all(
        left < right
        for left, right in itertools.pairwise(ints)
    )  # fmt: skip


def _assert_timestamp_non_decreasing(values: list[c_uuid_v7.UUID]) -> None:
    timestamps = [value.timestamp for value in values]
    assert all(
        left <= right
        for left, right in itertools.pairwise(timestamps)
    )  # fmt: skip


@pytest.mark.parametrize("mode", [None, "fast", "secure"])
def test_uuid7_returns_uuid(mode: Mode | None) -> None:
    uuid_ = c_uuid_v7.uuid7() if mode is None else c_uuid_v7.uuid7(mode=mode)
    assert isinstance(uuid_, c_uuid_v7.UUID)
    _assert_v7(uuid_)


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("00000000-0000-0000-0000-000000000000", 0),
        ("ffffffff-ffff-ffff-ffff-ffffffffffff", (1 << 128) - 1),
    ],
)
def test_uuid_constructor_accepts_canonical_text(text: str, expected: int) -> None:
    uuid_ = c_uuid_v7.UUID(text)
    assert isinstance(uuid_, c_uuid_v7.UUID)
    assert str(uuid_) == text
    assert uuid_.int == expected


def test_uuid_constants_match_text_constructors() -> None:
    assert c_uuid_v7.UUID("00000000-0000-0000-0000-000000000000") == c_uuid_v7.NIL
    assert c_uuid_v7.UUID("ffffffff-ffff-ffff-ffff-ffffffffffff") == c_uuid_v7.MAX


@pytest.mark.skipif(
    sys.implementation.name != "cpython",
    reason="CPython object-identity specific",
)
def test_uuid7_reuses_cached_object() -> None:
    first = c_uuid_v7.uuid7()
    first_id = id(first)

    del first
    gc.collect()

    second = c_uuid_v7.uuid7()
    assert id(second) == first_id


@pytest.mark.parametrize(
    "make",
    [
        lambda: c_uuid_v7.UUID(
            bytes=b"\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff",
        ),
        lambda: c_uuid_v7.UUID(
            bytes_le=b"\x33\x22\x11\x00\x55\x44\x77\x66\x88\x99\xaa\xbb\xcc\xdd\xee\xff",
        ),
        lambda: c_uuid_v7.UUID(
            fields=(0x00112233, 0x4455, 0x6677, 0x88, 0x99, 0xAABBCCDDEEFF),
        ),
        lambda: c_uuid_v7.UUID(int=0x00112233445566778899AABBCCDDEEFF),
    ],
)
def test_uuid_constructor_accepts_inputs(make: Callable[[], c_uuid_v7.UUID]) -> None:
    uuid_ = make()
    assert str(uuid_) == "00112233-4455-6677-8899-aabbccddeeff"


def test_uuid_constructor_rejects_int_overflow() -> None:
    with pytest.raises(ValueError, match="int is out of range"):
        c_uuid_v7.UUID(int=1 << 200)


def test_uuid_constructor_returns_same_object_for_uuid_input() -> None:
    uuid_ = c_uuid_v7.uuid7()
    assert c_uuid_v7.UUID(uuid_) is uuid_


def test_uuid_constructor_requires_exactly_one_input_form() -> None:
    constructor = cast(Any, c_uuid_v7.UUID)

    with pytest.raises(
        TypeError,
        match="one of the hex, bytes, bytes_le, fields, or int",
    ):
        constructor()

    with pytest.raises(
        TypeError,
        match="one of the hex, bytes, bytes_le, fields, or int",
    ):
        constructor("00000000-0000-0000-0000-000000000000", int=0)


def test_uuid_constructor_rejects_more_than_one_positional_arg() -> None:
    constructor = cast(Any, c_uuid_v7.UUID)

    with pytest.raises(TypeError, match=r"UUID\(\) takes at most 1 positional argument"):
        constructor("00000000-0000-0000-0000-000000000000", b"\x00" * 16)


def test_compat_uuid7_returns_stdlib_uuid() -> None:
    uuid_ = c_uuid_v7.compat.uuid7()
    assert isinstance(uuid_, uuid.UUID)
    assert uuid_.version == 7


def test_uuid7_string_and_repr_shape() -> None:
    uuid_ = c_uuid_v7.uuid7()
    text = str(uuid_)
    assert len(text) == 36
    assert text.count("-") == 4
    assert repr(uuid_) == f"UUID('{text}')"


def test_uuid7_hex() -> None:
    uuid_ = c_uuid_v7.uuid7()
    assert len(uuid_.hex) == 32
    assert uuid_.int == int(uuid_.hex, 16)
    assert int(uuid_) == uuid_.int


def test_uuid7_object_properties() -> None:
    uuid_ = c_uuid_v7.uuid7()
    stdlib_uuid = uuid.UUID(int=uuid_.int)

    assert uuid_.bytes == stdlib_uuid.bytes
    assert uuid_.bytes_le == stdlib_uuid.bytes_le
    assert uuid_.fields == stdlib_uuid.fields
    assert uuid_.time_low == stdlib_uuid.time_low
    assert uuid_.time_mid == stdlib_uuid.time_mid
    assert uuid_.time_hi_version == stdlib_uuid.time_hi_version
    assert uuid_.clock_seq_hi_variant == stdlib_uuid.clock_seq_hi_variant
    assert uuid_.clock_seq_low == stdlib_uuid.clock_seq_low
    assert uuid_.clock_seq == stdlib_uuid.clock_seq
    assert uuid_.node == stdlib_uuid.node

    if sys.version_info >= (3, 14):
        assert uuid_.time == stdlib_uuid.time
        assert uuid_.timestamp == stdlib_uuid.time
    else:
        assert uuid_.time == (stdlib_uuid.int >> 80)
        assert uuid_.timestamp == (stdlib_uuid.int >> 80)

    assert uuid_.urn == stdlib_uuid.urn


def test_uuid7_copy_and_deepcopy_return_same_object() -> None:
    uuid_ = c_uuid_v7.uuid7()

    assert copy.copy(uuid_) is uuid_
    assert copy.deepcopy(uuid_) is uuid_


def test_uuid7_sets_expected_version_and_variant_bits() -> None:
    for _ in range(128):
        _assert_v7(c_uuid_v7.uuid7())


def test_uuid7_consecutive_values_change_more_than_the_last_bit() -> None:
    first = c_uuid_v7.uuid7()
    second = c_uuid_v7.uuid7()

    assert first != second
    assert (first.int & ((1 << 62) - 1)) != (second.int & ((1 << 62) - 1))
    assert (first.int ^ second.int) > 1


@pytest.mark.parametrize(
    ("make", "size"),
    [
        (c_uuid_v7.uuid7, 1024),
        (c_uuid_v7.uuid7, 10_000),
        (lambda: c_uuid_v7.uuid7(mode="fast"), 2048),
        (lambda: c_uuid_v7.uuid7(mode="secure"), 2048),
    ],
)
def test_uuid7_batches(make: Callable[[], c_uuid_v7.UUID], size: int) -> None:
    values = [make() for _ in range(size)]

    _assert_inc(values)
    _assert_timestamp_non_decreasing(values)
    assert all(((value.int >> 76) & 0xF) == 0x7 for value in values)


def test_uuid7_explicit_timestamp_batch() -> None:
    values = [c_uuid_v7.uuid7(1_704_164_645, 123_000_000) for _ in range(256)]

    assert all(value.timestamp == 1_704_164_645_123 for value in values)
    assert all(value.hex[:12] == values[0].hex[:12] for value in values)
    assert len(set(_uuid_ints(values))) == len(values)
    assert all(((value.int >> 76) & 0xF) == 0x7 for value in values)


def test_uuid7_fixed_timestamp_batch() -> None:
    values = [c_uuid_v7.uuid7(1_700_000_000) for _ in range(10_000)]
    assert len(set(_uuid_ints(values))) == len(values)
    assert all(((value.int >> 76) & 0xF) == 0x7 for value in values)


@pytest.mark.parametrize(
    ("args", "expected_timestamp"),
    [
        ((1_679_665_408,), 1_679_665_408_000),
        ((1_704_164_645, 123_000_000), 1_704_164_645_123),
    ],
)
def test_uuid7_timestamp_args(
    args: tuple[int, ...],
    expected_timestamp: int,
) -> None:
    uuid_ = c_uuid_v7.uuid7(args[0], args[1] if len(args) > 1 else None)
    assert uuid_.timestamp == expected_timestamp
    _assert_v7(uuid_)


@pytest.mark.parametrize("nanos", [0, 999_999_999])
def test_uuid7_accepts_valid_nanos_bounds(nanos: int) -> None:
    _assert_v7(c_uuid_v7.uuid7(nanos=nanos))


@pytest.mark.parametrize(
    ("kwargs", "error_type", "message"),
    [
        ({"nanos": 1_000_000_000}, ValueError, r"nanos must be in range 0\.\.999999999"),
        ({"timestamp": -1}, TypeError, "timestamp must be a non-negative int or None"),
        ({"nanos": -1}, TypeError, "nanos must be a non-negative int or None"),
        ({"timestamp": "bad"}, TypeError, "timestamp must be a non-negative int or None"),
        ({"nanos": "bad"}, TypeError, "nanos must be a non-negative int or None"),
        ({"mode": 1}, TypeError, "mode must be 'fast', 'secure', or None"),
        ({"mode": "bad"}, ValueError, "mode must be 'fast' or 'secure'"),
        ({"timestamp": 281_474_976_711}, ValueError, "timestamp is too large"),
    ],
)
def test_uuid7_invalid_args(
    kwargs: dict[str, Any],
    error_type: type[Exception],
    message: str,
) -> None:
    with pytest.raises(error_type, match=message):
        c_uuid_v7.uuid7(**kwargs)


def test_uuid_objects_compare_and_hash() -> None:
    lower = c_uuid_v7.uuid7(1_700_000_000, 1)
    higher = c_uuid_v7.uuid7(1_700_000_001, 1)
    hash_ = hash(lower)

    assert lower < higher
    assert lower <= higher
    assert lower != higher
    assert higher > lower
    assert higher >= lower
    assert hash(lower) == hash_


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
        assert {uuid_: "stored"}[uuid_] == "stored"
    finally:
        raw_uuid.hi = original_hi
        raw_uuid.lo = original_lo


def test_compat_uuid7_preserves_timestamp() -> None:
    uuid_ = c_uuid_v7.compat.uuid7(1_679_665_408, 123_000_000)

    assert isinstance(uuid_, uuid.UUID)
    assert uuid_.version == 7


@pytest.mark.skipif(
    sys.platform == "win32",
    reason="Does not run on Windows",
)
def test_reseed_is_called_when_forking() -> None:
    read_end, write_end = os.pipe()
    c_uuid_v7.uuid7()

    pid = os.fork()  # ty: ignore[unresolved-attribute]
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


@pytest.mark.parametrize(
    ("timestamp", "expected"),
    [
        (0, 0),
        (V7_MAX_TIMESTAMP_MS // 1000, None),
    ],
)
def test_uuid7_timestamp_bounds(timestamp: int, expected: int | None) -> None:
    uuid_ = c_uuid_v7.uuid7(timestamp)
    if expected is not None:
        assert uuid_.timestamp == expected
    _assert_v7(uuid_)


@pytest.mark.skipif(
    sys.implementation.name == "pypy",
    reason="sys.getsizeof() always raises TypeError on PyPy",
)
def test_uuid7_mem_size() -> None:
    uuid_ = c_uuid_v7.uuid7()
    assert sys.getsizeof(uuid_) < 200
