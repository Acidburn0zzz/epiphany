// mouse wheel
pref("mousewheel.withcontrolkey.action", 1);
pref("mousewheel.withcontrolkey.numlines", 1);
pref("mousewheel.withcontrolkey.sysnumlines", false);

// don't allow opening file:/// URLs on pages from network sources (http, etc.)
pref("security.checkloaduri", true);

// enable line wrapping in View Source
pref("view_source.wrap_long_lines", true);

// disable sidebar What's Related, we don't use it
pref("browser.related.enabled", false);

// Work around for mozilla focus bugs
pref("mozilla.widget.raise-on-setfocus", false);

// disable sucky XUL ftp view, have nice ns4-like html page instead
pref("network.dir.generate_html", true);

// deactivate mailcap support, it breaks Gnome-based helper apps
pref("helpers.global_mailcap_file", "");
pref("helpers.private_mailcap_file", "");

// use the mozilla defaults for mime.types files to let mozilla guess proper
// Content-Type for file uploads instead of always falling back to
// application/octet-stream
pref("helpers.global_mime_types_file", null);
pref("helpers.private_mime_types_file", null);

// use google for keywords
pref("keyword.URL", "http://www.google.com/search?ie=UTF-8&oe=UTF-8&q=");
pref("keyword.enabled", true);

// disable usless security warnings
pref("security.warn_entering_secure", false);
pref("security.warn_leaving_secure", false);
pref("security.warn_submit_insecure", false);

// fonts
pref("font.size.unit", "pt");
pref("font.size.fixed.ar", 10);
pref("font.size.fixed.x-baltic", 10);
pref("font.size.fixed.x-central-euro", 10);
pref("font.size.fixed.x-cyrillic", 10);
pref("font.size.fixed.x-devanagari", 10);
pref("font.size.fixed.el", 10);
pref("font.size.fixed.he", 10);
pref("font.size.fixed.ja", 10);
pref("font.size.fixed.ko", 10);
pref("font.size.fixed.zh-CN", 10);
pref("font.size.fixed.x-tamil", 10);
pref("font.size.fixed.th", 10);
pref("font.size.fixed.zh-TW", 10);
pref("font.size.fixed.tr", 10);
pref("font.size.fixed.x-unicode", 10);
pref("font.size.fixed.x-western", 10);
pref("font.size.variable.ar", 11);
pref("font.size.variable.x-baltic", 11);
pref("font.size.variable.x-central-euro", 11);
pref("font.size.variable.x-cyrillic", 11);
pref("font.size.variable.x-devanagari", 11);
pref("font.size.variable.el", 11);
pref("font.size.variable.he", 11);
pref("font.size.variable.ja", 11);
pref("font.size.variable.ko", 11);
pref("font.size.variable.zh-CN", 11);
pref("font.size.variable.x-tamil", 11);
pref("font.size.variable.th", 11);
pref("font.size.variable.zh-TW", 11);
pref("font.size.variable.tr", 11);
pref("font.size.variable.x-unicode", 11);
pref("font.size.variable.x-western", 11);

