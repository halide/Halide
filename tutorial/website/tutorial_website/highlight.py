"""Syntax highlighting via Pygments.

Replaces the old pipeline's dependency on the external `highlight` CLI and
its `Halide.theme`/`-s Halide` language file, neither of which exists
anywhere any more.
"""

from __future__ import annotations

from pygments import highlight as _pygments_highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import BashLexer, CppLexer

_LEXERS = {
    "cpp": CppLexer(stripnl=False, stripall=False, ensurenl=True),
    "bash": BashLexer(stripnl=False, stripall=False, ensurenl=True),
}
# The default Pygments style only sets a background color, leaving
# unstyled/plain tokens (identifiers, punctuation, ...) to inherit whatever
# `color` the page happens to set -- fine for a light page, invisible on a
# dark one. Monokai sets both, and its palette anchors the whole page's
# color scheme (see assets/style.css).
_FORMATTER = HtmlFormatter(nowrap=False, cssclass="highlight", style="monokai")


def highlight_source(text: str, lexer: str = "cpp") -> str:
    """Highlights a chunk of source, returning a self-contained
    `<div class="highlight"><pre>...</pre></div>` block.

    Callers pass one contiguous chunk of lesson source per call (rather than
    the whole file at once) so that output/figure blocks can be interleaved
    between chunks; each chunk is lexed independently, so a lexer state that
    spans a chunk boundary (e.g. a still-open /* block comment */) won't
    carry over -- a rare cosmetic-only edge case given lesson sources
    overwhelmingly use // line comments.
    """
    if not text.endswith("\n"):
        text += "\n"
    return _pygments_highlight(text, _LEXERS[lexer], _FORMATTER)


def stylesheet() -> str:
    return _FORMATTER.get_style_defs(".highlight")
