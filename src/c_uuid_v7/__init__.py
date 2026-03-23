__all__ = (
    "UUID",
    "__version__",
    "compat",
    "uuid7",
)

import os

from . import compat
from ._uuid_v7_c import (
    UUID,
    _reseed_rng,
    _uuid7 as uuid7,
)
from ._version import __version__

if hasattr(os, "fork"):
    os.register_at_fork(after_in_child=_reseed_rng)
