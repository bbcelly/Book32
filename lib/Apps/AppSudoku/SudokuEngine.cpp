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

}  // namespace

const char* difficultyName(Difficulty difficulty) {
    switch (difficulty) {
        case EASY: return "Easy";
        case MEDIUM: return "Medium";
        case HARD: return "Hard";
        case EXPERT: return "Expert";
        default: return "";
    }
}

int targetClues(Difficulty difficulty) {
    static const int clues[DIFFICULTY_COUNT] = {40, 32, 27, 23};
    return difficulty < DIFFICULTY_COUNT ? clues[difficulty] : 40;
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

void generate(Difficulty difficulty, Rng& rng, uint8_t puzzle[81], uint8_t solution[81]) {
    memset(solution, 0, 81);
    Masks masks;
    fillFrom(solution, masks, &rng);
    memcpy(puzzle, solution, 81);

    uint8_t order[81];
    for (int i = 0; i < 81; ++i) order[i] = i;
    for (int i = 80; i > 0; --i) {
        const int j = rng.below(i + 1);
        const uint8_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    int clues = 81;
    const int target = targetClues(difficulty);
    for (int i = 0; i < 81 && clues > target; ++i) {
        const int cell = order[i];
        const uint8_t kept = puzzle[cell];
        puzzle[cell] = 0;
        if (countSolutions(puzzle, 2) == 1) --clues;
        else puzzle[cell] = kept;
    }
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
