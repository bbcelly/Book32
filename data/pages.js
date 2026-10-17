// Page books for the eReader: PDFs and comic archives (CBZ/ZIP) are turned
// into a CBZ of grayscale baseline JPEGs in the browser before upload, sized
// for the device (twice its screen, so zooming in stays sharp). The device
// then only ever decodes simple JPEGs. PDFs are drawn with pdf.js, which is
// stored on the device (/pdfjs) so this works without internet.

const PageBooks = (() => {
    const IMAGE_TYPES = { jpg: 'image/jpeg', jpeg: 'image/jpeg', png: 'image/png', webp: 'image/webp',
                          gif: 'image/gif', bmp: 'image/bmp', avif: 'image/avif' };
    const JPEG_QUALITY = 0.78;

    // ---- ZIP ----

    const CRC_TABLE = (() => {
        const table = new Uint32Array(256);
        for (let n = 0; n < 256; n++) {
            let c = n;
            for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
            table[n] = c >>> 0;
        }
        return table;
    })();

    function crc32(bytes) {
        let crc = 0xFFFFFFFF;
        for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
        return (crc ^ 0xFFFFFFFF) >>> 0;
    }

    // An uncompressed ZIP (JPEGs don't compress further) from [{name, data}].
    // Returns the parts to put in a Blob, so large books are never copied
    // into one array.
    function zipStore(files) {
        const parts = [], central = [];
        let offset = 0;
        for (const file of files) {
            const name = new TextEncoder().encode(file.name);
            const crc = crc32(file.data);
            const local = new DataView(new ArrayBuffer(30));
            local.setUint32(0, 0x04034b50, true);
            local.setUint16(4, 20, true);
            local.setUint32(14, crc, true);
            local.setUint32(18, file.data.length, true);
            local.setUint32(22, file.data.length, true);
            local.setUint16(26, name.length, true);
            parts.push(local.buffer, name, file.data);
            const entry = new DataView(new ArrayBuffer(46));
            entry.setUint32(0, 0x02014b50, true);
            entry.setUint16(4, 20, true);
            entry.setUint16(6, 20, true);
            entry.setUint32(16, crc, true);
            entry.setUint32(20, file.data.length, true);
            entry.setUint32(24, file.data.length, true);
            entry.setUint16(28, name.length, true);
            entry.setUint32(42, offset, true);
            central.push(entry.buffer, name);
            offset += 30 + name.length + file.data.length;
        }
        const centralSize = central.reduce((sum, part) => sum + part.byteLength, 0);
        const end = new DataView(new ArrayBuffer(22));
        end.setUint32(0, 0x06054b50, true);
        end.setUint16(8, files.length, true);
        end.setUint16(10, files.length, true);
        end.setUint32(12, centralSize, true);
        end.setUint32(16, offset, true);
        return [...parts, ...central, end.buffer];
    }

    // The entries of a ZIP: [{name, method, data (compressed), size}].
    function zipEntries(buffer) {
        const view = new DataView(buffer);
        let end = -1;
        for (let i = buffer.byteLength - 22; i >= Math.max(0, buffer.byteLength - 65557); i--) {
            if (view.getUint32(i, true) === 0x06054b50) { end = i; break; }
        }
        if (end < 0) throw new Error('This is not a ZIP or CBZ file.');
        const count = view.getUint16(end + 10, true);
        let position = view.getUint32(end + 16, true);
        const entries = [];
        for (let i = 0; i < count; i++) {
            if (view.getUint32(position, true) !== 0x02014b50) throw new Error('The archive is damaged.');
            const method = view.getUint16(position + 10, true);
            const compressed = view.getUint32(position + 20, true);
            const size = view.getUint32(position + 24, true);
            const nameLength = view.getUint16(position + 28, true);
            const extraLength = view.getUint16(position + 30, true);
            const commentLength = view.getUint16(position + 32, true);
            const localOffset = view.getUint32(position + 42, true);
            const name = new TextDecoder().decode(new Uint8Array(buffer, position + 46, nameLength));
            const dataStart = localOffset + 30 + view.getUint16(localOffset + 26, true) + view.getUint16(localOffset + 28, true);
            entries.push({ name, method, size, data: new Uint8Array(buffer, dataStart, compressed) });
            position += 46 + nameLength + extraLength + commentLength;
        }
        return entries;
    }

    async function inflate(entry) {
        if (entry.method === 0) return entry.data;
        if (entry.method !== 8) throw new Error(`${entry.name}: unsupported compression.`);
        if (typeof DecompressionStream === 'undefined') throw new Error('This browser cannot open ZIP files; try a current Chrome, Edge, Firefox or Safari.');
        const stream = new Blob([entry.data]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
        return new Uint8Array(await new Response(stream).arrayBuffer());
    }

    // "page2" before "page10".
    function naturalCompare(a, b) {
        return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });
    }

    function imageType(name) {
        const base = name.split('/').pop();
        if (!base || base.startsWith('.') || name.startsWith('__MACOSX/')) return null;
        return IMAGE_TYPES[base.split('.').pop().toLowerCase()] || null;
    }

    // ---- Pages ----

    // Draws a source onto a gray canvas fitted inside the target and encodes
    // a baseline JPEG (what browsers' canvas encoders produce). Images are
    // only ever shrunk; PDF pages (vectors) are drawn at the full target size.
    async function encodePage(draw, sourceWidth, sourceHeight, target, enlarge = false) {
        const fit = Math.min(target.width / sourceWidth, target.height / sourceHeight);
        const scale = enlarge ? fit : Math.min(fit, 1);
        const width = Math.max(1, Math.round(sourceWidth * scale));
        const height = Math.max(1, Math.round(sourceHeight * scale));
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const context = canvas.getContext('2d', { willReadFrequently: true });
        context.fillStyle = '#fff';
        context.fillRect(0, 0, width, height);
        context.imageSmoothingQuality = 'high';
        await draw(context, width, height);
        const image = context.getImageData(0, 0, width, height);
        const pixels = image.data;
        for (let i = 0; i < pixels.length; i += 4) {
            const gray = (pixels[i] * 77 + pixels[i + 1] * 150 + pixels[i + 2] * 29) >> 8;
            pixels[i] = pixels[i + 1] = pixels[i + 2] = gray;
        }
        context.putImageData(image, 0, 0);
        const blob = await new Promise((resolve) => canvas.toBlob(resolve, 'image/jpeg', JPEG_QUALITY));
        if (!blob) throw new Error('The browser could not encode a page.');
        return new Uint8Array(await blob.arrayBuffer());
    }

    let pdfjsLoading = null;
    function loadPdfJs() {
        if (window.pdfjsLib) return Promise.resolve(window.pdfjsLib);
        pdfjsLoading = pdfjsLoading || new Promise((resolve, reject) => {
            const script = document.createElement('script');
            script.src = '/pdfjs/pdf.min.js';
            script.onload = () => {
                window.pdfjsLib.GlobalWorkerOptions.workerSrc = '/pdfjs/pdf.worker.min.js';
                resolve(window.pdfjsLib);
            };
            script.onerror = () => { pdfjsLoading = null; reject(new Error('Could not load the PDF reader from the device.')); };
            document.head.appendChild(script);
        });
        return pdfjsLoading;
    }

    async function pdfPages(file, target, progress) {
        const pdfjs = await loadPdfJs();
        const pdf = await pdfjs.getDocument({
            data: new Uint8Array(await file.arrayBuffer()),
            isEvalSupported: false,  // pdf.js 3.x: no code from font data (CVE-2024-4367)
            // Character maps for PDFs whose CJK fonts aren't embedded; only
            // fetched when such a PDF is opened, and only with internet.
            cMapUrl: 'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/cmaps/',
            cMapPacked: true,
        }).promise;
        const pages = [];
        for (let number = 1; number <= pdf.numPages; number++) {
            progress(number - 1, pdf.numPages);
            const page = await pdf.getPage(number);
            const base = page.getViewport({ scale: 1 });
            const data = await encodePage(async (context, width) => {
                const viewport = page.getViewport({ scale: width / base.width });
                await page.render({ canvasContext: context, viewport, background: '#fff' }).promise;
            }, base.width, base.height, target, true);
            page.cleanup();
            pages.push(data);
        }
        await pdf.destroy();
        return pages;
    }

    async function archivePages(file, target, progress) {
        const entries = zipEntries(await file.arrayBuffer())
            .filter((entry) => entry.size > 0 && imageType(entry.name))
            .sort((a, b) => naturalCompare(a.name, b.name));
        if (!entries.length) throw new Error('No JPEG, PNG or WebP pages found in this archive.');
        const pages = [];
        for (let i = 0; i < entries.length; i++) {
            progress(i, entries.length);
            const blob = new Blob([await inflate(entries[i])], { type: imageType(entries[i].name) });
            let bitmap;
            try {
                bitmap = await createImageBitmap(blob);
            } catch (e) {
                throw new Error(`${entries[i].name} could not be read as an image.`);
            }
            pages.push(await encodePage((context, width, height) => context.drawImage(bitmap, 0, 0, width, height),
                                        bitmap.width, bitmap.height, target));
            bitmap.close();
        }
        return pages;
    }

    function isPageBook(name) {
        return /\.(pdf|cbz|zip)$/i.test(name);
    }

    // file: a PDF, CBZ or ZIP. target: {width, height} from /api/status.
    // progress(done, total). Returns a File named "<name>.cbz".
    async function convert(file, target, progress = () => {}) {
        const pages = /\.pdf$/i.test(file.name) ? await pdfPages(file, target, progress)
                                                : await archivePages(file, target, progress);
        progress(pages.length, pages.length);
        const digits = Math.max(4, String(pages.length).length);
        const parts = zipStore(pages.map((data, i) => ({ name: `${String(i + 1).padStart(digits, '0')}.jpg`, data })));
        const name = file.name.replace(/\.(pdf|cbz|zip)$/i, '') + '.cbz';
        return new File(parts, name, { type: 'application/vnd.comicbook+zip' });
    }

    return { convert, isPageBook, zipStore, zipEntries, crc32, naturalCompare };
})();

if (typeof module !== 'undefined') module.exports = PageBooks;
