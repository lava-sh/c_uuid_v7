__all__ = ("__version__", "sum")

from ._core import sum as _sum
from ._version import __version__

sum = _sum  # noqa: A001
