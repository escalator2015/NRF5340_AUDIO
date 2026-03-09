/**
 * app.js — Lógica WebSerial para Auracast Control
 *
 * Protocolo JSON+newline sobre USB CDC-ACM (WebSerial API).
 * Compatible con Chrome/Edge 89+ en contexto seguro (https:// o localhost).
 *
 * Comandos enviados al source:
 *   {"cmd":"list_sinks"}
 *   {"cmd":"get_info","sink":<idx>}
 *   {"cmd":"set_vol","sink":<idx>,"db":<-64..0>}
 *   {"cmd":"set_mute","sink":<idx>,"muted":<bool>}
 *   {"cmd":"set_name","sink":<idx>,"name":"<str>"}
 *
 * Respuestas recibidas del source:
 *   {"sinks":[{"idx":0,"id":"...","name":"...","vol_db":-15,"muted":false,"state":"ready"},...]}
 *   {"ok":true}
 *   {"ok":false,"err":"..."}
 *   {"info":"..."}   // mensajes de estado del dispositivo
 */

'use strict';

/* ═══════════════════════════════════════════════════════════════════
   SerialController — gestión del puerto WebSerial
═══════════════════════════════════════════════════════════════════ */
class SerialController {
  constructor() {
    this.port      = null;
    this.reader    = null;
    this.writer    = null;
    this._running  = false;
    this._lineBuffer = '';

    /** Callback invocado con cada objeto JSON recibido del source. */
    this.onMessage = null;
    /** Callback invocado al desconectar (error o cierre). */
    this.onDisconnect = null;
  }

  get connected() {
    return this._running && this.port !== null;
  }

  /**
   * Solicita al usuario seleccionar el puerto CDC-ACM del source.
   * Filtra automáticamente por USB (el usuario verá solo puertos USB).
   */
  async connect() {
    this.port = await navigator.serial.requestPort({ filters: [] });
    await this.port.open({ baudRate: 115200 });

    // Activar DTR: indica al firmware que el host está listo
    await this.port.setSignals({ dataTerminalReady: true });

    const encoder = new TextEncoderStream();
    encoder.readable.pipeTo(this.port.writable);
    this.writer = encoder.writable.getWriter();

    this._running = true;
    this._readLoop();
  }

  async disconnect() {
    this._running = false;
    try { this.reader?.cancel(); } catch (_) {}
    try { await this.writer?.close(); } catch (_) {}
    try { await this.port?.close(); } catch (_) {}
    this.port   = null;
    this.reader = null;
    this.writer = null;
    this._lineBuffer = '';
  }

  /** Envía un objeto JS como JSON+LF al source. */
  async send(obj) {
    if (!this.connected) {
      throw new Error('Puerto no conectado');
    }
    const line = JSON.stringify(obj) + '\n';
    console.log('[serial TX]', line.trim());
    await this.writer.write(line);
  }

  /* --- bucle de lectura de líneas -------------------------------- */
  async _readLoop() {
    const decoder = new TextDecoderStream();
    this.port.readable.pipeTo(decoder.writable);
    this.reader = decoder.readable.getReader();

    try {
      while (this._running) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (!value) continue;

        this._lineBuffer += value;
        const lines = this._lineBuffer.split('\n');
        this._lineBuffer = lines.pop(); // última parte incompleta

        for (const raw of lines) {
          const line = raw.replace(/\r/g, '').trim();
          if (!line) continue;
          console.log('[serial RX]', line);
          try {
            const msg = JSON.parse(line);
            if (this.onMessage) this.onMessage(msg);
          } catch (e) {
            console.warn('[serial RX] JSON parse error:', line, e);
          }
        }
      }
    } catch (err) {
      if (this._running) {
        console.error('Serial read error:', err);
      }
    } finally {
      this._running = false;
      if (this.onDisconnect) this.onDisconnect();
    }
  }
}


/* ═══════════════════════════════════════════════════════════════════
   UIRenderer — renderizado de cards de sinks
═══════════════════════════════════════════════════════════════════ */
class UIRenderer {
  constructor() {
    this.grid     = document.getElementById('sinks-grid');
    this.template = document.getElementById('sink-card-tpl');
    this.cards    = new Map(); // idx → DOM element
  }

  /** Actualiza o crea las cards según el array de sinks recibido. */
  render(sinks) {
    /* 1. Detectar indices presentes */
    const presentIdxs = new Set(sinks.map(s => s.idx));

    /* 2. Eliminar cards de sinks que ya no existen */
    for (const [idx, el] of this.cards) {
      if (!presentIdxs.has(idx)) {
        el.remove();
        this.cards.delete(idx);
      }
    }

    /* 3. Actualizar o crear */
    for (const sink of sinks) {
      if (this.cards.has(sink.idx)) {
        this._update(sink, this.cards.get(sink.idx));
      } else {
        const el = this._create(sink);
        this.grid.appendChild(el);
        this.cards.set(sink.idx, el);
        this._update(sink, el);
      }
    }
  }

  /** Elimina todas las cards. */
  clear() {
    this.grid.innerHTML = '';
    this.cards.clear();
  }

  /* --- creación de card ---------------------------------------- */
  _create(sink) {
    const frag = this.template.content.cloneNode(true);
    const col  = frag.querySelector('.col');
    col.dataset.sinkIdx = sink.idx;
    return frag.children[0]; // el nodo .col ya está fuera del fragment
  }

  /* --- actualización de card ----------------------------------- */
  _update(sink, col) {
    const card = col.querySelector('.sink-card');

    /* Estado CSS */
    card.classList.remove('state-ready', 'state-connecting',
                          'state-discovering', 'state-disconnected');
    card.classList.add(`state-${sink.state}`);

    /* Nombre e ID */
    col.querySelector('.sink-name').textContent = sink.name || `Receptor ${sink.idx}`;
    col.querySelector('.sink-id').textContent   = sink.id   || '';

    /* Badge estado */
    const badge = col.querySelector('.sink-state-badge');
    badge.textContent = SINK_STATE_LABELS[sink.state] ?? sink.state;
    badge.className = 'badge rounded-pill sink-state-badge ' + SINK_STATE_BADGE[sink.state];

    /* Slider volumen */
    const slider = col.querySelector('.sink-vol-slider');
    const volTxt = col.querySelector('.sink-vol-text');
    slider.value         = sink.vol_db ?? 0;
    slider.disabled      = (sink.state !== 'ready');
    volTxt.textContent   = `${sink.vol_db ?? 0} dB`;

    /* Botón mute */
    const btnMute    = col.querySelector('.btn-mute');
    const muteText   = col.querySelector('.btn-mute-text');
    btnMute.disabled = (sink.state !== 'ready');
    if (sink.muted) {
      btnMute.classList.replace('btn-outline-warning', 'btn-warning');
      muteText.textContent = 'Unmute';
    } else {
      btnMute.classList.replace('btn-warning', 'btn-outline-warning');
      muteText.textContent = 'Mute';
    }

    /* Botón renombrar */
    col.querySelector('.btn-rename').disabled      = (sink.state !== 'ready');
    col.querySelector('.btn-refresh-sink').disabled = false;

    /* Guardar datos en el DOM para los event listeners */
    col.dataset.sinkIdx  = sink.idx;
    col.dataset.sinkMuted = sink.muted ? '1' : '0';
  }
}


/* ═══════════════════════════════════════════════════════════════════
   Constantes de estado
═══════════════════════════════════════════════════════════════════ */
const SINK_STATE_LABELS = {
  idle:         'Libre',
  connecting:   'Conectando…',
  discovering:  'Descubriendo…',
  ready:        'Listo',
  disconnected: 'Desconectado',
};
const SINK_STATE_BADGE = {
  idle:         'bg-secondary',
  connecting:   'bg-warning text-dark',
  discovering:  'bg-warning text-dark',
  ready:        'bg-success',
  disconnected: 'bg-danger',
};


/* ═══════════════════════════════════════════════════════════════════
   App — controlador principal
═══════════════════════════════════════════════════════════════════ */
class App {
  constructor() {
    this.serial   = new SerialController();
    this.ui       = new UIRenderer();
    this.sinks    = [];     // último estado conocido de los sinks
    this._volTimers = {};   // debounce por sink idx
    this._toastEl = null;
    this._toast   = null;
    this._renameModal = null;
  }

  init() {
    /* Comprobar soporte WebSerial */
    if (!('serial' in navigator)) {
      document.getElementById('no-webserial').style.display = '';
      document.getElementById('btn-connect').disabled = true;
      return;
    }

    /* Inicializar Bootstrap toast y modal */
    this._toastEl = document.getElementById('app-toast');
    this._toast   = new bootstrap.Toast(this._toastEl, { delay: 3500 });
    this._renameModal = new bootstrap.Modal(document.getElementById('renameModal'));

    /* Callbacks del serial */
    this.serial.onMessage    = msg  => this._onMessage(msg);
    this.serial.onDisconnect = ()   => this._onDisconnect();

    /* Eventos de UI */
    document.getElementById('btn-connect')
            .addEventListener('click', () => this._toggleConnection());

    document.getElementById('btn-refresh')
            .addEventListener('click', () => this._listSinks());

    document.getElementById('btn-rename-confirm')
            .addEventListener('click', () => this._confirmRename());

    document.getElementById('renameModal')
            .addEventListener('show.bs.modal', e => {
              const btn = e.relatedTarget;
              const col = btn.closest('[data-sink-idx]');
              document.getElementById('rename-sink-idx').value = col.dataset.sinkIdx;
              const name = col.querySelector('.sink-name').textContent;
              const inp  = document.getElementById('rename-input');
              inp.value  = name.startsWith('Receptor') ? '' : name;
              setTimeout(() => inp.focus(), 300);
            });

    /* Delegación de eventos en la grid */
    this.ui.grid.addEventListener('input', e => {
      if (e.target.matches('.sink-vol-slider')) {
        this._onVolInput(e.target);
      }
    });

    this.ui.grid.addEventListener('click', e => {
      const btn = e.target.closest('button');
      if (!btn) return;

      const col = btn.closest('[data-sink-idx]');
      if (!col) return;
      const idx = parseInt(col.dataset.sinkIdx, 10);

      if (btn.matches('.btn-mute')) {
        const muted = col.dataset.sinkMuted === '1';
        this._sendCmd({ cmd: 'set_mute', sink: idx, muted: !muted });
      } else if (btn.matches('.btn-refresh-sink')) {
        this._sendCmd({ cmd: 'get_info', sink: idx });
      }
    });
  }

  /* --- Conexión / desconexión ----------------------------------- */
  async _toggleConnection() {
    if (this.serial.connected) {
      await this.serial.disconnect();
      this._setConnBadge(false);
      document.getElementById('btn-connect').textContent = 'Conectar dispositivo';
      document.getElementById('btn-refresh').disabled = true;
      this.ui.clear();
      this._showState('empty');
    } else {
      try {
        await this.serial.connect();
        this._setConnBadge(true);
        document.getElementById('btn-connect').textContent = 'Desconectar';
        document.getElementById('btn-refresh').disabled = false;
        this._showState('loading');
        await this._listSinks();
      } catch (err) {
        if (err.name !== 'NotFoundError') {
          this._toast_show(`Error al conectar: ${err.message}`, 'danger');
        }
      }
    }
  }

  _onDisconnect() {
    this._setConnBadge(false);
    document.getElementById('btn-connect').textContent = 'Conectar dispositivo';
    document.getElementById('btn-refresh').disabled = true;
    this._toast_show('Dispositivo desconectado', 'warning');
    this.ui.clear();
    this._showState('empty');
  }

  /* --- Mensajes recibidos --------------------------------------- */
  _onMessage(msg) {
    console.log('[msg]', JSON.stringify(msg));
    if (msg.sinks !== undefined) {
      /* Respuesta a list_sinks */
      this.sinks = msg.sinks;
      console.log(`[msg] sinks count=${msg.sinks.length}`, msg.sinks.map(s => `${s.idx}:${s.vol_db}dB(${s.state})`));
      this._showState(this.sinks.length === 0 ? 'empty' : 'grid');
      this.ui.render(this.sinks);
      return;
    }

    if (msg.ok === true) {
      /* ACK genérico de un comando → re-list silencioso (sin ocultar el grid
       * para que el slider no desaparezca mientras se espera la respuesta) */
      this._listSinks(false);
      return;
    }

    if (msg.ok === false) {
      this._toast_show(`Error del dispositivo: ${msg.err ?? 'desconocido'}`, 'danger');
      return;
    }

    if (msg.info) {
      console.info('[device]', msg.info);
    }
  }

  /* --- Envío de comandos --------------------------------------- */
  async _sendCmd(obj) {
    try {
      await this.serial.send(obj);
    } catch (err) {
      this._toast_show(`Error de envío: ${err.message}`, 'danger');
    }
  }

  async _listSinks(showLoader = true) {
    console.log(`[listSinks] showLoader=${showLoader}`);
    if (showLoader) {
      this._showState('loading');
    }
    await this._sendCmd({ cmd: 'list_sinks' });
  }

  /* --- Slider de volumen (con debounce 300 ms) ----------------- */
  _onVolInput(slider) {
    const col = slider.closest('[data-sink-idx]');
    const idx = parseInt(col.dataset.sinkIdx, 10);
    const db  = parseInt(slider.value, 10);

    console.log(`[vol] input sink=${idx} db=${db} slider.disabled=${slider.disabled}`);
    col.querySelector('.sink-vol-text').textContent = `${db} dB`;

    clearTimeout(this._volTimers[idx]);
    this._volTimers[idx] = setTimeout(() => {
      console.log(`[vol] debounce fired → set_vol sink=${idx} db=${db}`);
      this._sendCmd({ cmd: 'set_vol', sink: idx, db });
    }, 300);
  }

  /* --- Modal renombrar ----------------------------------------- */
  _confirmRename() {
    const idx  = parseInt(document.getElementById('rename-sink-idx').value, 10);
    const name = document.getElementById('rename-input').value.trim();
    if (!name) {
      document.getElementById('rename-input').focus();
      return;
    }
    this._renameModal.hide();
    this._sendCmd({ cmd: 'set_name', sink: idx, name });
  }

  /* --- Helpers de UI ------------------------------------------- */
  _setConnBadge(connected) {
    const badge = document.getElementById('badge-conn');
    if (connected) {
      badge.className = 'badge rounded-pill bg-success';
      badge.textContent = 'Conectado';
    } else {
      badge.className = 'badge rounded-pill bg-secondary';
      badge.textContent = 'Sin conexión';
    }
  }

  /** Muestra uno de los estados: 'loading' | 'empty' | 'grid' */
  _showState(state) {
    // Usar 'flex' en lugar de '' para sobreescribir el CSS display:none
    // que tienen #loading-row y #empty-row definido en la hoja de estilos.
    document.getElementById('loading-row').style.display =
      state === 'loading' ? 'flex' : 'none';
    document.getElementById('empty-row').style.display =
      state === 'empty' ? 'flex' : 'none';
    document.getElementById('sinks-grid').style.display =
      state === 'grid' ? 'flex' : 'none';
  }

  _toast_show(msg, type = 'success') {
    const el = this._toastEl;
    el.classList.remove('text-bg-success', 'text-bg-danger',
                        'text-bg-warning', 'text-bg-info', 'text-bg-secondary');
    el.classList.add(`text-bg-${type}`);
    document.getElementById('toast-msg').textContent = msg;
    this._toast.show();
  }
}


/* ═══════════════════════════════════════════════════════════════════
   Bootstrap
═══════════════════════════════════════════════════════════════════ */
document.addEventListener('DOMContentLoaded', () => {
  const app = new App();
  app.init();
  // Ocultar grid inicialmente
  document.getElementById('sinks-grid').style.display = 'none';
});
