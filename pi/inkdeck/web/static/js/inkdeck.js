// InkDeck for Raspberry Pi web interface.

const $ = (id) => document.getElementById(id);

function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

async function api(url, options = {}) {
    const init = { ...options };
    if (init.json !== undefined) {
        init.method = init.method || 'POST';
        init.headers = { 'Content-Type': 'application/json' };
        init.body = JSON.stringify(init.json);
        delete init.json;
    }
    const response = await fetch(url, init);
    const data = await response.json().catch(() => ({}));
    if (!response.ok || data.error) throw new Error(data.error || `${response.status} ${response.statusText}`);
    return data;
}

function setStatus(id, text, isError = false) {
    const el = $(id);
    if (!el) return;
    el.textContent = text;
    el.classList.toggle('error', isError);
}

// ---- Tabs ----

const tabLoaders = {
    dashboard: refreshStats,
    ereader: loadProgress,
    todo: loadTodos,
    network: refreshNetwork,
    klipper: refreshKlipper,
    settings: refreshBluetooth,
    logs: loadLogs,
};

function showTab(name) {
    if (!$(name)) name = 'dashboard';
    document.querySelectorAll('.tab-content').forEach((s) => s.classList.toggle('active', s.id === name));
    document.querySelectorAll('.nav-links button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
    if (location.hash !== `#${name}`) history.replaceState(null, '', `#${name}`);
    (tabLoaders[name] || (() => {}))();
}

// ---- Dashboard ----

function batteryLevel(percent) {
    return [100, 80, 60, 40, 20, 0].find((step) => percent >= step - 10) ?? 0;
}

async function refreshStats() {
    let s;
    try { s = await api('/api/system_stats'); } catch (e) { return; }
    const set = (id, text, cls) => { const el = $(id); if (el) { el.textContent = text ?? 'N/A'; el.className = cls || ''; } };
    const temp = parseFloat(s.cpu_temp);
    set('stat-temp', s.cpu_temp, temp >= 70 ? 'bad' : temp >= 60 ? 'warn' : '');
    set('stat-uptime', s.uptime);
    set('stat-disk', s.disk_free ? `${s.disk_free} of ${s.disk_total}` : 'N/A');
    set('stat-memory', s.memory_used ? `${s.memory_used} of ${s.memory_total}` : 'N/A',
        s.memory_percent >= 85 ? 'bad' : s.memory_percent >= 70 ? 'warn' : '');
    set('stat-cpu', s.cpu_speed);
    set('stat-ip', s.ip);
    set('stat-wifi', s.wifi_status, s.wifi_status === 'On' ? 'good' : '');
    set('stat-bluetooth', s.bluetooth_status);
    set('stat-voltage', s.cpu_voltage);
    set('stat-undervolt', s.undervolt ? `${s.undervolt} (−${Math.abs(s.undervolt) * 25} mV)` : 'None');
    set('stat-throttle', s.throttle_status === 'OK' ? 'None' : s.throttle_status, s.throttle_status === 'OK' ? 'good' : 'bad');
    set('stat-cores', `${s.active_cores} of ${s.total_cores}`);
    set('stat-os', s.os_name);
    const b = s.battery;
    const text = b ? `${b.percentage}%${b.is_charging ? ' charging' : ''}` : 'No battery';
    set('stat-battery', text, b && b.is_low && !b.is_charging ? 'bad' : '');
    $('header-battery').textContent = b ? `${b.percentage}%` : '--';
    $('battery-icon').dataset.level = b ? batteryLevel(b.percentage) : 100;
    $('battery-icon').classList.toggle('charging', !!(b && b.is_charging));
}

// ---- Remote control ----

async function remote(command) {
    setStatus('remote-status', 'Sending…');
    try {
        const data = await api(`/remote/${command}`, { method: 'POST' });
        setStatus('remote-status', `Showing ${data.screen.replace('_', ' ')}`);
    } catch (e) {
        setStatus('remote-status', e.message === 'Failed to fetch' ? 'No reply: Wi-Fi turns off while a book is open.' : e.message, true);
    }
}

// ---- Books ----

function renderBooks(books) {
    $('book-count').textContent = `${books.length} on the device`;
    $('book-list').innerHTML = books.length ? books.map((b) =>
        `<div class="book-item"><span class="book-title">${escapeHtml(b.title)}</span><span class="book-size">${escapeHtml(b.size)}</span>` +
        `<button class="btn-delete" data-file="${escapeHtml(b.filename)}">Delete</button></div>`).join('')
        : '<p class="hint">No books yet.</p>';
}

async function refreshBooks() {
    try { renderBooks((await api('/api/books')).books); } catch (e) { /* keep the list */ }
}

function uploadBooks() {
    const files = $('book-file').files;
    if (!files.length) { setStatus('upload-status', 'Choose one or more EPUB files first.', true); return; }
    const form = new FormData();
    for (const file of files) form.append('file', file);
    const xhr = new XMLHttpRequest();
    $('upload-button').disabled = true;
    $('upload-progress').classList.remove('hidden');
    xhr.upload.onprogress = (e) => { if (e.lengthComputable) $('upload-bar').style.width = `${e.loaded * 100 / e.total}%`; };
    xhr.onloadend = () => {
        $('upload-button').disabled = false;
        $('upload-progress').classList.add('hidden');
        $('upload-bar').style.width = '0';
        let data = {};
        try { data = JSON.parse(xhr.responseText); } catch (e) { /* not JSON */ }
        if (xhr.status === 200 && data.success) {
            setStatus('upload-status', `Uploaded ${data.count} book${data.count === 1 ? '' : 's'}.`);
            $('book-file').value = '';
            refreshBooks();
        } else {
            setStatus('upload-status', data.error || 'Upload failed.', true);
        }
    };
    setStatus('upload-status', 'Uploading…');
    xhr.open('POST', '/upload');
    xhr.send(form);
}

async function deleteBook(filename) {
    if (!confirm(`Delete “${filename}”?`)) return;
    try {
        await api(`/delete/${encodeURIComponent(filename)}`, { method: 'POST' });
        refreshBooks();
        loadProgress();
    } catch (e) { alert(`Could not delete: ${e.message}`); }
}

async function loadProgress() {
    const box = $('progress-list');
    try {
        const { progress } = await api('/api/progress/list');
        box.innerHTML = progress.length ? '<div class="table-scroll"><table class="data"><thead><tr><th>Book</th><th>Position</th><th></th></tr></thead><tbody>' +
            progress.map((p) => `<tr><td>${escapeHtml(p.filename.replace(/\.epub$/i, '').replace(/_/g, ' '))}</td>` +
                `<td class="mono">${p.current_page} / ${p.total_pages} (${Math.round(p.current_page * 100 / p.total_pages)}%)</td>` +
                `<td><button class="btn-delete" data-reset="${escapeHtml(p.path)}">Reset</button></td></tr>`).join('') +
            '</tbody></table></div>' : '<p class="hint">No saved positions yet.</p>';
    } catch (e) { box.innerHTML = `<p class="error">${escapeHtml(e.message)}</p>`; }
}

async function resetProgress(path) {
    if (!confirm(path === '__all__' ? 'Reset every saved reading position?' : 'Reset the reading position for this book?')) return;
    try { await api('/api/progress/reset', { json: { path } }); } catch (e) { alert(e.message); }
    loadProgress();
}

// ---- Settings forms ----

async function saveSettings(form) {
    const data = {};
    form.querySelectorAll('input[name], select[name]').forEach((el) => {
        data[el.name] = el.type === 'checkbox' ? el.checked : el.type === 'number' ? Number(el.value) : el.value;
    });
    const statusId = form.dataset.status;
    setStatus(statusId, 'Saving…');
    try {
        const result = await api('/save_settings', { json: data });
        const warning = result.undervolt_warning || result.boot_cores_warning;
        setStatus(statusId, warning || 'Saved.', !!warning);
        for (const [key, value] of Object.entries(result.settings)) {
            const el = form.querySelector(`[name="${key}"]`);
            if (el && el.type === 'checkbox') el.checked = value; else if (el) el.value = value;
        }
    } catch (e) { setStatus(statusId, e.message, true); }
}

async function rebootDevice() {
    if (!confirm('Reboot the Pi now? The web interface returns in about a minute.')) return;
    try { await api('/api/reboot', { method: 'POST' }); } catch (e) { /* the server is going down */ }
    setStatus('cpu-status', 'Rebooting…');
}

// ---- To Do ----

async function loadTodos() {
    const box = $('todo-list');
    try {
        const { tasks } = await api('/api/todos');
        box.innerHTML = tasks.length ? tasks.map((t) =>
            `<div class="todo-item${t.completed ? ' completed' : ''}" data-id="${escapeHtml(t.id)}">` +
            `<input type="checkbox" class="todo-checkbox" ${t.completed ? 'checked' : ''} aria-label="Done">` +
            `<span class="todo-text">${escapeHtml(t.text)}</span>` +
            `<div class="todo-actions"><button class="btn-edit">Edit</button><button class="btn-delete">Delete</button></div></div>`).join('')
            : '<p class="hint">No tasks yet.</p>';
    } catch (e) { box.innerHTML = `<p class="error">${escapeHtml(e.message)}</p>`; }
}

async function addTodo() {
    const input = $('todo-input');
    const text = input.value.trim();
    if (!text) return;
    try { await api('/api/todos', { json: { text } }); input.value = ''; loadTodos(); } catch (e) { alert(e.message); }
}

async function todoAction(event) {
    const item = event.target.closest('.todo-item');
    if (!item) return;
    const id = encodeURIComponent(item.dataset.id);
    try {
        if (event.target.classList.contains('todo-checkbox')) {
            await api(`/api/todos/${id}`, { method: 'PUT' });
        } else if (event.target.classList.contains('btn-delete')) {
            if (!confirm('Delete this task?')) return;
            await api(`/api/todos/${id}`, { method: 'DELETE' });
        } else if (event.target.classList.contains('btn-edit')) {
            const current = item.querySelector('.todo-text').textContent;
            const text = prompt('Edit task', current);
            if (text === null || !text.trim() || text === current) return;
            await api(`/api/todos/${id}`, { method: 'PATCH', json: { text: text.trim() } });
        } else {
            return;
        }
    } catch (e) { alert(e.message); }
    loadTodos();
}

// ---- Network scanner and Klipper ----

const pollers = {};

function poll(name, fn, ms) {
    clearTimeout(pollers[name]);
    pollers[name] = setTimeout(fn, ms);
}

function showProgress(prefix, active, percent) {
    $(`${prefix}-progress`).classList.toggle('hidden', !active);
    $(`${prefix}-bar`).style.width = `${percent || 0}%`;
}

async function refreshNetwork() {
    let data;
    try { data = await api('/api/ipscanner/status'); } catch (e) { return; }
    $('local-ip').textContent = `This Pi: ${data.local_ip}`;
    $('device-count').textContent = data.devices.length;
    $('network-scan-button').disabled = data.scanning;
    $('network-scan-button').textContent = data.scanning ? 'Scanning…' : 'Scan';
    showProgress('network', data.scanning, data.progress);
    if (data.devices.length) {
        $('device-list').innerHTML = '<div class="table-scroll"><table class="data"><thead><tr><th>Address</th><th>Name</th><th>MAC</th><th>Vendor</th><th>Web</th></tr></thead><tbody>' +
            data.devices.map((d) => `<tr><td class="mono">${escapeHtml(d.ip)}</td><td>${escapeHtml(d.hostname || '–')}</td>` +
                `<td class="mono">${escapeHtml(d.mac)}</td><td>${escapeHtml(d.name)}</td>` +
                `<td>${d.http ? `<a class="good" href="http://${escapeHtml(d.ip)}" target="_blank" rel="noopener">Open ↗</a>` : '–'}</td></tr>`).join('') +
            '</tbody></table></div>';
    } else if (data.scanning) {
        $('device-list').innerHTML = '<p class="hint">Scanning…</p>';
    }
    if (data.scanning) poll('network', refreshNetwork, 1000);
}

async function startNetworkScan() {
    try { await api('/api/ipscanner/scan', { method: 'POST' }); } catch (e) { alert(e.message); }
    refreshNetwork();
}

function temperature(now, target) {
    return now === null || now === undefined ? '–' : `${now.toFixed(0)} / ${(target || 0).toFixed(0)} °C`;
}

async function refreshKlipper() {
    let data;
    try { data = await api('/api/klipper/printers'); } catch (e) { return; }
    $('printer-count').textContent = data.printers.length;
    $('klipper-scan-button').disabled = data.scanning;
    $('klipper-scan-button').textContent = data.scanning ? 'Scanning…' : 'Scan';
    $('klipper-status').textContent = data.scanning ? 'Looking for Moonraker on port 7125…' : '';
    showProgress('klipper', data.scanning, data.progress);
    if (data.printers.length) {
        $('printer-list').innerHTML = data.printers.map((p) => {
            const printing = (p.state === 'printing' || p.state === 'paused') && p.progress !== null;
            const host = escapeHtml(p.ip);
            return `<article class="printer"><header><h3>${escapeHtml(p.hostname)}</h3><span class="state ${escapeHtml(p.state)}">${escapeHtml(p.state)}</span></header>` +
                `<dl><dt>Address</dt><dd class="mono">${host}</dd><dt>Nozzle</dt><dd>${temperature(p.extruder_temp, p.extruder_target)}</dd>` +
                `<dt>Bed</dt><dd>${temperature(p.bed_temp, p.bed_target)}</dd>` +
                (printing ? `<dt>Progress</dt><dd>${(p.progress * 100).toFixed(0)}%</dd>` : '') +
                (p.filename ? `<dt>File</dt><dd>${escapeHtml(p.filename.split('/').pop())}</dd>` : '') +
                `</dl><a class="btn secondary" href="http://${host}" target="_blank" rel="noopener">Open Mainsail ↗</a></article>`;
        }).join('');
    } else {
        $('printer-list').innerHTML = `<p class="hint">${data.scanning ? 'Scanning…' : 'No printers found yet. Scan to search the network.'}</p>`;
    }
    if (data.scanning) poll('klipper', refreshKlipper, 1500);
}

async function startKlipperScan() {
    try { await api('/api/klipper/scan', { method: 'POST' }); } catch (e) { alert(e.message); }
    refreshKlipper();
}

// ---- Bluetooth ----

let bluetoothScanning = false;
let pairingMac = null;

function deviceRow(d, button) {
    return `<li><span><strong>${escapeHtml(d.name)}</strong><small>${escapeHtml(d.mac)}</small></span>${button}</li>`;
}

async function refreshBluetooth() {
    let data;
    try { data = await api('/api/bluetooth/status'); } catch (e) { return; }
    const toggle = $('bluetooth-power');
    toggle.checked = data.powered;
    toggle.disabled = !data.available;
    if (!data.available) $('bluetooth-note').textContent = 'Bluetooth is turned off in the Pi’s boot configuration (dtoverlay=disable-bt), which saves power.';
    $('bluetooth-controls').classList.toggle('hidden', !data.powered);
    $('paired-devices').innerHTML = data.paired_devices.length
        ? data.paired_devices.map((d) => deviceRow(d, `<button class="btn-delete" data-remove="${escapeHtml(d.mac)}">Remove</button>`)).join('')
        : '<li class="hint">No paired devices.</li>';
}

async function setBluetoothPower(on) {
    try { await api('/api/bluetooth/power', { json: { power: on } }); } catch (e) { setStatus('bluetooth-status', e.message, true); }
    refreshBluetooth();
}

async function toggleBluetoothScan() {
    bluetoothScanning = !bluetoothScanning;
    $('bluetooth-scan-button').textContent = bluetoothScanning ? 'Stop scanning' : 'Scan for devices';
    $('bluetooth-scan').classList.toggle('hidden', !bluetoothScanning);
    try { await api('/api/bluetooth/scan', { json: { scan: bluetoothScanning } }); } catch (e) { setStatus('bluetooth-status', e.message, true); }
    if (bluetoothScanning) pollBluetooth();
}

async function pollBluetooth() {
    if (!bluetoothScanning) return;
    try {
        const { devices } = await api('/api/bluetooth/devices');
        $('found-devices').innerHTML = devices.filter((d) => !d.paired).sort((a, b) => a.name.localeCompare(b.name))
            .map((d) => deviceRow(d, `<button class="btn-edit" data-pair="${escapeHtml(d.mac)}" data-name="${escapeHtml(d.name)}">Pair</button>`)).join('')
            || '<li class="hint">Searching…</li>';
    } catch (e) { /* keep polling */ }
    poll('bluetooth', pollBluetooth, 2000);
}

async function pairDevice(mac, name, pin = '') {
    pairingMac = mac;
    if (bluetoothScanning) toggleBluetoothScan();
    setStatus('bluetooth-status', `Pairing with ${name}…`);
    try {
        const result = await api('/api/bluetooth/pair', { json: { mac, pin } });
        if (result.status === 'passkey_required') {
            openPinModal(name, result.message, result.passkey);
        } else if (result.success) {
            closePinModal();
            setStatus('bluetooth-status', `Paired with ${name}.`);
        } else {
            setStatus('bluetooth-status', result.error, true);
            openPinModal(name, `${result.error} You can also try a PIN.`);
        }
    } catch (e) { setStatus('bluetooth-status', e.message, true); }
    refreshBluetooth();
}

function openPinModal(name, message, passkey) {
    $('pin-title').textContent = `Pair ${name}`;
    $('pin-message').textContent = message;
    $('pin-passkey').textContent = passkey || '';
    $('pin-passkey').classList.toggle('hidden', !passkey);
    $('pin-input').classList.toggle('hidden', !!passkey);
    $('pin-submit').classList.toggle('hidden', !!passkey);
    $('pin-modal').dataset.name = name;
    $('pin-modal').classList.remove('hidden');
}

function closePinModal() { $('pin-modal').classList.add('hidden'); $('pin-input').value = ''; }

function submitPin() {
    const pin = $('pin-input').value.trim();
    if (pin) pairDevice(pairingMac, $('pin-modal').dataset.name, pin);
}

async function removeDevice(mac) {
    if (!confirm('Remove this device?')) return;
    try { await api('/api/bluetooth/remove', { json: { mac } }); } catch (e) { alert(e.message); }
    refreshBluetooth();
}

// ---- Terminal and logs ----

function appendConsole(text, cls) {
    const out = $('terminal-output');
    const span = document.createElement('span');
    if (cls) span.className = cls;
    span.textContent = text;
    out.appendChild(span);
    out.scrollTop = out.scrollHeight;
}

async function runCommand() {
    const input = $('terminal-input');
    const command = input.value.trim();
    if (!command) return;
    input.value = '';
    appendConsole(`\n$ ${command}\n`, 'cmd');
    try {
        const response = await fetch('/terminal/execute', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ command }) });
        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        let buffer = '';
        for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            buffer += decoder.decode(value, { stream: true });
            let end;
            while ((end = buffer.indexOf('\n\n')) !== -1) {
                const event = buffer.slice(0, end);
                buffer = buffer.slice(end + 2);
                if (!event.startsWith('data: ')) continue;
                const data = JSON.parse(event.slice(6));
                if (data.stdout !== undefined) appendConsole(data.stdout);
                else if (data.error) appendConsole(`${data.error}\n`, 'err');
                else if (data.returncode) appendConsole(`Exit code ${data.returncode}\n`, 'err');
            }
        }
    } catch (e) { appendConsole(`${e.message}\n`, 'err'); }
}

function clearTerminal() { $('terminal-output').textContent = ''; }

let logType = 'app';

async function loadLogs() {
    const out = $('log-output');
    try {
        const data = await api(`/api/logs/${logType}`);
        out.textContent = data.logs;
        out.scrollTop = out.scrollHeight;
    } catch (e) { out.textContent = e.message; }
}

// ---- Wiring ----

document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('.nav-links button').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));
    document.querySelectorAll('.settings-form').forEach((form) => form.addEventListener('submit', (e) => { e.preventDefault(); saveSettings(form); }));
    $('book-list').addEventListener('click', (e) => { if (e.target.dataset.file) deleteBook(e.target.dataset.file); });
    $('progress-list').addEventListener('click', (e) => { if (e.target.dataset.reset) resetProgress(e.target.dataset.reset); });
    $('todo-list').addEventListener('click', todoAction);
    $('todo-input').addEventListener('keydown', (e) => { if (e.key === 'Enter') addTodo(); });
    $('terminal-input').addEventListener('keydown', (e) => { if (e.key === 'Enter') runCommand(); });
    document.querySelectorAll('.chip').forEach((c) => c.addEventListener('click', () => { $('terminal-input').value = c.dataset.command; $('terminal-input').focus(); }));
    document.querySelectorAll('.segmented button').forEach((b) => b.addEventListener('click', () => {
        logType = b.dataset.log;
        document.querySelectorAll('.segmented button').forEach((x) => x.classList.toggle('active', x === b));
        loadLogs();
    }));
    $('bluetooth-power').addEventListener('change', (e) => setBluetoothPower(e.target.checked));
    $('paired-devices').addEventListener('click', (e) => { if (e.target.dataset.remove) removeDevice(e.target.dataset.remove); });
    $('found-devices').addEventListener('click', (e) => { if (e.target.dataset.pair) pairDevice(e.target.dataset.pair, e.target.dataset.name); });
    $('pisugar-link').href = `http://${location.hostname}:8421`;
    showTab(location.hash.slice(1) || 'dashboard');
    if (location.hash.slice(1) !== 'dashboard') refreshStats();
    setInterval(() => { if (document.visibilityState === 'visible') refreshStats(); }, 60000);
});
