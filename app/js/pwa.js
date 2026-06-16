/* GESTUUM — PWA layout loader
   Injeta o header padrao (partials/pwa-header.html) no slot #pwa-header.
   Le data-pwa-app no <body> para o nome da app ("Instalador" / "Configurador").
   Move o conteudo de <template id="pwa-header-extra"> para o slot do header
   — assim cada PWA injeta seus widgets (bateria, BLE, step atual...) sem
   precisar reescrever o markup do header. */

(function () {
  'use strict';

  function loadPartial(slotId, url) {
    var slot = document.getElementById(slotId);
    if (!slot) return Promise.resolve(null);

    return fetch(url, { cache: 'no-cache' })
      .then(function (r) {
        if (!r.ok) throw new Error('HTTP ' + r.status + ' em ' + url);
        return r.text();
      })
      .then(function (html) {
        slot.innerHTML = html;
        return slot;
      })
      .catch(function (err) {
        console.error('[pwa] Falha ao carregar', url, err);
        return null;
      });
  }

  function configureHeader(slot) {
    if (!slot) return;

    // Nome da aplicacao (Instalador / Configurador) lido do <body data-pwa-app="...">
    var appName = document.body.getAttribute('data-pwa-app') || 'App';
    var nameEl = slot.querySelector('[data-pwa-name]');
    if (nameEl) nameEl.textContent = appName;

    // Mover conteudo de <template id="pwa-header-extra"> para o slot do header
    var extra = document.getElementById('pwa-header-extra');
    var headerSlot = slot.querySelector('#pwa-header-slot');
    if (extra && extra.content && headerSlot) {
      headerSlot.appendChild(extra.content.cloneNode(true));
    }
  }

  function init() {
    loadPartial('pwa-header', 'partials/pwa-header.html').then(function (slot) {
      configureHeader(slot);
      document.dispatchEvent(new CustomEvent('pwa:ready'));
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
