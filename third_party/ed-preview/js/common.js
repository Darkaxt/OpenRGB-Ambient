'use strict';

// ── API call wrapper ──────────────────────────────────────────────────────────

async function api(funcName, ...args) {
    try {
        if (typeof eel === 'undefined') {
            console.error('[API] Eel not loaded');
            return { success: false, error: 'Eel not initialized' };
        }
        const result = await eel[funcName](...args)();
        return result || {};
    } catch (error) {
        console.error(`[API] Error calling ${funcName}:`, error);
        window.dispatchEvent(new CustomEvent('api-error', { detail: { funcName, error } }));
        return { success: false, error: error.message || 'Connection error' };
    }
}

// True when an api() result is not a failure (backend returns {ok:false} or {success:false})
function apiOk(res) {
    return !!res && res.ok !== false && res.success !== false;
}

// ── Notifications ────────────────────────────────────────────────────────────

function showNotification(message, type = 'info', duration = 4000) {
    const colors = { success: '#10b981', error: '#ef4444', warning: '#f59e0b', info: '#14F5FF' };
    const icons  = { success: 'check_circle', error: 'error', warning: 'warning', info: 'info' };

    let container = document.getElementById('toast-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'toast-container';
        Object.assign(container.style, {
            position: 'fixed', top: '24px', right: '24px',
            zIndex: '10000', display: 'flex', flexDirection: 'column', gap: '12px',
            pointerEvents: 'none',
        });
        document.body.appendChild(container);
    }

    const toast = document.createElement('div');
    toast.innerHTML = `
        <div style="display:flex;align-items:center;gap:12px;
            background:rgba(0,0,0,0.95);border:1px solid ${colors[type] || colors.info};
            border-radius:8px;padding:12px 16px;min-width:280px;max-width:400px;
            box-shadow:0 10px 40px rgba(0,0,0,0.5);pointer-events:auto;
            animation:slideInRight 0.3s ease-out;">
            <span class="material-symbols-outlined" style="color:${colors[type]||colors.info};font-size:24px;">
                ${icons[type] || 'info'}
            </span>
            <span style="color:white;flex:1;font-size:14px;">${escapeHtml(message)}</span>
            <button onclick="this.parentElement.parentElement.remove()"
                style="background:none;border:none;color:rgba(255,255,255,0.5);cursor:pointer;font-size:20px;">×</button>
        </div>`;
    container.appendChild(toast);

    if (duration > 0) {
        setTimeout(() => {
            toast.style.animation = 'slideOutRight 0.3s ease-in';
            setTimeout(() => toast.remove(), 300);
        }, duration);
    }
}

// ── Confirm dialog (replaces window.confirm, matches the design system) ──────

function confirmDialog(message, okLabel = 'Confirm', { danger = false } = {}) {
    return new Promise(resolve => {
        const prevFocus = document.activeElement;
        const overlay = document.createElement('div');
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        Object.assign(overlay.style, {
            position: 'fixed', inset: '0', zIndex: '10001', display: 'flex',
            alignItems: 'center', justifyContent: 'center', background: 'rgba(0,0,0,.6)',
            backdropFilter: 'blur(4px)',
        });
        const accent = danger ? '255,90,90' : '20,245,255';
        overlay.innerHTML = `
            <div style="width:min(420px,calc(100vw - 40px));padding:22px 24px;border-radius:14px;
                        background:linear-gradient(160deg,rgba(10,24,36,.98),rgba(4,12,20,.99));
                        border:1px solid rgba(${accent},.35);box-shadow:0 20px 60px rgba(0,0,0,.6);">
                <div style="font-size:14px;line-height:1.55;color:#fff;margin-bottom:18px;">${escapeHtml(message)}</div>
                <div style="display:flex;gap:8px;justify-content:flex-end;">
                    <button type="button" data-act="cancel" class="btn-secondary">Cancel</button>
                    <button type="button" data-act="ok" class="btn"
                            style="${danger ? 'border-color:rgba(255,90,90,.6);color:#ff8a8a;' : ''}">${escapeHtml(okLabel)}</button>
                </div>
            </div>`;
        const done = result => {
            document.removeEventListener('keydown', onKey, true);
            overlay.remove();
            if (prevFocus && prevFocus.focus) prevFocus.focus();
            resolve(result);
        };
        const onKey = e => {
            if (e.key === 'Escape') { e.preventDefault(); done(false); }
            else if (e.key === 'Enter' && e.target.dataset.act !== 'cancel') { e.preventDefault(); done(true); }
        };
        overlay.addEventListener('click', e => {
            if (e.target === overlay) done(false);
            else if (e.target.dataset && e.target.dataset.act) done(e.target.dataset.act === 'ok');
        });
        document.addEventListener('keydown', onKey, true);
        document.body.appendChild(overlay);
        overlay.querySelector('[data-act="ok"]').focus();
    });
}

// ── Utilities ─────────────────────────────────────────────────────────────────

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function escapeAttr(text) {
    return String(text ?? '')
        .replace(/&/g, '&amp;').replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function hexToRgb(hex) {
    hex = hex.replace(/^#/, '');
    const r = /^([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return r ? [parseInt(r[1],16), parseInt(r[2],16), parseInt(r[3],16)] : null;
}

function rgbToHex(r, g, b) {
    r = Math.max(0, Math.min(255, Math.round(r)));
    g = Math.max(0, Math.min(255, Math.round(g)));
    b = Math.max(0, Math.min(255, Math.round(b)));
    return '#' + ((1<<24)+(r<<16)+(g<<8)+b).toString(16).slice(1).toUpperCase();
}

function debounce(func, wait) {
    let t;
    return function(...args) {
        clearTimeout(t);
        t = setTimeout(() => func(...args), wait);
    };
}

function throttle(func, limit) {
    let inThrottle;
    return function(...args) {
        if (!inThrottle) { func(...args); inThrottle = true; setTimeout(() => inThrottle = false, limit); }
    };
}

// ── Toast animations CSS ──────────────────────────────────────────────────────

if (!document.getElementById('toast-animations')) {
    const s = document.createElement('style');
    s.id = 'toast-animations';
    s.textContent = `
        @keyframes slideInRight  { from { transform:translateX(400px);opacity:0; } to { transform:translateX(0);opacity:1; } }
        @keyframes slideOutRight { from { transform:translateX(0);opacity:1; }     to { transform:translateX(400px);opacity:0; } }
    `;
    document.head.appendChild(s);
}

// ── Cancel shutdown on page connect ──────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
    if (typeof eel !== 'undefined') {
        try { eel.cancel_shutdown()(); } catch(_) {}
    }
});

console.log('[Common] EDAmbilight utilities loaded');
