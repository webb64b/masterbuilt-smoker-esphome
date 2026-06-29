// Masterbuilt Smoker card for Home Assistant.
// A control-panel style card for the masterbuilt_smoker ESPHome integration.
// Off shows a power button; on shows the chamber temperature, target, broil, probes and timer.
// Single file, no dependencies. Add it as a dashboard resource (type: module).

const VERSION = "1.0.0";
const TEMP_STEP = 5;   // degrees per tap on the target
const TIME_STEP = 5;   // minutes per tap on the timer
const PENDING_ACTIONS = new Set(["power", "light", "mode-smoke", "mode-broil", "broil"]);  // spinner + lock

const STYLE = `
  :host { --mb-bg1:#26262a; --mb-bg2:#161617; --mb-panel:#2f2f34; --mb-line:#3c3c42;
          --mb-fire:#ff7518; --mb-fire-dim:#7a3a10; --mb-text:#e9e6e1; --mb-mute:#8a8a92; }
  .smoker { font-family: var(--paper-font-body1_-_font-family, sans-serif);
    color: var(--mb-text);
    background: radial-gradient(120% 120% at 50% 0%, var(--mb-bg1), var(--mb-bg2));
    border: 1px solid var(--mb-line); border-radius: 16px; padding: 16px 18px;
    box-shadow: inset 0 1px 0 rgba(255,255,255,.04); }
  .row { display:flex; align-items:center; }
  .title { font-weight:600; letter-spacing:.06em; text-transform:uppercase; font-size:.8rem; color:var(--mb-mute); }
  ha-icon { --mdc-icon-size:22px; }

  /* OFF view */
  .view-off { display:flex; flex-direction:column; align-items:center; gap:12px; padding:10px 0 6px; }
  .power-big { width:84px; height:84px; border-radius:50%; border:2px solid var(--mb-line);
    background:radial-gradient(80% 80% at 50% 35%, #34343a, #1c1c1f); color:var(--mb-mute);
    cursor:pointer; display:flex; align-items:center; justify-content:center; transition:.25s; }
  .power-big ha-icon { --mdc-icon-size:40px; }
  .power-big:hover { color:var(--mb-fire); border-color:var(--mb-fire-dim); }
  .off-status { font-size:1.5rem; font-weight:700; letter-spacing:.32em; text-transform:uppercase; color:var(--mb-mute); }
  .off-temp { font-size:.78rem; color:var(--mb-mute); opacity:.75; letter-spacing:.02em; }
  .off-temp b { color:var(--mb-text); font-weight:600; }

  /* ON view */
  .view-on { display:flex; flex-direction:column; gap:14px; }
  .header { justify-content:space-between; }
  .indicators { display:flex; gap:6px; align-items:center; }
  .indicators ha-icon { --mdc-icon-size:20px; color:var(--mb-mute); }
  .indicators ha-icon.warn { color:var(--mb-fire); }
  .indicators ha-icon.open { color:#ffd24a; }
  .power-small { background:none; border:none; color:var(--mb-fire); cursor:pointer; display:flex; padding:2px; }
  .power-small ha-icon { --mdc-icon-size:24px; }
  .icon-btn { background:none; border:none; color:var(--mb-mute); cursor:pointer; display:flex; padding:2px; transition:.18s; }
  .icon-btn ha-icon { --mdc-icon-size:22px; }
  .icon-btn.on { color:var(--mb-fire); }

  .readout { text-align:center; padding:2px 0 0; }
  .led { font-family:"DSEG7",ui-monospace,"Courier New",monospace; font-weight:700; line-height:1;
    font-size:3.4rem; color:#5a3a22; letter-spacing:.02em; transition:.4s; }
  .led .unit { font-size:1.1rem; vertical-align:super; margin-left:4px; }
  .led.lit { color:var(--mb-fire); text-shadow:0 0 12px rgba(255,117,24,.55), 0 0 30px rgba(255,117,24,.25); }
  .action-label { margin-top:4px; font-size:.72rem; letter-spacing:.14em; text-transform:uppercase; color:var(--mb-mute); }
  .action-label.heating { color:var(--mb-fire); }

  .target-row { justify-content:center; gap:18px; }
  .target { min-width:120px; text-align:center; font-size:.95rem; color:var(--mb-text); }
  .target small { color:var(--mb-mute); letter-spacing:.1em; }
  .target b { font-size:1.5rem; font-weight:700; margin:0 2px; }
  .target-row[hidden] { display:none; }

  .step { width:42px; height:42px; border-radius:12px; border:1px solid var(--mb-line);
    background:linear-gradient(#3a3a40,#28282c); color:var(--mb-text); cursor:pointer;
    display:flex; align-items:center; justify-content:center; transition:.15s; }
  .step:hover { border-color:var(--mb-fire-dim); color:var(--mb-fire); }
  .step:active { transform:translateY(1px); }

  .seg { display:flex; gap:8px; }
  .seg.center { justify-content:center; }
  .seg-btn { flex:1; padding:9px 8px; border-radius:10px; border:1px solid var(--mb-line);
    background:var(--mb-panel); color:var(--mb-mute); cursor:pointer; font-size:.82rem; font-weight:600;
    letter-spacing:.04em; display:flex; align-items:center; justify-content:center; gap:6px; transition:.18s; }
  .seg-btn:hover { color:var(--mb-text); }
  .seg-btn.active { color:#1a1208; background:linear-gradient(var(--mb-fire),#e85f0c);
    border-color:var(--mb-fire); box-shadow:0 0 14px rgba(255,117,24,.35); }
  [hidden] { display:none !important; }

  .stack { display:flex; flex-direction:column; gap:8px; }
  .line { display:flex; align-items:center; justify-content:space-between; gap:10px; padding:8px 12px;
    border-radius:10px; background:rgba(255,255,255,.025); border:1px solid var(--mb-line); }
  .line .lbl { display:flex; align-items:center; gap:8px; color:var(--mb-text); font-size:.88rem; min-width:0; }
  .line .lbl ha-icon { color:var(--mb-mute); flex:none; }
  .line .lbl small { color:var(--mb-mute); }
  .line .lbl .cur { font-weight:700; font-size:1rem; color:var(--mb-text); margin-left:2px; }
  .line .val { display:flex; align-items:center; gap:10px; flex:none; }
  .mini { width:30px; height:30px; border-radius:8px; }
  .mini ha-icon { --mdc-icon-size:18px; }
  .tgt { color:var(--mb-fire); font-size:.85rem; min-width:42px; text-align:center; }

  /* pending: spinner on the tapped control, lock out further taps until the smoker confirms */
  .busy .seg-btn, .busy .step, .busy .power-big, .busy .power-small { pointer-events:none; }
  .pending { position:relative; pointer-events:none; }
  .pending > * { visibility:hidden; }
  .pending::after { content:""; position:absolute; top:50%; left:50%; width:18px; height:18px;
    margin:-9px 0 0 -9px; border:2px solid rgba(255,255,255,.25); border-top-color:var(--mb-fire);
    border-radius:50%; animation:mb-spin .7s linear infinite; }
  @keyframes mb-spin { to { transform:rotate(360deg); } }
`;

class MasterbuiltSmokerCard extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._built = false;
    this._pending = null;
  }

  setConfig(config) {
    if (!config || !config.climate) {
      throw new Error("masterbuilt-smoker-card: a 'climate' entity is required");
    }
    this._config = Object.assign({ name: "Smoker", probes: [] }, config);
    this._built = false;
    this.shadowRoot.innerHTML = "";
  }

  set hass(hass) {
    this._hass = hass;
    if (!this._built) this._build();
    this._update();
  }

  getCardSize() { return 4; }

  static getStubConfig() { return { name: "Smoker", climate: "" }; }

  // --- state helpers ---
  _stateObj(id) { return id && this._hass && this._hass.states[id] ? this._hass.states[id] : null; }
  _state(id) { const s = this._stateObj(id); return s ? s.state : null; }
  _attr(id, a) { const s = this._stateObj(id); return s ? s.attributes[a] : undefined; }
  _num(id) { const v = parseFloat(this._state(id)); return isNaN(v) ? null : v; }
  _call(domain, service, data) { this._hass.callService(domain, service, data); }

  _isOn() {
    const c = this._config;
    // Master power is the source of truth for on/off when a power switch is configured.
    if (c.power) return this._state(c.power) === "on";
    const mode = this._state(c.climate);
    const broil = c.broil ? this._state(c.broil) : "Off";
    return mode === "heat" || (broil && broil !== "Off");
  }

  _icon(name, cls) {
    return `<ha-icon icon="${name}"${cls ? ` class="${cls}"` : ""}></ha-icon>`;
  }

  _build() {
    const c = this._config;
    const probesHtml = (c.probes || []).map((p, i) => {
      const tgt = p.target
        ? `<div class="val"><button class="step mini" data-action="probe-down" data-index="${i}">${this._icon("mdi:minus")}</button>` +
          `<span class="tgt" data-ref="ptgt${i}">--</span>` +
          `<button class="step mini" data-action="probe-up" data-index="${i}">${this._icon("mdi:plus")}</button></div>`
        : "";
      return `<div class="line">
          <span class="lbl">${this._icon("mdi:thermometer")}${p.name || "Probe " + (i + 1)}</span>
          <span class="val"><span class="cur" data-ref="pcur${i}">--</span>${tgt}</span>
        </div>`;
    }).join("");

    this.shadowRoot.innerHTML = `<style>${STYLE}</style>
      <div class="smoker">
        <div class="view-off">
          <div class="title">${c.name}</div>
          <button class="power-big" data-action="power" title="Turn on">${this._icon("mdi:power")}</button>
          <div class="off-status">Off</div>
          <div class="off-temp">chamber <b data-ref="offtemp">--</b></div>
        </div>
        <div class="view-on">
          <div class="row header">
            <span class="title">${c.name}</span>
            <span class="indicators">
              <ha-icon data-ref="door" hidden></ha-icon>
              <ha-icon data-ref="err" class="warn" icon="mdi:alert" hidden></ha-icon>
              ${c.light ? `<button class="icon-btn" data-action="light" data-ref="lightbtn" title="Light">${this._icon("mdi:lightbulb")}</button>` : ""}
              <button class="power-small" data-action="power" title="Turn off">${this._icon("mdi:power")}</button>
            </span>
          </div>
          <div class="readout">
            <div class="led" data-ref="led">--<span class="unit">&deg;</span></div>
            <div class="action-label" data-ref="action">idle</div>
          </div>
          <div class="row target-row" data-ref="targetrow">
            <button class="step" data-action="temp-down">${this._icon("mdi:chevron-down")}</button>
            <div class="target"><small>SET</small> <b data-ref="target">--</b>&deg;</div>
            <button class="step" data-action="temp-up">${this._icon("mdi:chevron-up")}</button>
          </div>
          ${c.broil ? `<div class="seg" data-ref="moderow">
            <button class="seg-btn" data-action="mode-smoke" data-ref="smokebtn">${this._icon("mdi:grill")} Smoke</button>
            <button class="seg-btn" data-action="mode-broil" data-ref="broilbtn">${this._icon("mdi:fire")} Broil</button>
          </div>
          <div class="seg center" data-ref="levelrow" hidden>
            <button class="seg-btn" data-action="broil" data-level="Low">Low</button>
            <button class="seg-btn" data-action="broil" data-level="Medium">Medium</button>
            <button class="seg-btn" data-action="broil" data-level="High">High</button>
          </div>` : ""}
          <div class="stack">
            ${probesHtml}
            ${c.cook_timer ? `<div class="line">
              <span class="lbl">${this._icon("mdi:timer-outline")}Timer${c.time_remaining ? ` <small data-ref="remain"></small>` : ""}</span>
              <span class="val">
                <button class="step mini" data-action="timer-down">${this._icon("mdi:minus")}</button>
                <span class="tgt" data-ref="timer">--</span>
                <button class="step mini" data-action="timer-up">${this._icon("mdi:plus")}</button>
              </span>
            </div>` : ""}
          </div>
        </div>
      </div>`;

    this._refs = {};
    this.shadowRoot.querySelectorAll("[data-ref]").forEach((el) => { this._refs[el.dataset.ref] = el; });
    this._smokerEl = this.shadowRoot.querySelector(".smoker");
    this._viewOff = this.shadowRoot.querySelector(".view-off");
    this._viewOn = this.shadowRoot.querySelector(".view-on");
    this.shadowRoot.addEventListener("click", (e) => this._onClick(e));
    this._built = true;
  }

  _onClick(e) {
    const el = e.target.closest("[data-action]");
    if (!el || !this._hass || this._pending) return;  // ignore taps while a command is in flight
    const a = el.dataset.action;
    const c = this._config;
    if (a === "power") this._togglePower();
    else if (a === "light") {
      const on = this._state(c.light) === "on";
      this._call("switch", on ? "turn_off" : "turn_on", { entity_id: c.light });
    }
    else if (a === "temp-up") this._adjustTemp(TEMP_STEP);
    else if (a === "temp-down") this._adjustTemp(-TEMP_STEP);
    else if (a === "mode-smoke") {
      // Toggle: tapping Smoke while it's heating stops the heat (back to idle, still powered).
      const smoking = this._state(c.climate) === "heat";
      this._call("climate", "set_hvac_mode", { entity_id: c.climate, hvac_mode: smoking ? "off" : "heat" });
    } else if (a === "mode-broil") {
      // Toggle: tapping Broil while broiling stops it (back to idle).
      const cur = this._state(c.broil);
      const broiling = cur && cur !== "Off";
      this._call("select", "select_option", { entity_id: c.broil, option: broiling ? "Off" : "Low" });
    } else if (a === "broil") {
      this._call("select", "select_option", { entity_id: c.broil, option: el.dataset.level });
    } else if (a === "probe-up" || a === "probe-down") {
      this._adjustProbe(parseInt(el.dataset.index), a === "probe-up" ? TEMP_STEP : -TEMP_STEP);
    } else if (a === "timer-up" || a === "timer-down") {
      this._adjustNumber(c.cook_timer, a === "timer-up" ? TIME_STEP : -TIME_STEP);
    }
    if (PENDING_ACTIONS.has(a)) this._beginPending(a, el);
  }

  // Snapshot the state the action targets, so we can tell when the smoker has acted on it.
  _pendingSnap(a) {
    const c = this._config;
    if (a === "power") return this._state(c.power);
    if (a === "light") return this._state(c.light);
    if (a === "mode-smoke") return this._state(c.climate);
    return this._state(c.broil);
  }

  _beginPending(a, el) {
    this._pending = { key: a, snap: this._pendingSnap(a), until: Date.now() + 6000 };
    el.classList.add("pending");
    if (this._smokerEl) this._smokerEl.classList.add("busy");
    setTimeout(() => this._update(), 6100);
  }

  _clearPending() {
    this._pending = null;
    if (this._smokerEl) this._smokerEl.classList.remove("busy");
    this.shadowRoot.querySelectorAll(".pending").forEach((b) => b.classList.remove("pending"));
  }

  _togglePower() {
    const c = this._config;
    const on = this._isOn();
    if (c.power) {
      // Master power: on just powers the panel up (idle), off shuts everything down.
      this._call("switch", on ? "turn_off" : "turn_on", { entity_id: c.power });
      return;
    }
    // No power switch configured: fall back to driving the climate directly.
    if (on) {
      this._call("climate", "set_hvac_mode", { entity_id: c.climate, hvac_mode: "off" });
      if (c.broil) this._call("select", "select_option", { entity_id: c.broil, option: "Off" });
    } else {
      this._call("climate", "set_hvac_mode", { entity_id: c.climate, hvac_mode: "heat" });
    }
  }

  _clamp(v, id) {
    const mn = this._attr(id, "min_temp") ?? this._attr(id, "min");
    const mx = this._attr(id, "max_temp") ?? this._attr(id, "max");
    if (mn != null) v = Math.max(v, mn);
    if (mx != null) v = Math.min(v, mx);
    return v;
  }

  _adjustTemp(delta) {
    const id = this._config.climate;
    const cur = this._attr(id, "temperature");
    const base = (cur == null || cur <= 0) ? (this._attr(id, "min_temp") || 100) : cur;
    this._call("climate", "set_temperature", { entity_id: id, temperature: this._clamp(base + delta, id) });
  }

  _adjustProbe(i, delta) {
    const p = this._config.probes[i];
    if (!p || !p.target) return;
    this._adjustNumber(p.target, delta);
  }

  _adjustNumber(id, delta) {
    const cur = this._num(id) ?? 0;
    this._call("number", "set_value", { entity_id: id, value: this._clamp(cur + delta, id) });
  }

  // --- render ---
  _update() {
    if (!this._built) return;
    const c = this._config, r = this._refs;
    // Clear the spinner once the smoker confirms the change (targeted state moved) or a timeout.
    if (this._pending && (this._pendingSnap(this._pending.key) !== this._pending.snap || Date.now() > this._pending.until)) {
      this._clearPending();
    }
    const on = this._isOn();
    this._viewOff.hidden = on;
    this._viewOn.hidden = !on;

    const cur = this._attr(c.climate, "current_temperature");
    const heating = this._attr(c.climate, "hvac_action") === "heating";
    const broil = c.broil ? this._state(c.broil) : "Off";
    const broiling = broil && broil !== "Off";

    if (!on) {
      r.offtemp.textContent = cur != null ? Math.round(cur) + "°" : "--";
      return;
    }

    // LED readout. Glow only when actually cooking (heating/holding/broiling), not just powered,
    // so "on but idle" never looks like it's heating.
    const smoking = this._state(c.climate) === "heat";
    r.led.innerHTML = (cur != null ? Math.round(cur) : "--") + `<span class="unit">&deg;</span>`;
    r.led.classList.toggle("lit", smoking || broiling);
    r.action.textContent = heating ? "heating"
      : broiling ? "broiling · " + broil.toLowerCase()
      : smoking ? "holding" : "ready";
    r.action.classList.toggle("heating", heating || broiling);

    // target (hidden while broiling - broil has no temperature)
    const tgt = this._attr(c.climate, "temperature");
    r.targetrow.hidden = broiling;
    if (r.target) r.target.textContent = (tgt != null && tgt > 0) ? Math.round(tgt) : "--";

    // mode + level
    if (c.broil) {
      r.smokebtn.classList.toggle("active", !broiling && this._state(c.climate) === "heat");
      r.broilbtn.classList.toggle("active", broiling);
      r.levelrow.hidden = !broiling;
      r.levelrow.querySelectorAll(".seg-btn").forEach((b) => {
        b.classList.toggle("active", b.dataset.level === broil);
      });
    }

    // light
    if (c.light && r.lightbtn) {
      r.lightbtn.classList.toggle("on", this._state(c.light) === "on");
    }

    // probes
    (c.probes || []).forEach((p, i) => {
      const v = this._num(p.sensor);
      if (r["pcur" + i]) r["pcur" + i].textContent = v != null ? Math.round(v) + "°" : "--";
      if (p.target && r["ptgt" + i]) {
        const t = this._num(p.target);
        r["ptgt" + i].textContent = (t != null && t > 0) ? Math.round(t) + "°" : "off";
      }
    });

    // timer
    if (c.cook_timer && r.timer) {
      const t = this._num(c.cook_timer);
      r.timer.textContent = (t != null && t > 0) ? Math.round(t) + "m" : "off";
      if (c.time_remaining && r.remain) {
        const rem = this._num(c.time_remaining);
        r.remain.textContent = (rem != null && rem > 0) ? Math.round(rem) + "m left" : "";
      }
    }

    // indicators
    if (c.door && r.door) {
      const open = this._state(c.door) === "on";
      r.door.hidden = false;
      r.door.setAttribute("icon", open ? "mdi:door-open" : "mdi:door");
      r.door.classList.toggle("open", open);
      r.door.title = open ? "Door open" : "Door closed";
    }
    if (c.temperature_error && r.err) {
      r.err.hidden = this._state(c.temperature_error) !== "on";
    }
  }
}

customElements.define("masterbuilt-smoker-card", MasterbuiltSmokerCard);
window.customCards = window.customCards || [];
window.customCards.push({
  type: "masterbuilt-smoker-card",
  name: "Masterbuilt Smoker",
  description: "Control panel for a Masterbuilt Bluetooth smoker",
  preview: false,
});
console.info(`%c MASTERBUILT-SMOKER-CARD %c v${VERSION} `, "background:#ff7518;color:#1a1208;font-weight:700;border-radius:3px 0 0 3px;padding:2px 6px", "background:#26262a;color:#ff7518;border-radius:0 3px 3px 0;padding:2px 6px");
