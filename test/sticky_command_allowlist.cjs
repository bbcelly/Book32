// The Sticky's fast-drive code may only send reviewed SSD1677 commands. The
// firmware already refuses others at compile time (gameCommand<> static_assert);
// this test also guards the allowlist itself and rejects raw command writes.
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const source = fs.readFileSync(path.join(__dirname, '../lib/Book32_Core/StickyDisplay.cpp'), 'utf8');

const ALLOWED = new Set([0x11, 0x20, 0x21, 0x22, 0x24, 0x26, 0x32, 0x3C, 0x44, 0x45, 0x4E, 0x4F]);
const UPDATES = new Set([0x0C, 0xCC, 0x03]);
// One-time-programmable memory: never part of the driver.
const OTP = [0x2A, 0x2B, 0x36, 0x37, 0x38];

const allowlist = source.match(/constexpr bool gameCommandAllowed\(uint8_t command\) \{([\s\S]*?)\n\}/);
assert(allowlist, 'gameCommandAllowed() not found');
const listed = new Set([...allowlist[1].matchAll(/command == (0x[0-9A-Fa-f]{2})/g)].map(m => parseInt(m[1], 16)));
assert.deepStrictEqual([...listed].sort(), [...ALLOWED].sort(), 'allowlist changed: review it here too');

const updates = source.match(/constexpr bool gameUpdateAllowed\(uint8_t value\) \{([\s\S]*?)\n\}/);
assert(updates, 'gameUpdateAllowed() not found');
const listedUpdates = new Set([...updates[1].matchAll(/value == (0x[0-9A-Fa-f]{2})/g)].map(m => parseInt(m[1], 16)));
assert.deepStrictEqual([...listedUpdates].sort(), [...UPDATES].sort(), 'update sequences changed');

let uses = 0;
for (const m of source.matchAll(/game(?:Command|CommandByte)<(0x[0-9A-Fa-f]{2})>/g)) {
  assert(ALLOWED.has(parseInt(m[1], 16)), `command ${m[1]} is not allowed`);
  ++uses;
}
for (const m of source.matchAll(/gameUpdate<(0x[0-9A-Fa-f]{2})>/g)) {
  assert(UPDATES.has(parseInt(m[1], 16)), `update ${m[1]} is not allowed`);
  ++uses;
}
assert(uses > 10, 'expected the driver to use the checked command helpers');

// gameSend is the only raw sender and is called only from gameCommand<>.
const rawSends = [...source.matchAll(/gameSend\(/g)].length;
assert.strictEqual(rawSends, 2, 'gameSend must only be defined and called once (from gameCommand<>)');

for (const command of OTP) {
  const hex = '0x' + command.toString(16).toUpperCase().padStart(2, '0');
  assert(!new RegExp(`<${hex}>`, 'i').test(source), `OTP command ${hex} must not be sent`);
}

console.log(`Sticky command allowlist PASS: ${uses} checked command uses, no OTP commands`);
