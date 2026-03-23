__all__ = (
    "MAX",
    "NIL",
    "UUID",
    "__version__",
    "compat",
    "uuid7",
)

import os
import uuid

from . import compat
from .__core import (
    UUID,
    _reseed_rng,
    _uuid7 as uuid7,
)
from ._version import __version__

NIL = uuid.UUID("00000000-0000-0000-0000-000000000000")
MAX = uuid.UUID("ffffffff-ffff-ffff-ffff-ffffffffffff")

if hasattr(os, "fork"):
    os.register_at_fork(after_in_child=_reseed_rng)
