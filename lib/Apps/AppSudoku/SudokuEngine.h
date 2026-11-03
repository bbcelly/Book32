#pragma once
#include <stdint.h>
#include <vector>

// The Sudoku rules and puzzle generator, free of Arduino and display code so
// the host test (test/sudoku_host) can run it. Cells are 0..80, row-major;
// a value of 0 is empty.
namespace Sudoku {

enum Difficulty : uint8_t { EASY = 0, MEDIUM, HARD, EXPERT, DIFFICULTY_COUNT };

const char* difficultyName(Difficulty difficulty);
// Clues the generator aims for. Removal stops earlier when every further
// removal would allow a second solution.
int targetClues(Difficulty difficulty);

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

// A puzzle with exactly one solution. `puzzle` holds the clues.
void generate(Difficulty difficulty, Rng& rng, uint8_t puzzle[81], uint8_t solution[81]);

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
