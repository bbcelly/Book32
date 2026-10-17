#include "PageBook.h"

#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// unzipLIB passes its ZIPFILE to every callback; fHandle is what open
// returned (the descriptor plus one, so 0 can mean failure). A descriptor of
// its own keeps the book readable while EpubLoader opens other archives
// (library covers, the sleep screen).
void* zipOpen(const char* filename, int32_t* size) {
    const int fd = ::open(filename, O_RDONLY);
    if (fd < 0) return nullptr;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        ::close(fd);
        return nullptr;
    }
    *size = static_cast<int32_t>(st.st_size);
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd + 1));
}

int descriptor(void* p) {
    return static_cast<int>(reinterpret_cast<intptr_t>(static_cast<ZIPFILE*>(p)->fHandle)) - 1;
}

void zipClose(void* p) {
    const int fd = descriptor(p);
    if (fd >= 0) ::close(fd);
}

int32_t zipRead(void* p, uint8_t* buffer, int32_t length) {
    const int fd = descriptor(p);
    if (fd < 0 || !buffer || length <= 0) return -1;
    return static_cast<int32_t>(::read(fd, buffer, length));
}

int32_t zipSeek(void* p, int32_t position, int type) {
    const int fd = descriptor(p);
    if (fd < 0) return -1;
    return static_cast<int32_t>(lseek(fd, position, type));
}

bool endsWithIgnoreCase(const String& text, const char* suffix) {
    String lower = text;
    lower.toLowerCase();
    return lower.endsWith(suffix);
}

bool isPageImage(const String& name) {
    if (name.endsWith("/") || name.startsWith("__MACOSX/")) return false;
    const int slash = name.lastIndexOf('/');
    if (name.charAt(slash + 1) == '.') return false;  // ._page1.jpg and other hidden files
    return endsWithIgnoreCase(name, ".jpg") || endsWithIgnoreCase(name, ".jpeg") ||
           endsWithIgnoreCase(name, ".png");
}

// "page2" before "page10": runs of digits compare by value.
bool naturalLess(const String& a, const String& b) {
    size_t i = 0, j = 0;
    while (i < a.length() && j < b.length()) {
        const char ca = a.charAt(i), cb = b.charAt(j);
        if (isdigit(ca) && isdigit(cb)) {
            size_t ei = i, ej = j;
            while (ei < a.length() && a.charAt(ei) == '0') ei++;
            while (ej < b.length() && b.charAt(ej) == '0') ej++;
            size_t si = ei, sj = ej;
            while (ei < a.length() && isdigit(a.charAt(ei))) ei++;
            while (ej < b.length() && isdigit(b.charAt(ej))) ej++;
            if (ei - si != ej - sj) return ei - si < ej - sj;
            for (size_t k = 0; k < ei - si; k++) {
                if (a.charAt(si + k) != b.charAt(sj + k)) return a.charAt(si + k) < b.charAt(sj + k);
            }
            i = ei;
            j = ej;
            continue;
        }
        const char la = tolower(ca), lb = tolower(cb);
        if (la != lb) return la < lb;
        i++;
        j++;
    }
    return a.length() - i < b.length() - j;
}

}  // namespace

PageBook::PageBook() {
    void* storage = ps_malloc(sizeof(UNZIP));  // UNZIP holds a 41 KB buffer
    if (!storage) storage = malloc(sizeof(UNZIP));
    _zip = storage ? new (storage) UNZIP() : nullptr;
}

PageBook::~PageBook() {
    close();
    if (_zip) {
        _zip->~UNZIP();
        free(_zip);
    }
}

bool PageBook::isPageBookName(const String& name) {
    return endsWithIgnoreCase(name, ".cbz");
}

bool PageBook::open(const String& path) {
    close();
    if (!_zip || _zip->openZIP(path.c_str(), zipOpen, zipClose, zipRead, zipSeek) != UNZ_OK) return false;
    _open = true;
    char name[256];
    unz_file_info info;
    for (int result = _zip->gotoFirstFile(); result == UNZ_OK; result = _zip->gotoNextFile()) {
        if (_zip->getFileInfo(&info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK) continue;
        String entry(name);
        if (info.uncompressed_size > 0 && isPageImage(entry)) _pages.push_back(entry);
        if ((_pages.size() & 31) == 0) yield();
    }
    std::sort(_pages.begin(), _pages.end(), naturalLess);
    Serial.printf("PageBook: %s, %d pages\n", path.c_str(), pageCount());
    if (_pages.empty()) {
        close();
        return false;
    }
    return true;
}

void PageBook::close() {
    if (_open && _zip) _zip->closeZIP();
    _open = false;
    _pages.clear();
}

uint8_t* PageBook::readPage(int index, size_t& size) {
    size = 0;
    if (!_open || index < 0 || index >= pageCount()) return nullptr;
    if (_zip->locateFile(_pages[index].c_str()) != UNZ_OK || _zip->openCurrentFile() != UNZ_OK) return nullptr;
    unz_file_info info = {};
    char name[256];
    if (_zip->getFileInfo(&info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK ||
        info.uncompressed_size == 0 ||
        info.uncompressed_size + 512 * 1024 > heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) {
        _zip->closeCurrentFile();
        return nullptr;
    }
    const size_t required = info.uncompressed_size;
    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) {
        _zip->closeCurrentFile();
        return nullptr;
    }
    size_t total = 0;
    while (total < required) {
        const int chunk = static_cast<int>(min(static_cast<size_t>(16384), required - total));
        const int count = _zip->readCurrentFile(data + total, chunk);
        if (count <= 0) break;
        total += count;
        yield();
    }
    _zip->closeCurrentFile();
    if (total != required) {
        free(data);
        return nullptr;
    }
    size = total;
    return data;
}
