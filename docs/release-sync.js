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

  function setDownloads(urls) {
    all('[data-download="installer"]').forEach((el) => { el.href = urls.installer; });
    all('[data-download="portable"]').forEach((el) => { el.href = urls.portable; });
    all('[data-download="checksums"]').forEach((el) => { el.href = urls.checksums; });
    all('[data-download="release"]').forEach((el) => { el.href = urls.release; });
  }

  function setReleaseText(tag, installerName, portableName) {
    all('[data-release-version]').forEach((el) => { el.textContent = tag; });
    all('[data-installer-name]').forEach((el) => { el.textContent = installerName; });
    all('[data-portable-name]').forEach((el) => { el.textContent = portableName; });
  }

  function preferredAsset(assets, versionedPattern, stableName) {
    return assets.find((asset) => versionedPattern.test(asset.name)) ||
      assets.find((asset) => asset.name === stableName) ||
      null;
  }

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
