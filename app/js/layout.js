/* GESTUUM — Layout loader
   Injeta nav e footer compartilhados nos slots #site-nav e #site-footer.
   Marca o link atual como .active baseado em location.pathname.
   Liga scroll listener (toggle .nav.scrolled) e mobile toggle.
   Usado em index, devices, contato. Wizards mantem layout proprio. */

(function () {
  'use strict';

  // ---- Resolucao do link ativo a partir do pathname ----
  function detectActiveKey() {
    var path = (location.pathname || '').toLowerCase();
    var hash = (location.hash || '').toLowerCase();

    // Fim do pathname (ex: "/contato.html" -> "contato.html")
    var file = path.split('/').pop() || 'index.html';

    if (file === 'devices.html') return 'devices';
    if (file === 'contato.html') return 'contato';
    if (file === 'install.html') return 'install';
    if (file === 'gestuum-app.html') return 'gestuum-app';

    // index ou raiz: usar hash se existir (#para-quem -> para-quem)
    if (file === '' || file === 'index.html') {
      if (hash.length > 1) return hash.slice(1);
      return null;
    }

    return null;
  }

  function markActive(navRoot) {
    var key = detectActiveKey();
    if (!key) return;
    var link = navRoot.querySelector('[data-link="' + key + '"]');
    if (link) link.classList.add('active');
  }

  // ---- Scroll behavior ----
  function bindScroll(nav) {
    function handle() {
      if (window.scrollY > 40) {
        nav.classList.add('scrolled');
      } else {
        nav.classList.remove('scrolled');
      }
    }
    window.addEventListener('scroll', handle, { passive: true });
    handle();
  }

  // ---- Mobile toggle ----
  function bindToggle(navRoot) {
    var toggle = navRoot.querySelector('#navToggle');
    var links = navRoot.querySelector('#navLinks');
    if (!toggle || !links) return;

    toggle.addEventListener('click', function () {
      links.classList.toggle('open');
    });

    links.querySelectorAll('a').forEach(function (a) {
      a.addEventListener('click', function () {
        links.classList.remove('open');
      });
    });
  }

  // ---- Carrega um partial via fetch e injeta no slot ----
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
        console.error('[layout] Falha ao carregar', url, err);
        return null;
      });
  }

  // ---- Inicializacao ----
  function init() {
    Promise.all([
      loadPartial('site-nav', 'partials/nav.html'),
      loadPartial('site-footer', 'partials/footer.html')
    ]).then(function (slots) {
      var navSlot = slots[0];
      if (navSlot) {
        var nav = navSlot.querySelector('.nav');
        if (nav) {
          markActive(nav);
          bindScroll(nav);
          bindToggle(nav);
        }
      }
      // Sinal opcional para que paginas saibam que o layout terminou
      document.dispatchEvent(new CustomEvent('layout:ready'));
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
