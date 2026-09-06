(() => {
  'use strict';

  const repo = 'masarray/obs-vst3';
  const apiUrl = `https://api.github.com/repos/${repo}/releases/latest`;
  const releasePage = `https://github.com/${repo}/releases/latest`;
  const fallback = {
    installer: `${releasePage}/download/OBS-Safe-VST3-Host-Setup-x64.exe`,
    portable: `${releasePage}/download/OBS-Safe-VST3-Host-Windows-x64-Portable.zip`,
    checksums: `${releasePage}/download/SHA256SUMS.txt`,
    release: releasePage,
  };

  const all = (selector) => Array.from(document.querySelectorAll(selector));
  const isLandingPage = document.body?.classList.contains('landing-page');
  const languageStorageKey = 'obs-vst3-language';
  const indonesiaTimezones = new Set([
    'Asia/Jakarta',
    'Asia/Pontianak',
    'Asia/Makassar',
    'Asia/Jayapura',
  ]);

  let currentLanguage = 'en';
  let capturedTextNodes = [];

  const idText = new Map(Object.entries({
    'Skip to main content': 'Lewati ke konten utama',
    'Safe host for creators': 'Host aman untuk kreator',
    'Why': 'Mengapa',
    'How it works': 'Cara kerja',
    'Safety': 'Keamanan',
    'Compatibility': 'Kompatibilitas',
    'Download free': 'Unduh gratis',
    'Free · Open source · Windows x64 · OBS 29.1+': 'Gratis · Open source · Windows x64 · OBS 29.1+',
    'Use the VST3 plug-ins you already love.': 'Gunakan plug-in VST3 yang sudah Anda kenal dan sukai.',
    'Keep OBS focused on the show.': 'Biarkan OBS fokus pada siaran Anda.',
    'Bring modern EQs, compressors, de-essers, limiters and creative effects into OBS without turning': 'Bawa EQ modern, kompresor, de-esser, limiter, dan efek kreatif ke OBS tanpa menjadikan',
    'into the place where third-party VST3 code lives.': 'sebagai tempat kode VST3 pihak ketiga berjalan.',
    'One effect or a full Rack': 'Satu efek atau Rack lengkap',
    'Native plug-in windows': 'Jendela plug-in asli',
    'Remembers your working chain': 'Mengingat chain terakhir Anda',
    'Dry audio stays available': 'Audio dry tetap tersedia',
    'Download for Windows': 'Unduh untuk Windows',
    'See how it works': 'Lihat cara kerjanya',
    'Installer + portable ZIP': 'Installer + ZIP portable',
    'No account · No subscription · No telemetry requirement · Direct GitHub Release download': 'Tanpa akun · Tanpa langganan · Tidak wajib telemetri · Unduh langsung dari GitHub Release',
    'Microphone · Live chain': 'Mikrofon · Chain live',
    'PLUGIN CHAIN': 'CHAIN PLUG-IN',
    '3 active': '3 aktif',
    'Your VST3 plug-in · native editor': 'Plug-in VST3 Anda · editor asli',
    'Add VST3 effect': 'Tambah efek VST3',
    'Signal sent to OBS': 'Sinyal dikirim ke OBS',
    'INPUT TRIM': 'TRIM INPUT',
    'OUTPUT': 'OUTPUT',
    'Rack ready': 'Rack siap',
    'Auto recall on': 'Auto recall aktif',
    'Conceptual presentation of the current Rack workflow: compact effect slots, master gain and final-output broadcast metering.': 'Gambaran alur Rack saat ini: slot efek ringkas, master gain, dan metering broadcast pada output akhir.',
    'Use your own VST3s': 'Gunakan VST3 milik Anda',
    'Keep the tools and sound you already know.': 'Tetap gunakan tools dan karakter suara yang sudah Anda kenal.',
    'Native vendor UI': 'UI asli dari vendor',
    'Open the plug-in interface instead of a generic parameter wall.': 'Buka antarmuka asli plug-in, bukan deretan parameter generik.',
    'Built for live recovery': 'Dirancang untuk recovery saat live',
    'If the wet path is unavailable, audio can stay dry instead of disappearing.': 'Jika jalur wet bermasalah, audio dapat tetap dry daripada menghilang.',
    'Direct official downloads': 'Unduhan resmi langsung',
    'Installer, portable ZIP and SHA-256 from GitHub Releases.': 'Installer, ZIP portable, dan SHA-256 langsung dari GitHub Releases.',
    'Why this exists': 'Mengapa ini dibuat',
    'Modern plug-ins belong in your audio workflow—not in your troubleshooting routine.': 'Plug-in modern seharusnya membantu workflow audio Anda—bukan menambah pekerjaan troubleshooting.',
    'OBS creators should be able to shape a microphone or program feed with familiar VST3 tools without first learning a hosting architecture. OBS Safe VST3 Host keeps the everyday workflow straightforward and pushes isolation, recovery and state handling into the background.': 'Kreator OBS seharusnya bisa membentuk suara mikrofon atau program feed dengan tools VST3 yang sudah familiar tanpa harus mempelajari arsitektur hosting terlebih dahulu. OBS Safe VST3 Host menjaga workflow sehari-hari tetap sederhana, sementara isolasi, recovery, dan state handling bekerja di belakang layar.',
    'THE PROMISE': 'JANJINYA SEDERHANA',
    'Load. Listen. Go live.': 'Load. Dengarkan. Mulai live.',
    'Choose one VST3 effect for a simple job, or build a serial Rack when your chain grows. Open the real plug-in window, make the sound right, then let the host remember where you left it.': 'Pilih satu efek VST3 untuk kebutuhan sederhana, atau bangun Rack serial saat chain Anda bertambah. Buka jendela plug-in aslinya, atur sampai suaranya tepat, lalu biarkan host mengingat kondisi terakhirnya.',
    'Simple first': 'Mulai dari yang sederhana',
    'Rack when needed': 'Gunakan Rack saat perlu',
    'Safe by design': 'Aman sejak desain',
    'Keep your favorite tools': 'Tetap pakai tools favorit Anda',
    'Use modern VST3 plug-ins from vendors you already trust instead of rebuilding your sound around a special OBS-only effect set.': 'Gunakan plug-in VST3 modern dari vendor yang sudah Anda percaya tanpa harus membangun ulang karakter suara dengan efek khusus OBS.',
    'Build the chain you actually need': 'Bangun chain sesuai kebutuhan',
    'Start with one effect. Add EQ, dynamics and limiting only when the job calls for it.': 'Mulai dari satu efek. Tambahkan EQ, dynamics, dan limiting hanya ketika memang dibutuhkan.',
    'Come back to the same working Rack': 'Kembali ke Rack yang sama seperti terakhir digunakan',
    'Normal OBS restarts can restore the latest working topology, bypass state and captured VST3 state automatically.': 'Restart OBS secara normal dapat memulihkan topology terakhir, status bypass, dan state VST3 yang tersimpan secara otomatis.',
    'Prefer dry audio over dead air': 'Lebih baik audio dry daripada dead air',
    'Recovery behavior is designed around keeping the original signal available when a processed result is not ready.': 'Mekanisme recovery dirancang agar sinyal asli tetap tersedia ketika hasil processing belum siap.',
    'From installer to live audio': 'Dari installer sampai audio live',
    'Three steps. No VST hosting degree required.': 'Tiga langkah. Tidak perlu jadi ahli VST hosting.',
    'The technical boundaries are there for reliability. The creator workflow stays familiar.': 'Batas teknis ada untuk menjaga keandalan. Workflow kreator tetap familiar.',
    'Install the host': 'Instal host',
    'Run the Windows installer, then restart OBS. The package adds the VST3 host and Rack components needed by OBS.': 'Jalankan installer Windows, lalu restart OBS. Paket akan menambahkan komponen VST3 Host dan Rack yang dibutuhkan OBS.',
    'Installation guide →': 'Panduan instalasi →',
    'Add an audio filter': 'Tambahkan audio filter',
    'On a microphone or other OBS audio source, add either': 'Pada mikrofon atau sumber audio OBS lainnya, tambahkan',
    'for one effect or': 'untuk satu efek atau',
    'for a serial chain.': 'untuk chain serial.',
    'Choose your plug-ins and listen': 'Pilih plug-in lalu dengarkan',
    'Open the native vendor editor, shape the sound, and use the Rack master meters when you need final-output level context.': 'Buka editor asli dari vendor, bentuk karakter suaranya, lalu gunakan master meter Rack saat Anda perlu melihat level output akhir.',
    'Two ways to work': 'Dua cara bekerja',
    'One effect when that is enough. A Rack when it is not.': 'Satu efek jika itu sudah cukup. Gunakan Rack ketika Anda butuh lebih.',
    'The project intentionally keeps both workflows. You do not need a multi-effect surface just to run one compressor.': 'Proyek ini sengaja mempertahankan kedua workflow. Anda tidak perlu membuka surface multi-effect hanya untuk menjalankan satu compressor.',
    'Ideal for one EQ, compressor, de-esser or limiter. Discover a VST3, open its vendor UI, save state and keep the path focused.': 'Ideal untuk satu EQ, compressor, de-esser, atau limiter. Temukan VST3, buka UI asli vendornya, simpan state, dan jaga jalur tetap sederhana.',
    'Add, replace, remove, reorder and bypass multiple VST3 effects inside a dedicated graphical Rack editor.': 'Tambah, ganti, hapus, ubah urutan, dan bypass beberapa efek VST3 di dalam editor Rack grafis khusus.',
    'YOUR LIVE SIGNAL': 'SINYAL LIVE ANDA',
    'What the Rack actually does': 'Apa yang sebenarnya dikerjakan Rack',
    'Input Trim': 'Input Trim',
    'Pre-chain level': 'Level sebelum chain',
    'VST3 Chain': 'Chain VST3',
    '1…N effects': '1…N efek',
    'Output Fader': 'Output Fader',
    'Final gain': 'Gain akhir',
    'Final signal': 'Sinyal akhir',
    'Post-output telemetry': 'Telemetri setelah output',
    'Built for repeatable live work': 'Dibuat untuk workflow live yang konsisten',
    'Your Rack should feel like part of the setup—not a temporary experiment.': 'Rack Anda harus terasa sebagai bagian dari setup—bukan eksperimen sementara.',
    'Useful persistence is not a “save preset or lose everything” experience. The working Rack can restore itself across normal OBS restarts.': 'Persistence yang baik bukan pengalaman “simpan preset atau kehilangan semuanya”. Rack yang sedang dipakai dapat memulihkan dirinya setelah restart OBS normal.',
    'Working-state continuity': 'Kondisi kerja tetap berlanjut',
    'Topology, bypass and captured VST3 state can come back automatically after a normal restart.': 'Topology, bypass, dan state VST3 yang tersimpan dapat kembali otomatis setelah restart normal.',
    'Reusable chains when you want them': 'Chain reusable saat Anda membutuhkannya',
    'Named presets still exist for intentional reusable setups, without being mandatory for ordinary continuity.': 'Preset bernama tetap tersedia untuk setup yang memang ingin digunakan ulang, tetapi tidak wajib hanya untuk mempertahankan kondisi kerja biasa.',
    'Less visual churn during edits': 'Tampilan lebih tenang saat diedit',
    'The Rack keeps the last committed topology visible while an authoritative update is pending, reducing distracting UI flicker.': 'Rack mempertahankan topology terakhir yang sudah committed ketika update authoritative masih berlangsung, sehingga flicker UI yang mengganggu berkurang.',
    'Safety without the scary wording': 'Keamanan tanpa istilah yang menakutkan',
    'If a third-party plug-in misbehaves, OBS should not be the first thing it owns.': 'Jika plug-in pihak ketiga bermasalah, OBS seharusnya bukan yang pertama ikut jatuh.',
    'The project places VST3 runtime work, vendor interfaces and scanning behind separate process boundaries. That does not turn unknown plug-ins into safe software, but it gives live production a better failure boundary than loading everything directly into the OBS process.': 'Proyek ini menempatkan runtime VST3, antarmuka vendor, dan proses scanning di balik batas proses yang terpisah. Ini tidak otomatis membuat plug-in yang tidak dikenal menjadi aman, tetapi memberi produksi live batas kegagalan yang lebih baik dibanding memuat semuanya langsung ke proses OBS.',
    'Separate helper processes.': 'Helper process terpisah.',
    'Third-party VST3 work is kept outside': 'Pekerjaan VST3 pihak ketiga dijaga tetap di luar',
    'Dry-first behavior.': 'Perilaku dry-first.',
    'When wet audio is unavailable, the design prefers the original signal over silence.': 'Saat audio wet tidak tersedia, desain lebih memilih sinyal asli daripada silence.',
    'Output attenuation remains authoritative.': 'Attenuation output tetap authoritative.',
    'Your output fader still matters on fail-dry output.': 'Output fader Anda tetap berlaku pada output fail-dry.',
    'No false promise.': 'Tanpa janji palsu.',
    'Isolation is not a malware sandbox; only install VST3s from vendors you trust.': 'Isolasi bukan sandbox malware; instal VST3 hanya dari vendor yang Anda percaya.',
    'Read the exact safety boundary →': 'Baca batas keamanan secara lengkap →',
    'Straight answers': 'Jawaban langsung',
    'Before you install.': 'Sebelum Anda menginstal.',
    'Does this package include VST3 effects?': 'Apakah paket ini sudah termasuk efek VST3?',
    'No. It is a VST3 host for OBS. You install the audio plug-ins you want from their own vendors, then select them inside the Single Host or Rack.': 'Tidak. Ini adalah VST3 host untuk OBS. Anda menginstal plug-in audio yang diinginkan dari vendor masing-masing, lalu memilihnya di Single Host atau Rack.',
    'Will every VST3 plug-in work?': 'Apakah semua plug-in VST3 pasti bekerja?',
    'No host can promise universal compatibility with every third-party plug-in. The project has a documented compatibility boundary and test process. Check the compatibility page for the current support model.': 'Tidak ada host yang dapat menjamin kompatibilitas universal untuk setiap plug-in pihak ketiga. Proyek ini memiliki batas kompatibilitas dan proses pengujian yang terdokumentasi. Lihat halaman kompatibilitas untuk model dukungan saat ini.',
    'Does the Rack remember my chain after restarting OBS?': 'Apakah Rack mengingat chain setelah OBS direstart?',
    'Yes. The current stable Rack includes durable working-state recall for normal restart continuity, while named presets remain available for reusable setups.': 'Ya. Rack stabil saat ini memiliki working-state recall yang tahan restart normal, sementara preset bernama tetap tersedia untuk setup yang ingin digunakan ulang.',
    'What happens if the processed path is not ready?': 'Apa yang terjadi jika jalur processing belum siap?',
    'The runtime is designed to keep a bounded dry/pass-through path available instead of making the realtime audio thread wait indefinitely for wet processing.': 'Runtime dirancang agar jalur dry/pass-through yang terbatas tetap tersedia, daripada membuat realtime audio thread menunggu processing wet tanpa batas.',
    'Is macOS supported?': 'Apakah macOS didukung?',
    'The current public stable package is Windows x64. The website and release assets should be treated as Windows-only unless a future release explicitly says otherwise.': 'Paket stabil publik saat ini adalah Windows x64. Website dan asset release harus dianggap Windows-only sampai ada release mendatang yang secara eksplisit menyatakan dukungan platform lain.',
    'Ready when you are': 'Siap saat Anda siap',
    'Put your VST3 chain where your live audio already lives.': 'Tempatkan chain VST3 tepat di tempat audio live Anda berada.',
    'Download the latest stable Windows installer directly. No Releases-page detour.': 'Unduh installer Windows stabil terbaru secara langsung. Tanpa harus mampir ke halaman Releases.',
    'Download installer': 'Unduh installer',
    'Portable ZIP': 'ZIP portable',
    'SHA-256 available': 'SHA-256 tersedia',
    'LATEST STABLE': 'RILIS STABIL TERBARU',
    'Installer': 'Installer',
    'Portable': 'Portable',
    'Release notes': 'Catatan rilis',
    'Install guide': 'Panduan instalasi',
    'Independent open-source project · GPL-3.0-or-later': 'Proyek open-source independen · GPL-3.0-or-later',
    'Install': 'Instalasi',
    'Roadmap': 'Roadmap',
    'VST and OBS are trademarks of their respective owners. This project is not affiliated with or endorsed by those trademark owners.': 'VST dan OBS adalah merek dagang milik pemegang hak masing-masing. Proyek ini tidak berafiliasi dengan dan tidak didukung oleh pemilik merek dagang tersebut.',
  }));

  const metadata = {
    en: {
      title: 'OBS Safe VST3 Host — Use Modern VST3 Plug-ins in OBS Studio',
      description: 'Bring modern VST3 audio plug-ins into OBS Studio on Windows. Use one effect or build an isolated serial Rack with native vendor GUIs, automatic recall, real master gain, LUFS-I and dBTP metering.',
      ogTitle: 'OBS Safe VST3 Host — Modern VST3 plug-ins in OBS',
      ogDescription: 'Use the VST3 plug-ins you already know inside OBS, with isolated hosting, a serial Rack, native plug-in windows, recall and broadcast metering.',
      twitterDescription: 'A creator-friendly VST3 host and serial audio Rack for OBS Studio on Windows.',
    },
    id: {
      title: 'OBS Safe VST3 Host — Gunakan Plug-in VST3 Modern di OBS Studio',
      description: 'Gunakan plug-in audio VST3 modern di OBS Studio pada Windows. Jalankan satu efek atau bangun Rack serial terisolasi dengan UI vendor asli, auto recall, master gain, LUFS-I, dan metering dBTP.',
      ogTitle: 'OBS Safe VST3 Host — Plug-in VST3 modern di OBS',
      ogDescription: 'Gunakan plug-in VST3 yang sudah Anda kenal di OBS dengan hosting terisolasi, Rack serial, jendela plug-in asli, recall, dan metering broadcast.',
      twitterDescription: 'VST3 host dan serial audio Rack yang ramah kreator untuk OBS Studio di Windows.',
    },
  };

  const attributeTranslations = [
    ['.navlinks', 'aria-label', 'Main navigation', 'Navigasi utama'],
    ['.heroValues', 'aria-label', 'Core product benefits', 'Manfaat utama produk'],
    ['.releaseLine', 'aria-label', 'Release information', 'Informasi rilis'],
    ['.rackStage', 'aria-label', 'Illustration of the OBS Safe VST3 Rack', 'Ilustrasi OBS Safe VST3 Rack'],
    ['.trustStrip', 'aria-label', 'Product highlights', 'Sorotan produk'],
    ['.signalCard', 'aria-label', 'Rack signal flow', 'Alur sinyal Rack'],
  ];

  function setDownloads(urls) {
    all('[data-download="installer"]').forEach((el) => { el.href = urls.installer; });
    all('[data-download="portable"]').forEach((el) => { el.href = urls.portable; });
    all('[data-download="checksums"]').forEach((el) => { el.href = urls.checksums; });
    all('[data-download="release"]').forEach((el) => { el.href = urls.release; });
  }

  function localizedReleaseTag(tag) {
    if (tag === 'latest stable') return currentLanguage === 'id' ? 'stabil terbaru' : 'latest stable';
    return tag;
  }

  function setReleaseText(tag, installerName, portableName) {
    all('[data-release-version]').forEach((el) => {
      el.dataset.releaseRaw = tag;
      el.textContent = localizedReleaseTag(tag);
    });
    all('[data-installer-name]').forEach((el) => { el.textContent = installerName; });
    all('[data-portable-name]').forEach((el) => { el.textContent = portableName; });
  }

  function preferredAsset(assets, versionedPattern, stableName) {
    return assets.find((asset) => versionedPattern.test(asset.name)) ||
      assets.find((asset) => asset.name === stableName) ||
      null;
  }

  function detectLanguage() {
    try {
      const saved = localStorage.getItem(languageStorageKey);
      if (saved === 'id' || saved === 'en') return saved;
    } catch (_) {
      // localStorage can be blocked; automatic detection still works.
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

  function ensureLandingStyles() {
    if (!isLandingPage || document.querySelector('link[data-landing-i18n]')) return;
    const link = document.createElement('link');
    link.rel = 'stylesheet';
    link.href = 'landing-i18n.css';
    link.dataset.landingI18n = 'true';
    document.head.appendChild(link);
  }

  function captureLandingText() {
    if (!isLandingPage) return;
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    const nodes = [];
    let node;

    while ((node = walker.nextNode())) {
      const parent = node.parentElement;
      if (!parent) continue;
      if (parent.closest('script, style, [data-release-version], [data-installer-name], [data-portable-name], .languageSwitcher')) continue;
      const raw = node.nodeValue || '';
      const trimmed = raw.trim();
      if (!trimmed) continue;
      const start = raw.indexOf(trimmed);
      nodes.push({
        node,
        english: trimmed,
        prefix: raw.slice(0, start),
        suffix: raw.slice(start + trimmed.length),
      });
    }

    capturedTextNodes = nodes;
  }

  function setMeta(selector, value) {
    const el = document.querySelector(selector);
    if (el) el.setAttribute('content', value);
  }

  function renderMetadata(language) {
    if (!isLandingPage) return;
    const copy = metadata[language] || metadata.en;
    document.title = copy.title;
    setMeta('meta[name="description"]', copy.description);
    setMeta('meta[property="og:title"]', copy.ogTitle);
    setMeta('meta[property="og:description"]', copy.ogDescription);
    setMeta('meta[name="twitter:description"]', copy.twitterDescription);
  }

  function renderAttributes(language) {
    attributeTranslations.forEach(([selector, attribute, english, indonesian]) => {
      const element = document.querySelector(selector);
      if (element) element.setAttribute(attribute, language === 'id' ? indonesian : english);
    });
  }

  function updateLanguageSwitcher(language) {
    const root = document.querySelector('.languageSwitcher');
    if (!root) return;
    const current = root.querySelector('.languageCurrent');
    const flag = root.querySelector('.langFlag');
    const code = root.querySelector('.langCode');
    const menu = root.querySelector('.languageMenu');

    if (flag) flag.textContent = language === 'id' ? '🇮🇩' : '🇬🇧';
    if (code) code.textContent = language === 'id' ? 'ID' : 'EN';
    if (current) current.setAttribute('aria-label', language === 'id' ? 'Ganti bahasa' : 'Change language');
    if (menu) menu.setAttribute('aria-label', language === 'id' ? 'Pilih bahasa' : 'Choose language');

    root.querySelectorAll('.languageOption').forEach((option) => {
      option.setAttribute('aria-current', option.dataset.lang === language ? 'true' : 'false');
    });
  }

  function refreshReleaseLanguage() {
    all('[data-release-version]').forEach((el) => {
      const raw = el.dataset.releaseRaw || el.textContent.trim();
      if (!el.dataset.releaseRaw) el.dataset.releaseRaw = raw;
      el.textContent = localizedReleaseTag(raw);
    });
  }

  function applyLanguage(language, persist = false) {
    if (!isLandingPage) return;
    currentLanguage = language === 'id' ? 'id' : 'en';
    document.documentElement.lang = currentLanguage;
    document.documentElement.dataset.language = currentLanguage;

    capturedTextNodes.forEach(({ node, english, prefix, suffix }) => {
      const translated = currentLanguage === 'id' ? (idText.get(english) || english) : english;
      node.nodeValue = `${prefix}${translated}${suffix}`;
    });

    renderMetadata(currentLanguage);
    renderAttributes(currentLanguage);
    refreshReleaseLanguage();
    updateLanguageSwitcher(currentLanguage);

    if (persist) {
      try {
        localStorage.setItem(languageStorageKey, currentLanguage);
      } catch (_) {
        // Language still changes for the current page when storage is blocked.
      }
    }
  }

  function buildLanguageSwitcher() {
    if (!isLandingPage || document.querySelector('.languageSwitcher')) return;
    const nav = document.querySelector('.navlinks');
    if (!nav) return;

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
        applyLanguage(option.dataset.lang, true);
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

  function initializeLandingLanguage() {
    if (!isLandingPage) return;
    ensureLandingStyles();
    captureLandingText();
    buildLanguageSwitcher();
    applyLanguage(detectLanguage(), false);
  }

  initializeLandingLanguage();
  setDownloads(fallback);

  fetch(apiUrl, {
    headers: { Accept: 'application/vnd.github+json' },
    cache: 'no-store',
  })
    .then((response) => {
      if (!response.ok) throw new Error(`GitHub API ${response.status}`);
      return response.json();
    })
    .then((release) => {
      const assets = Array.isArray(release.assets) ? release.assets : [];
      const installer = preferredAsset(
        assets,
        /^OBS-Safe-VST3-Host-v.+-Setup-x64\.exe$/i,
        'OBS-Safe-VST3-Host-Setup-x64.exe'
      );
      const portable = preferredAsset(
        assets,
        /^OBS-Safe-VST3-Host-v.+-Windows-x64-Portable\.zip$/i,
        'OBS-Safe-VST3-Host-Windows-x64-Portable.zip'
      );
      const checksums = assets.find((asset) => asset.name === 'SHA256SUMS.txt');
      const tag = release.tag_name || 'latest stable';

      setDownloads({
        installer: installer?.browser_download_url || fallback.installer,
        portable: portable?.browser_download_url || fallback.portable,
        checksums: checksums?.browser_download_url || fallback.checksums,
        release: release.html_url || fallback.release,
      });

      setReleaseText(
        tag,
        installer?.name || 'OBS-Safe-VST3-Host-Setup-x64.exe',
        portable?.name || 'OBS-Safe-VST3-Host-Windows-x64-Portable.zip'
      );

      document.documentElement.dataset.releaseSync = 'ready';
    })
    .catch(() => {
      setReleaseText(
        'latest stable',
        'OBS-Safe-VST3-Host-Setup-x64.exe',
        'OBS-Safe-VST3-Host-Windows-x64-Portable.zip'
      );
      document.documentElement.dataset.releaseSync = 'fallback';
    });
})();

// pages-publish-trigger: refresh project Pages from current main
