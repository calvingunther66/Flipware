/*
 * Google Analytics 4 for flipware-fw.dev (property "Flipware Website").
 *
 * - Consent Mode v2: ads storage is always denied. Analytics storage starts
 *   denied in the EEA, UK and Switzerland until the visitor allows it, and is
 *   granted elsewhere unless the visitor declines. Either way the banner lets
 *   anyone change their mind, and the choice is remembered in this browser.
 * - Besides GA's own page views, scrolls, outbound clicks and engagement
 *   time, this sends:
 *     cta_click      {cta}            Download / Source code / nav links
 *     section_view   {section}        a section scrolled at least 40% into view
 *     read_time      {seconds}        visible time on page passed 15/30/60/120/300 s
 *     copy_command   {command}        build command copied
 */
(function () {
  var GA_ID = "G-Y6HF4R88SX";
  var KEY = "flipware-analytics-consent";

  window.dataLayer = window.dataLayer || [];
  function gtag() {
    window.dataLayer.push(arguments);
  }
  window.gtag = gtag;

  var strict = [
    "AT", "BE", "BG", "HR", "CY", "CZ", "DK", "EE", "FI", "FR", "DE", "GR",
    "HU", "IE", "IT", "LV", "LT", "LU", "MT", "NL", "PL", "PT", "RO", "SK",
    "SI", "ES", "SE", "IS", "LI", "NO", "GB", "CH",
  ];
  var adsDenied = { ad_storage: "denied", ad_user_data: "denied", ad_personalization: "denied" };
  gtag("consent", "default", Object.assign({ analytics_storage: "granted" }, adsDenied));
  gtag("consent", "default", Object.assign({ analytics_storage: "denied", region: strict }, adsDenied));

  function readChoice() {
    try {
      return localStorage.getItem(KEY);
    } catch (e) {
      return null;
    }
  }
  function saveChoice(v) {
    try {
      localStorage.setItem(KEY, v);
    } catch (e) {}
  }

  var choice = readChoice();
  if (choice === "granted" || choice === "denied") {
    gtag("consent", "update", { analytics_storage: choice });
  }

  gtag("js", new Date());
  gtag("config", GA_ID);

  var s = document.createElement("script");
  s.async = true;
  s.src = "https://www.googletagmanager.com/gtag/js?id=" + GA_ID;
  document.head.appendChild(s);

  function track(name, params) {
    gtag("event", name, params || {});
  }

  function setupBanner() {
    var banner = document.getElementById("consent");
    if (!banner) return;
    var show = function () {
      banner.hidden = false;
    };
    if (!choice) show();
    banner.addEventListener("click", function (e) {
      var v = e.target && e.target.getAttribute("data-consent");
      if (!v) return;
      choice = v;
      saveChoice(v);
      gtag("consent", "update", { analytics_storage: v });
      banner.hidden = true;
    });
    var reopen = document.getElementById("consent-open");
    if (reopen)
      reopen.addEventListener("click", function (e) {
        e.preventDefault();
        show();
      });
  }

  function setupCtas() {
    document.querySelectorAll("[data-cta]").forEach(function (el) {
      el.addEventListener("click", function () {
        track("cta_click", { cta: el.getAttribute("data-cta") });
      });
    });
  }

  function setupSections() {
    if (!("IntersectionObserver" in window)) return;
    var seen = {};
    var io = new IntersectionObserver(
      function (entries) {
        entries.forEach(function (en) {
          var id = en.target.id;
          if (en.isIntersecting && id && !seen[id]) {
            seen[id] = true;
            track("section_view", { section: id });
          }
        });
      },
      { threshold: 0.4 }
    );
    document.querySelectorAll("section[id]").forEach(function (el) {
      io.observe(el);
    });
  }

  function setupReadTime() {
    var marks = [15, 30, 60, 120, 300];
    var visibleMs = 0;
    var last = Date.now();
    setInterval(function () {
      var now = Date.now();
      if (document.visibilityState === "visible") visibleMs += now - last;
      last = now;
      while (marks.length && visibleMs >= marks[0] * 1000) {
        track("read_time", { seconds: marks.shift() });
      }
    }, 1000);
  }

  function setupCopy() {
    document.querySelectorAll("[data-copy]").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var target = document.getElementById(btn.getAttribute("data-copy"));
        if (!target) return;
        var text = target.innerText;
        var done = function () {
          btn.textContent = "Copied";
          setTimeout(function () {
            btn.textContent = "Copy";
          }, 1500);
        };
        if (navigator.clipboard) navigator.clipboard.writeText(text).then(done, function () {});
        track("copy_command", { command: btn.getAttribute("data-copy") });
      });
    });
  }

  function init() {
    setupBanner();
    setupCtas();
    setupSections();
    setupReadTime();
    setupCopy();
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
