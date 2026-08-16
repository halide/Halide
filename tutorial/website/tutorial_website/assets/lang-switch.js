// Lets lessons that have both a C++ and a Python source (lessons 1-14)
// switch which one is shown, and remembers the choice in localStorage so it
// carries over as the reader navigates between lesson pages. Also reflects
// the choice into the URL via halideSyncLangURL (defined in base.html.j2),
// so the address bar is always a shareable link to what's on screen.
(function () {
  var STORAGE_KEY = "halide-tutorial-lang";

  document.addEventListener("click", function (event) {
    var button = event.target.closest(".lang-switch button");
    if (!button) {
      return;
    }
    var lang = button.dataset.lang;
    try {
      localStorage.setItem(STORAGE_KEY, lang);
    } catch (e) {
      // Ignore (e.g. storage disabled) -- the toggle still updates the
      // current page, it just won't be remembered.
    }
    document.documentElement.classList.toggle("pref-python", lang === "python");
    if (window.halideSyncLangURL) {
      window.halideSyncLangURL(lang);
    }
  });
})();
