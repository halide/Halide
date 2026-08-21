"""Sphinx configuration for the Halide documentation site.

breathe_projects.Halide is populated at build time via `sphinx-build -D`
(see doc/CMakeLists.txt), not hardcoded here, since the Doxygen XML output
path depends on the CMake build directory.
"""

import logging

project = "Halide"
copyright = "The Halide team"
author = "The Halide team"

extensions = [
    "myst_parser",
    "breathe",
    "sphinxcontrib.video",
    "sphinx_design",
]

source_suffix = {
    ".md": "markdown",
}

exclude_patterns = ["_build"]

html_static_path = ["_static"]
html_css_files = ["custom.css"]

# doc/*.md files cross-reference each other's sections with GitHub-style
# heading anchors in prose (e.g. "see [below](#current-status)"); this makes
# MyST generate matching anchor targets instead of erroring on each one as an
# unresolved cross-reference. 4 covers every heading depth used in doc/*.md.
myst_heading_anchors = 4

# Tutorial lessons with a Python translation wrap their C++/Python source in
# a sphinx-design {tab-set}; colon-fenced directives (unlike the default
# backtick fences) let that outer container nest the code/output/figure
# blocks inside each {tab-item} without having to escalate backtick counts.
myst_enable_extensions = ["colon_fence"]

breathe_default_project = "Halide"

html_title = "Halide"
html_theme = "pydata_sphinx_theme"
html_theme_options = {
    "show_nav_level": 2,
    "navigation_with_keys": True,
    # pydata-sphinx-theme ignores Sphinx's standard pygments_style/
    # pygments_dark_style config values and reads its own theme-option keys
    # instead. Its own defaults are high-contrast accessibility styles, which
    # read as harsh (e.g. bright yellow comments) for a full documentation
    # site rather than a single code sample -- these are more muted, and
    # shared by every code block (prose, API reference, and tutorial alike).
    "pygments_light_style": "friendly",
    "pygments_dark_style": "monokai",
    # Every page's raw MyST source is a generated build artifact (hand-
    # written prose aside, most pages are either API reference stubs wrapping
    # a single Breathe directive or tutorial pages generated straight from
    # tutorial/lesson_*.cpp), so the "Show Source" link is never useful --
    # drop it site-wide. Tutorial pages also drop the "On this page" outline,
    # since a lesson page is just one big flat section with no sub-headings.
    "secondary_sidebar_items": {
        "**": ["page-toc"],
        "tutorial/**": [],
    },
}


class _SuppressSecondarySidebarWildcardWarning(logging.Filter):
    """Silences a false-positive pydata-sphinx-theme warning.

    Every page under tutorial/ necessarily matches both wildcard patterns
    above (** and tutorial/**) by construction -- that's the whole point,
    not a misconfiguration -- but the theme's own matcher warns on any page
    matching two wildcard patterns regardless of intent, with no way to
    silence it via Sphinx's own `suppress_warnings` (it doesn't pass a
    type/subtype). It still resolves to the right (tutorial/**, empty) value
    per pydata_sphinx_theme.utils._get_matching_sidebar_items's own
    last-match-wins rule; only the log line itself is suppressed.
    """

    def filter(self, record: logging.LogRecord) -> bool:
        return (
            "matches two wildcard patterns in secondary_sidebar_items"
            not in record.getMessage()
        )


# sphinx.util.logging.getLogger(name) prefixes the name with "sphinx.", so
# pydata_sphinx_theme.utils.SPHINX_LOGGER is really this logger underneath.
logging.getLogger("sphinx.pydata_sphinx_theme.utils").addFilter(
    _SuppressSecondarySidebarWildcardWarning()
)
