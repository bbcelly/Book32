// Host test for the Sudoku rules and generator (lib/Apps/AppSudoku/SudokuEngine).
// Build: c++ -std=c++17 -O2 -I lib/Apps/AppSudoku test/sudoku_host/test.cpp lib/Apps/AppSudoku/SudokuEngine.cpp
#include <assert.h>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include "SudokuEngine.h"

using namespace Sudoku;

static void parse(const char* text, uint8_t grid[81]) {
    for (int i = 0; i < 81; ++i) grid[i] = text[i] == '.' ? 0 : text[i] - '0';
}

static bool validSolution(const uint8_t grid[81]) {
    for (int unit = 0; unit < 9; ++unit) {
        uint16_t row = 0, col = 0, box = 0;
        for (int i = 0; i < 9; ++i) {
            row |= 1u << grid[unit * 9 + i];
            col |= 1u << grid[i * 9 + unit];
            box |= 1u << grid[(unit / 3 * 3 + i / 3) * 9 + unit % 3 * 3 + i % 3];
        }
        if (row != 0x3FE || col != 0x3FE || box != 0x3FE) return false;
    }
    return true;
}

int main() {
    // A known puzzle with one solution.
    uint8_t puzzle[81], solution[81];
    parse("53..7....6..195....98....6.8...6...34..8.3..17...2...6.6....28....419..5....8..79", puzzle);
    assert(countSolutions(puzzle, 2) == 1);
    memcpy(solution, puzzle, 81);
    assert(solve(solution) && validSolution(solution));
    assert(solution[2] == 4);

    // An empty grid has many solutions; repeated clues have none.
    uint8_t empty[81] = {};
    assert(countSolutions(empty, 2) == 2);
    uint8_t clash[81] = {};
    clash[0] = clash[1] = 5;
    assert(countSolutions(clash, 2) == 0);

    // Generated puzzles: one solution, the clues agree with it, near the target.
    Rng rng(12345);
    for (int d = 0; d < DIFFICULTY_COUNT; ++d) {
        const auto difficulty = static_cast<Difficulty>(d);
        int worstClues = 0;
        double slowestMs = 0;
        for (int round = 0; round < 20; ++round) {
            const auto t0 = std::chrono::steady_clock::now();
            generate(difficulty, rng, puzzle, solution);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > slowestMs) slowestMs = ms;
            assert(validSolution(solution));
            int clues = 0;
            for (int i = 0; i < 81; ++i) {
                if (!puzzle[i]) continue;
                ++clues;
                assert(puzzle[i] == solution[i]);
            }
            assert(countSolutions(puzzle, 2) == 1);
            assert(clues >= targetClues(difficulty));
            if (clues > worstClues) worstClues = clues;
        }
        printf("%-6s target %d clues, worst %d, slowest %.1f ms\n", difficultyName(difficulty),
               targetClues(difficulty), worstClues, slowestMs);
    }

    // Game rules: clues are fixed, repeats are flagged, undo restores.
    parse("53..7....6..195....98....6.8...6...34..8.3..17...2...6.6....28....419..5....8..79", puzzle);
    Game game;
    game.start(puzzle, EASY);
    assert(!game.setValue(0, 1));          // a clue
    assert(game.toggleNote(2, 4) && game.notes(2) == (1u << 4));
    assert(game.setValue(2, 5));           // 5 is already in the row
    assert(game.notes(2) == 0);            // a digit replaces the notes
    assert(game.conflicts(2) && game.conflicts(0));
    assert(!game.conflicts(1));
    assert(game.undo() && game.value(2) == 0 && game.notes(2) == (1u << 4));
    assert(game.undo() && game.notes(2) == 0 && !game.canUndo());
    assert(!game.clear(2));                // nothing to clear

    // Filling in the solution finishes the game.
    memcpy(solution, puzzle, 81);
    solve(solution);
    for (int i = 0; i < 81; ++i)
        if (!game.isGiven(i)) game.setValue(i, solution[i]);
    assert(game.solved());
    int open = 80;
    while (game.isGiven(open)) --open;
    assert(game.setValue(open, solution[open] % 9 + 1));  // a wrong digit
    assert(!game.solved());

    // Restore keeps clues and drops notes on filled cells.
    uint8_t given[81] = {}, value[81] = {};
    uint16_t notes[81] = {};
    given[0] = 1;
    value[0] = 7;
    value[1] = 3;
    notes[1] = 1u << 2;
    notes[2] = (1u << 1) | (1u << 9);
    game.restore(given, value, notes, MEDIUM);
    assert(game.isGiven(0) && !game.isGiven(1) && game.value(1) == 3);
    assert(game.notes(1) == 0 && game.notes(2) == ((1u << 1) | (1u << 9)));
    assert(game.difficulty() == MEDIUM && !game.canUndo());

    puts("Sudoku PASS: solver, generator, rules, undo, restore");
    return 0;
}
