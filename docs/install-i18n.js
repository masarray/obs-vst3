(() => {
  'use strict';

  const storageKey = 'obs-vst3-language';
  const indonesiaTimezones = new Set([
    'Asia/Jakarta',
    'Asia/Pontianak',
    'Asia/Makassar',
    'Asia/Jayapura',
  ]);

  function detectLanguage() {
    try {
      const saved = localStorage.getItem(storageKey);
      if (saved === 'id' || saved === 'en') return saved;
    } catch (_) {
      // Continue with automatic detection when storage is unavailable.
    }

    try {
      const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone;
      if (indonesiaTimezones.has(timezone)) return 'id';
    } catch (_) {
      // Continue with locale detection.
    }

    const locales = Array.isArray(navigator.languages) && navigator.languages.length
      ? navigator.languages
      : [navigator.language].filter(Boolean);

    for (const locale of locales) {
      const normalized = String(locale || '').toLowerCase();
      if (normalized === 'id' || normalized.startsWith('id-') || normalized.endsWith('-id')) return 'id';
      try {
        if (typeof Intl.Locale === 'function' && new Intl.Locale(locale).region === 'ID') return 'id';
      } catch (_) {
        // Ignore malformed locale values.
      }
    }

    return 'en';
  }

  function setLanguage(language, persist = false) {
    const normalized = language === 'id' ? 'id' : 'en';
    document.documentElement.lang = normalized;
    document.documentElement.dataset.language = normalized;

    const root = document.querySelector('.languageSwitcher');
    if (root) {
      const current = root.querySelector('.languageCurrent');
      const flag = root.querySelector('.langFlag');
      const code = root.querySelector('.langCode');
      const menu = root.querySelector('.languageMenu');

      if (flag) flag.textContent = normalized === 'id' ? '🇮🇩' : '🇬🇧';
      if (code) code.textContent = normalized === 'id' ? 'ID' : 'EN';
      if (current) current.setAttribute('aria-label', normalized === 'id' ? 'Ganti bahasa' : 'Change language');
      if (menu) menu.setAttribute('aria-label', normalized === 'id' ? 'Pilih bahasa' : 'Choose language');
      root.querySelectorAll('.languageOption').forEach((option) => {
        option.setAttribute('aria-current', option.dataset.lang === normalized ? 'true' : 'false');
      });
    }

    const metadata = {
      en: {
        title: 'Install OBS Safe VST3 Host on Windows — Beginner Guide',
        description: 'Step-by-step beginner guide to install OBS Safe VST3 Host, verify the OBS filter, discover installed VST3 effects, open the native plug-in interface and troubleshoot first setup.',
      },
      id: {
        title: 'Instal OBS Safe VST3 Host di Windows — Panduan Pemula',
        description: 'Panduan langkah demi langkah untuk menginstal OBS Safe VST3 Host, memeriksa filter OBS, menemukan efek VST3, membuka interface plug-in, dan mengatasi masalah setup pertama.',
      },
    };
    const copy = metadata[normalized];
    document.title = copy.title;
    const description = document.querySelector('meta[name="description"]');
    if (description) description.setAttribute('content', copy.description);

    if (persist) {
      try {
        localStorage.setItem(storageKey, normalized);
      } catch (_) {
        // The page still switches language for this session.
      }
    }
  }

  function buildSwitcher() {
    const nav = document.querySelector('.navlinks');
    if (!nav || document.querySelector('.languageSwitcher')) return;

    const root = document.createElement('div');
    root.className = 'languageSwitcher';
    root.innerHTML = `
      <button class="languageCurrent" type="button" aria-haspopup="menu" aria-expanded="false">
        <span class="langFlag" aria-hidden="true">🇬🇧</span>
        <span class="langCode">EN</span>
        <span class="langChevron" aria-hidden="true">▾</span>
      </button>
      <div class="languageMenu" role="menu" hidden>
        <button class="languageOption" type="button" role="menuitemradio" data-lang="id">
          <span class="optionFlag" aria-hidden="true">🇮🇩</span><span>Bahasa Indonesia</span><span class="optionCheck" aria-hidden="true">✓</span>
        </button>
        <button class="languageOption" type="button" role="menuitemradio" data-lang="en">
          <span class="optionFlag" aria-hidden="true">🇬🇧</span><span>English</span><span class="optionCheck" aria-hidden="true">✓</span>
        </button>
      </div>`;

    nav.appendChild(root);
    const current = root.querySelector('.languageCurrent');
    const menu = root.querySelector('.languageMenu');

    const closeMenu = () => {
      menu.hidden = true;
      current.setAttribute('aria-expanded', 'false');
    };

    current.addEventListener('click', (event) => {
      event.stopPropagation();
      const willOpen = menu.hidden;
      menu.hidden = !willOpen;
      current.setAttribute('aria-expanded', willOpen ? 'true' : 'false');
    });

    root.querySelectorAll('.languageOption').forEach((option) => {
      option.addEventListener('click', () => {
        setLanguage(option.dataset.lang, true);
        closeMenu();
        current.focus();
      });
    });

    document.addEventListener('click', (event) => {
      if (!root.contains(event.target)) closeMenu();
    });

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && !menu.hidden) {
        closeMenu();
        current.focus();
      }
    });
  }

  buildSwitcher();
  setLanguage(detectLanguage(), false);
})();
