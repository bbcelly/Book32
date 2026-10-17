#pragma once

#include <Arduino.h>
#include <vector>
#include <unzipLIB.h>

// A book made of page images: a CBZ (a ZIP of JPEG or PNG pages, the usual
// manga format). The web interface turns PDFs into these when they are
// uploaded, and re-encodes uploaded CBZs, so every page is a baseline JPEG
// at twice the screen's resolution (sharp when zoomed in).
//
// Pages are the archive's images in natural name order (page2 before page10).
class PageBook {
public:
    PageBook();
    ~PageBook();
    PageBook(const PageBook&) = delete;
    PageBook& operator=(const PageBook&) = delete;

    // `path` is a POSIX path such as /ebooks/Name.cbz.
    bool open(const String& path);
    void close();
    bool isOpen() const { return _open; }
    int pageCount() const { return static_cast<int>(_pages.size()); }

    // The compressed image of one page, in PSRAM; the caller frees it.
    uint8_t* readPage(int index, size_t& size);

    static bool isPageBookName(const String& name);

private:
    UNZIP* _zip = nullptr;
    bool _open = false;
    std::vector<String> _pages;
};
