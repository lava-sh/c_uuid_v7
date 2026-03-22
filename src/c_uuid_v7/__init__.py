__all__ = (
    "UUID",
    "__version__",
    "uuid7",
)

import os

from ._uuid_v7_c import UUID, _reseed_rng, _uuid7
from ._version import __version__

if hasattr(os, "fork"):
    os.register_at_fork(after_in_child=_reseed_rng)


def uuid7(timestamp: int | None = None, nanos: int | None = None) -> UUID:
    return _uuid7(timestamp, nanos)
