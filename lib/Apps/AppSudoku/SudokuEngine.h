#pragma once
#include <stdint.h>
#include <vector>

// The Sudoku rules and puzzle generator, free of Arduino and display code so
// the host test (test/sudoku_host) can run it. Cells are 0..80, row-major;
// a value of 0 is empty.
namespace Sudoku {

enum Difficulty : uint8_t { EASY = 0, MEDIUM, HARD, EXPERT, DIFFICULTY_COUNT };

const char* difficultyName(Difficulty difficulty);
// One line on what the level asks of the player.
const char* difficultyHint(Difficulty difficulty);

// Difficulty is the hardest technique a human needs, not the clue count.
// rate() solves the puzzle the way a person would, always with the simplest
// technique that makes progress, and returns the level of the hardest one
// it used:
//   EASY    naked and hidden singles
//   MEDIUM  + locked candidates (pointing and claiming)
//   HARD    + naked and hidden pairs and triples
//   EXPERT  + X-Wing, Swordfish and XY-Wing
// -1 means these techniques do not finish it (it would need guessing).
// `solvedOut`, if given, receives the grid the techniques reached.
int rate(const uint8_t puzzle[81], uint8_t* solvedOut = nullptr);

// Small deterministic PRNG (xorshift32); the app seeds it from the hardware.
class Rng {
public:
    explicit Rng(uint32_t seed) : _state(seed ? seed : 0x9E3779B9u) {}
    uint32_t next();
    int below(int bound) { return static_cast<int>(next() % static_cast<uint32_t>(bound)); }

private:
    uint32_t _state;
};

// Counts solutions of a grid, stopping at `limit`.
int countSolutions(const uint8_t grid[81], int limit);
// Fills `grid` with its first solution; false when it has none.
bool solve(uint8_t grid[81]);

// A puzzle with exactly one solution whose rate() is `difficulty`.
// `puzzle` holds the clues. Candidates that rate differently are thrown
// away; after `maxAttempts` the closest easier one is kept. `tick` (if set)
// runs between attempts so a device can feed its watchdog. Returns the
// puzzle's rating.
int generate(Difficulty difficulty, Rng& rng, uint8_t puzzle[81], uint8_t solution[81],
             int maxAttempts = 400, void (*tick)() = nullptr);

// One game in progress: clues, the player's digits, pencil marks and undo.
class Game {
public:
    void start(const uint8_t puzzle[81], Difficulty difficulty);

    bool isGiven(int cell) const { return _given[cell]; }
    uint8_t value(int cell) const { return _value[cell]; }
    // Bit d (1..9) is set when the note d is shown.
    uint16_t notes(int cell) const { return _notes[cell]; }
    Difficulty difficulty() const { return _difficulty; }

    // Each returns false when nothing changed (a clue, or the same value).
    bool setValue(int cell, uint8_t digit);
    bool toggleNote(int cell, uint8_t digit);
    bool clear(int cell);
    bool undo();
    bool canUndo() const { return !_history.empty(); }

    // The digit repeats in the cell's row, column or box.
    bool conflicts(int cell) const;
    // Every cell filled with no repeats: with a unique puzzle, the solution.
    bool solved() const;
    int filledCount() const;

    // For restoring a saved game; the undo history starts empty.
    void restore(const uint8_t given[81], const uint8_t value[81], const uint16_t notes[81],
                 Difficulty difficulty);

private:
    struct Step {
        uint8_t cell;
        uint8_t value;
        uint16_t notes;
    };
    static constexpr size_t MAX_HISTORY = 200;

    void remember(int cell);

    bool _given[81] = {};
    uint8_t _value[81] = {};
    uint16_t _notes[81] = {};
    Difficulty _difficulty = EASY;
    std::vector<Step> _history;
};

}  // namespace Sudoku
