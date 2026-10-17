// Host test for data/pages.js: the ZIP writer and reader used to build page
// books (CBZ) in the browser, and the page order. Run: node test/page_books.cjs
// With an output path it also writes a sample CBZ for checking elsewhere.
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const PageBooks = require('../data/pages.js');

// CRC-32 of "123456789" is the standard check value.
assert.strictEqual(PageBooks.crc32(new TextEncoder().encode('123456789')), 0xCBF43926);

const files = [
    { name: '0001.jpg', data: new Uint8Array([0xFF, 0xD8, 1, 2, 3, 0xFF, 0xD9]) },
    { name: '0002.jpg', data: new Uint8Array(70000).map((_, i) => i & 255) },
    { name: '0003.jpg', data: new Uint8Array([]) },
];
const parts = PageBooks.zipStore(files);
const bytes = Buffer.concat(parts.map((part) => Buffer.from(part instanceof ArrayBuffer ? new Uint8Array(part) : part)));
const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length);
const entries = PageBooks.zipEntries(buffer);
assert.deepStrictEqual(entries.map((e) => e.name), files.map((f) => f.name));
entries.forEach((entry, i) => {
    assert.strictEqual(entry.method, 0);
    assert.strictEqual(entry.size, files[i].data.length);
    assert.deepStrictEqual(Array.from(entry.data), Array.from(files[i].data));
});

const names = ['p10.jpg', 'p2.jpg', 'P1.jpg', 'p02b.jpg'];
assert.deepStrictEqual(names.sort(PageBooks.naturalCompare), ['P1.jpg', 'p2.jpg', 'p02b.jpg', 'p10.jpg']);
assert.ok(PageBooks.isPageBook('Book.PDF') && PageBooks.isPageBook('a.cbz') && !PageBooks.isPageBook('a.epub'));
assert.throws(() => PageBooks.zipEntries(new ArrayBuffer(100)), /not a ZIP/);

if (process.argv[2]) fs.writeFileSync(path.resolve(process.argv[2]), bytes);
console.log('page_books: ok');
