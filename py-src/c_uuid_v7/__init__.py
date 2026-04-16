__all__ = (
    "MAX",
    "NIL",
    "UUID",
    "__version__",
    "compat",
    "uuid7",
)

import os

from . import compat
from ._core import (
    _MAX as MAX,
    _NIL as NIL,
    _UUID as UUID,
    _reseed_rng,
    _uuid7 as uuid7,
)
from ._version import __version__

if hasattr(os, "fork"):
    os.register_at_fork(after_in_child=_reseed_rng)
