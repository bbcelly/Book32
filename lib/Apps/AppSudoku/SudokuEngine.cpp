#include "SudokuEngine.h"
#include <string.h>

namespace Sudoku {

namespace {

constexpr uint16_t ALL_DIGITS = 0x3FE;  // bits 1..9

inline int boxOf(int cell) { return (cell / 27) * 3 + (cell % 9) / 3; }

// Row, column and box masks of the digits already placed.
struct Masks {
    uint16_t row[9] = {}, col[9] = {}, box[9] = {};

    bool load(const uint8_t grid[81]) {
        for (int cell = 0; cell < 81; ++cell) {
            if (!grid[cell]) continue;
            const uint16_t bit = 1u << grid[cell];
            const int r = cell / 9, c = cell % 9, b = boxOf(cell);
            if ((row[r] | col[c] | box[b]) & bit) return false;  // the clues repeat
            row[r] |= bit;
            col[c] |= bit;
            box[b] |= bit;
        }
        return true;
    }
    uint16_t candidates(int cell) const {
        return ALL_DIGITS & ~(row[cell / 9] | col[cell % 9] | box[boxOf(cell)]);
    }
    void toggle(int cell, uint16_t bit) {
        row[cell / 9] ^= bit;
        col[cell % 9] ^= bit;
        box[boxOf(cell)] ^= bit;
    }
};

int popcount(uint16_t bits) {
    int n = 0;
    for (; bits; bits &= bits - 1) ++n;
    return n;
}

// The empty cell with the fewest candidates, or -1 when the grid is full.
int pickCell(const uint8_t grid[81], const Masks& masks, uint16_t& options) {
    int best = -1, bestCount = 10;
    for (int cell = 0; cell < 81; ++cell) {
        if (grid[cell]) continue;
        const uint16_t cand = masks.candidates(cell);
        const int count = popcount(cand);
        if (count < bestCount) {
            best = cell;
            bestCount = count;
            options = cand;
            if (count <= 1) break;
        }
    }
    return best;
}

void countFrom(uint8_t grid[81], Masks& masks, int limit, int& found) {
    uint16_t options = 0;
    const int cell = pickCell(grid, masks, options);
    if (cell < 0) {
        ++found;
        return;
    }
    for (int digit = 1; digit <= 9 && found < limit; ++digit) {
        const uint16_t bit = 1u << digit;
        if (!(options & bit)) continue;
        grid[cell] = digit;
        masks.toggle(cell, bit);
        countFrom(grid, masks, limit, found);
        masks.toggle(cell, bit);
        grid[cell] = 0;
    }
}

// Depth-first fill; with an Rng the digits are tried in random order.
bool fillFrom(uint8_t grid[81], Masks& masks, Rng* rng) {
    uint16_t options = 0;
    const int cell = pickCell(grid, masks, options);
    if (cell < 0) return true;
    uint8_t order[9];
    int n = 0;
    for (int digit = 1; digit <= 9; ++digit)
        if (options & (1u << digit)) order[n++] = digit;
    if (rng) {
        for (int i = n - 1; i > 0; --i) {
            const int j = rng->below(i + 1);
            const uint8_t t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
    }
    for (int i = 0; i < n; ++i) {
        const uint16_t bit = 1u << order[i];
        grid[cell] = order[i];
        masks.toggle(cell, bit);
        if (fillFrom(grid, masks, rng)) return true;
        masks.toggle(cell, bit);
        grid[cell] = 0;
    }
    return false;
}

// --- The human-style solver behind rate() ---

// Units 0-8 are rows, 9-17 columns, 18-26 boxes.
struct Geometry {
    uint8_t units[27][9];
    uint8_t peers[81][20];

    Geometry() {
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                units[i][j] = i * 9 + j;
                units[9 + i][j] = j * 9 + i;
                units[18 + i][j] = (i / 3 * 3 + j / 3) * 9 + i % 3 * 3 + j % 3;
            }
        }
        for (int cell = 0; cell < 81; ++cell) {
            int n = 0;
            for (int other = 0; other < 81; ++other)
                if (other != cell && sees(cell, other)) peers[cell][n++] = other;
        }
    }
    static bool sees(int a, int b) {
        return a / 9 == b / 9 || a % 9 == b % 9 || boxOf(a) == boxOf(b);
    }
};

const Geometry& geometry() {
    static const Geometry g;
    return g;
}

inline int lowestDigit(uint16_t bits) {
    for (int d = 1; d <= 9; ++d)
        if (bits & (1u << d)) return d;
    return 0;
}

struct Logic {
    uint8_t value[81];
    uint16_t cand[81];  // candidates of empty cells; 0 once filled
    bool broken = false;

    bool load(const uint8_t puzzle[81]) {
        Masks masks;
        if (!masks.load(puzzle)) return false;
        for (int cell = 0; cell < 81; ++cell) {
            value[cell] = puzzle[cell];
            cand[cell] = puzzle[cell] ? 0 : masks.candidates(cell);
        }
        return true;
    }
    bool solved() const {
        for (int cell = 0; cell < 81; ++cell)
            if (!value[cell]) return false;
        return true;
    }
    void place(int cell, int digit) {
        value[cell] = digit;
        cand[cell] = 0;
        for (uint8_t peer : geometry().peers[cell]) cand[peer] &= ~(1u << digit);
    }
    bool eliminate(int cell, uint16_t bits) {
        if (!(cand[cell] & bits)) return false;
        cand[cell] &= ~bits;
        return true;
    }

    // Naked singles (one candidate left) and hidden singles (one place left).
    bool singles() {
        bool progress = false;
        for (int cell = 0; cell < 81; ++cell) {
            if (value[cell]) continue;
            if (!cand[cell]) {
                broken = true;
                return false;
            }
            if (popcount(cand[cell]) == 1) {
                place(cell, lowestDigit(cand[cell]));
                progress = true;
            }
        }
        for (const auto& unit : geometry().units) {
            for (int d = 1; d <= 9; ++d) {
                const uint16_t bit = 1u << d;
                int count = 0, where = -1;
                bool placed = false;
                for (uint8_t cell : unit) {
                    if (value[cell] == d) placed = true;
                    else if (cand[cell] & bit) {
                        ++count;
                        where = cell;
                    }
                }
                if (placed) continue;
                if (count == 0) {
                    broken = true;
                    return false;
                }
                if (count == 1) {
                    place(where, d);
                    progress = true;
                }
            }
        }
        return progress;
    }

    // A digit confined to one line inside a box leaves the rest of that line
    // (pointing), and confined to one box inside a line leaves the rest of
    // that box (claiming).
    bool lockedCandidates() {
        const Geometry& g = geometry();
        bool progress = false;
        for (int d = 1; d <= 9; ++d) {
            const uint16_t bit = 1u << d;
            for (int u = 0; u < 27; ++u) {
                int first = -1;
                bool sameRow = true, sameCol = true, sameBox = true;
                for (uint8_t cell : g.units[u]) {
                    if (!(cand[cell] & bit)) continue;
                    if (first < 0) first = cell;
                    sameRow &= cell / 9 == first / 9;
                    sameCol &= cell % 9 == first % 9;
                    sameBox &= boxOf(cell) == boxOf(first);
                }
                if (first < 0) continue;
                int target = -1;
                if (u >= 18 && sameRow) target = first / 9;            // pointing along a row
                else if (u >= 18 && sameCol) target = 9 + first % 9;   // pointing along a column
                else if (u < 18 && sameBox) target = 18 + boxOf(first);  // claiming
                if (target < 0) continue;
                for (uint8_t cell : g.units[target]) {
                    bool inSource = false;
                    for (uint8_t s : g.units[u]) inSource |= s == cell;
                    if (!inSource) progress |= eliminate(cell, bit);
                }
            }
        }
        return progress;
    }

    // Naked subsets: k cells of a unit sharing k candidates. Hidden subsets:
    // k digits of a unit that fit only in k cells.
    bool subsets(int k) {
        const Geometry& g = geometry();
        bool progress = false;
        for (const auto& unit : g.units) {
            int open[9], n = 0;
            for (uint8_t cell : unit)
                if (!value[cell]) open[n++] = cell;
            if (n <= k) continue;
            // Naked: every k-combination of open cells.
            int idx[3];
            for (idx[0] = 0; idx[0] < n; ++idx[0])
                for (idx[1] = idx[0] + 1; idx[1] < n; ++idx[1])
                    for (idx[2] = (k == 3 ? idx[1] + 1 : n); idx[2] <= (k == 3 ? n - 1 : n); ++idx[2]) {
                        uint16_t combined = 0;
                        for (int i = 0; i < k; ++i) combined |= cand[open[idx[i]]];
                        if (popcount(combined) != k) continue;
                        for (int i = 0; i < n; ++i) {
                            bool member = false;
                            for (int j = 0; j < k; ++j) member |= i == idx[j];
                            if (!member) progress |= eliminate(open[i], combined);
                        }
                    }
            // Hidden: every k-combination of digits still open in the unit.
            uint16_t where[10] = {};
            for (int i = 0; i < n; ++i)
                for (int d = 1; d <= 9; ++d)
                    if (cand[open[i]] & (1u << d)) where[d] |= 1u << i;
            int digits[9], m = 0;
            for (int d = 1; d <= 9; ++d)
                if (where[d]) digits[m++] = d;
            for (idx[0] = 0; idx[0] < m; ++idx[0])
                for (idx[1] = idx[0] + 1; idx[1] < m; ++idx[1])
                    for (idx[2] = (k == 3 ? idx[1] + 1 : m); idx[2] <= (k == 3 ? m - 1 : m); ++idx[2]) {
                        uint16_t cells = 0, keep = 0;
                        for (int i = 0; i < k; ++i) {
                            cells |= where[digits[idx[i]]];
                            keep |= 1u << digits[idx[i]];
                        }
                        if (popcount(cells) != k) continue;
                        for (int i = 0; i < n; ++i)
                            if (cells & (1u << i)) progress |= eliminate(open[i], ALL_DIGITS & ~keep);
                    }
        }
        return progress;
    }

    // X-Wing (k = 2) and Swordfish (k = 3): a digit whose places in k rows
    // fall in k columns leaves the rest of those columns, and the other way
    // round.
    bool fish(int k) {
        bool progress = false;
        for (int d = 1; d <= 9; ++d) {
            const uint16_t bit = 1u << d;
            for (int byRows = 0; byRows < 2; ++byRows) {
                uint16_t lines[9];
                int base[9], n = 0;
                for (int line = 0; line < 9; ++line) {
                    uint16_t spots = 0;
                    for (int i = 0; i < 9; ++i) {
                        const int cell = byRows ? line * 9 + i : i * 9 + line;
                        if (cand[cell] & bit) spots |= 1u << i;
                    }
                    const int count = popcount(spots);
                    if (count >= 2 && count <= k) {
                        lines[n] = spots;
                        base[n++] = line;
                    }
                }
                int idx[3];
                for (idx[0] = 0; idx[0] < n; ++idx[0])
                    for (idx[1] = idx[0] + 1; idx[1] < n; ++idx[1])
                        for (idx[2] = (k == 3 ? idx[1] + 1 : n); idx[2] <= (k == 3 ? n - 1 : n); ++idx[2]) {
                            uint16_t cover = 0;
                            for (int i = 0; i < k; ++i) cover |= lines[idx[i]];
                            if (popcount(cover) != k) continue;
                            for (int line = 0; line < 9; ++line) {
                                bool member = false;
                                for (int j = 0; j < k; ++j) member |= base[idx[j]] == line;
                                if (member) continue;
                                for (int i = 0; i < 9; ++i) {
                                    if (!(cover & (1u << i))) continue;
                                    const int cell = byRows ? line * 9 + i : i * 9 + line;
                                    progress |= eliminate(cell, bit);
                                }
                            }
                        }
            }
        }
        return progress;
    }

    // XY-Wing: a pivot {x,y} sees pincers {x,z} and {y,z}; z leaves every
    // cell that sees both pincers.
    bool xyWing() {
        const Geometry& g = geometry();
        bool progress = false;
        for (int pivot = 0; pivot < 81; ++pivot) {
            if (popcount(cand[pivot]) != 2) continue;
            for (uint8_t a : g.peers[pivot]) {
                const uint16_t sharedA = cand[a] & cand[pivot];
                if (popcount(cand[a]) != 2 || popcount(sharedA) != 1) continue;
                const uint16_t z = cand[a] & ~cand[pivot];
                const uint16_t needB = (cand[pivot] & ~sharedA) | z;
                for (uint8_t b : g.peers[pivot]) {
                    if (b == a || cand[b] != needB) continue;
                    for (int cell = 0; cell < 81; ++cell) {
                        if (cell == a || cell == b || cell == pivot) continue;
                        if (Geometry::sees(cell, a) && Geometry::sees(cell, b)) progress |= eliminate(cell, z);
                    }
                }
            }
        }
        return progress;
    }
};

}  // namespace

int rate(const uint8_t puzzle[81], uint8_t* solvedOut) {
    Logic logic;
    if (!logic.load(puzzle)) return -1;
    int level = EASY;
    while (!logic.solved()) {
        if (logic.singles()) continue;
        if (logic.broken) return -1;
        int used;
        if (logic.lockedCandidates()) used = MEDIUM;
        else if (logic.subsets(2) || logic.subsets(3)) used = HARD;
        else if (logic.fish(2) || logic.fish(3) || logic.xyWing()) used = EXPERT;
        else return -1;
        if (used > level) level = used;
    }
    if (solvedOut) memcpy(solvedOut, logic.value, 81);
    return level;
}

const char* difficultyName(Difficulty difficulty) {
    switch (difficulty) {
        case EASY: return "Easy";
        case MEDIUM: return "Medium";
        case HARD: return "Hard";
        case EXPERT: return "Expert";
        default: return "";
    }
}

const char* difficultyHint(Difficulty difficulty) {
    switch (difficulty) {
        case EASY: return "Singles only";
        case MEDIUM: return "Pointing and claiming";
        case HARD: return "Pairs and triples";
        case EXPERT: return "X-Wing, Swordfish, XY-Wing";
        default: return "";
    }
}

uint32_t Rng::next() {
    _state ^= _state << 13;
    _state ^= _state >> 17;
    _state ^= _state << 5;
    return _state;
}

int countSolutions(const uint8_t grid[81], int limit) {
    uint8_t work[81];
    memcpy(work, grid, 81);
    Masks masks;
    if (!masks.load(work)) return 0;
    int found = 0;
    countFrom(work, masks, limit, found);
    return found;
}

bool solve(uint8_t grid[81]) {
    Masks masks;
    if (!masks.load(grid)) return false;
    return fillFrom(grid, masks, nullptr);
}

int generate(Difficulty difficulty, Rng& rng, uint8_t puzzle[81], uint8_t solution[81], int maxAttempts,
             void (*tick)()) {
    const int target = difficulty < DIFFICULTY_COUNT ? difficulty : EASY;
    // The technique sets the level; a clue floor keeps the lower levels from
    // turning into long searches through an almost empty grid.
    static const int floors[DIFFICULTY_COUNT] = {36, 30, 26, 17};
    const int minClues = floors[target];
    uint8_t candidate[81], full[81];
    int bestRating = -2;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (tick) tick();
        memset(full, 0, 81);
        Masks masks;
        fillFrom(full, masks, &rng);
        memcpy(candidate, full, 81);

        uint8_t order[81];
        for (int i = 0; i < 81; ++i) order[i] = i;
        for (int i = 80; i > 0; --i) {
            const int j = rng.below(i + 1);
            const uint8_t t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
        // Dig while the puzzle keeps one solution and stays within the level.
        int clues = 81;
        for (int i = 0; i < 81 && clues > minClues; ++i) {
            const int cell = order[i];
            const uint8_t kept = candidate[cell];
            candidate[cell] = 0;
            const int rating = countSolutions(candidate, 2) == 1 ? rate(candidate) : -1;
            if (rating >= 0 && rating <= target) --clues;
            else candidate[cell] = kept;
        }
        const int rating = rate(candidate);
        if (rating > bestRating) {
            bestRating = rating;
            memcpy(puzzle, candidate, 81);
            memcpy(solution, full, 81);
        }
        if (rating == target) break;
    }
    return bestRating;
}

void Game::start(const uint8_t puzzle[81], Difficulty difficulty) {
    for (int cell = 0; cell < 81; ++cell) {
        _given[cell] = puzzle[cell] != 0;
        _value[cell] = puzzle[cell];
        _notes[cell] = 0;
    }
    _difficulty = difficulty;
    _history.clear();
}

void Game::restore(const uint8_t given[81], const uint8_t value[81], const uint16_t notes[81],
                   Difficulty difficulty) {
    for (int cell = 0; cell < 81; ++cell) {
        _given[cell] = given[cell] != 0;
        _value[cell] = value[cell] <= 9 ? value[cell] : 0;
        _notes[cell] = _value[cell] ? 0 : (notes[cell] & ALL_DIGITS);
    }
    _difficulty = difficulty < DIFFICULTY_COUNT ? difficulty : EASY;
    _history.clear();
}

void Game::remember(int cell) {
    if (_history.size() >= MAX_HISTORY) _history.erase(_history.begin());
    _history.push_back({static_cast<uint8_t>(cell), _value[cell], _notes[cell]});
}

bool Game::setValue(int cell, uint8_t digit) {
    if (cell < 0 || cell >= 81 || _given[cell] || digit < 1 || digit > 9) return false;
    if (_value[cell] == digit) return false;
    remember(cell);
    _value[cell] = digit;
    _notes[cell] = 0;
    return true;
}

bool Game::toggleNote(int cell, uint8_t digit) {
    if (cell < 0 || cell >= 81 || _given[cell] || _value[cell] || digit < 1 || digit > 9) return false;
    remember(cell);
    _notes[cell] ^= 1u << digit;
    return true;
}

bool Game::clear(int cell) {
    if (cell < 0 || cell >= 81 || _given[cell] || (!_value[cell] && !_notes[cell])) return false;
    remember(cell);
    _value[cell] = 0;
    _notes[cell] = 0;
    return true;
}

bool Game::undo() {
    if (_history.empty()) return false;
    const Step step = _history.back();
    _history.pop_back();
    _value[step.cell] = step.value;
    _notes[step.cell] = step.notes;
    return true;
}

bool Game::conflicts(int cell) const {
    const uint8_t digit = _value[cell];
    if (!digit) return false;
    const int r = cell / 9, c = cell % 9, b = boxOf(cell);
    for (int other = 0; other < 81; ++other) {
        if (other == cell || _value[other] != digit) continue;
        if (other / 9 == r || other % 9 == c || boxOf(other) == b) return true;
    }
    return false;
}

int Game::filledCount() const {
    int n = 0;
    for (int cell = 0; cell < 81; ++cell)
        if (_value[cell]) ++n;
    return n;
}

bool Game::solved() const {
    if (filledCount() != 81) return false;
    for (int cell = 0; cell < 81; ++cell)
        if (conflicts(cell)) return false;
    return true;
}

}  // namespace Sudoku
