from typing import Any, Literal

class _UUID:
    @property
    def bytes(self) -> bytes: ...

    @property
    def bytes_le(self) -> bytes: ...

    @property
    def clock_seq(self) -> int: ...

    @property
    def clock_seq_hi_variant(self) -> int: ...

    @property
    def clock_seq_low(self) -> int: ...

    @property
    def fields(self) -> tuple[int, int, int, int, int, int]: ...

    @property
    def hex(self) -> str: ...

    @property
    def int(self) -> int: ...

    @property
    def node(self) -> int: ...

    @property
    def time(self) -> int: ...

    @property
    def time_hi_version(self) -> int: ...

    @property
    def time_low(self) -> int: ...

    @property
    def time_mid(self) -> int: ...

    @property
    def timestamp(self) -> int: ...

    @property
    def urn(self) -> str: ...

    def __copy__(self) -> _UUID: ...
    def __deepcopy__(self, memo: dict[Any, Any], /) -> _UUID: ...
    def __hash__(self) -> int: ...
    def __int__(self) -> int: ...
    def __lt__(self, other: Any, /) -> bool: ...
    def __le__(self, other: Any, /) -> bool: ...
    def __eq__(self, other: Any, /) -> bool: ...
    def __ne__(self, other: Any, /) -> bool: ...
    def __gt__(self, other: Any, /) -> bool: ...
    def __ge__(self, other: Any, /) -> bool: ...

# https://www.rfc-editor.org/rfc/rfc9562#section-5.7
#
#  0                   1                   2                   3
#  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
# +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
# |                           unix_ts_ms                          |
# +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
# |          unix_ts_ms           |  ver  |       rand_a          |
# +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
# |var|                        rand_b                             |
# +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
# |                            rand_b                             |
# +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
def _uuid7(
    timestamp: int | None = None,
    nanos: int | None = None,
    mode: Literal["fast", "secure"] = "fast",
) -> _UUID:
    """
    Generate a UUIDv7 object.

    Args:
        timestamp:
            Unix timestamp in whole seconds.
            If omitted, the current system time is used.
        nanos:
            Optional fractional part in nanoseconds in the range
            `0..999_999_999`.
            When `timestamp` is provided, `nanos` contributes the
            sub-millisecond part of `unix_ts_ms`.
        mode:
            Changes only how the random and counter bits are produced.

            `mode="fast"` seeds the internal generator state from the OS
            randomness source once and then derives the random and counter
            bits from the internal PRNG state in the hot path.

            `mode="secure"` gets the random and counter bits from the OS
            randomness source while UUIDs are being generated and does not
            rely on the internal PRNG state for per-UUID random data.

    Returns:
        A UUIDv7 object.
    """

def _reseed_rng() -> None: ...
