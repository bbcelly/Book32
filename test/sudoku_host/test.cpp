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

    // The rater: the classic puzzle above falls to singles alone, and the
    // techniques reach the true solution.
    parse("53..7....6..195....98....6.8...6...34..8.3..17...2...6.6....28....419..5....8..79", puzzle);
    uint8_t logical[81];
    assert(rate(puzzle, logical) == EASY);
    memcpy(solution, puzzle, 81);
    solve(solution);
    assert(memcmp(logical, solution, 81) == 0);
    // "Platinum Blonde", a well-known puzzle beyond these techniques.
    parse(".......12........3..23..4....1....5..4.6..7.8...5.3.......8..6...7..4.21.3....5..", puzzle);
    assert(rate(puzzle) == -1);

    // Generated puzzles: one solution, the clues agree with it, and the
    // rating is the level asked for; any rating the techniques finish must
    // reach the true solution.
    Rng rng(12345);
    for (int d = 0; d < DIFFICULTY_COUNT; ++d) {
        const auto difficulty = static_cast<Difficulty>(d);
        const int rounds = 30;
        int hits = 0, minClues = 81, maxClues = 0;
        double slowestMs = 0, totalMs = 0;
        for (int round = 0; round < rounds; ++round) {
            const auto t0 = std::chrono::steady_clock::now();
            const int rating = generate(difficulty, rng, puzzle, solution);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            totalMs += ms;
            if (ms > slowestMs) slowestMs = ms;
            assert(validSolution(solution));
            int clues = 0;
            for (int i = 0; i < 81; ++i) {
                if (!puzzle[i]) continue;
                ++clues;
                assert(puzzle[i] == solution[i]);
            }
            assert(countSolutions(puzzle, 2) == 1);
            assert(rate(puzzle, logical) == rating);
            assert(rating >= 0 && rating <= d);
            assert(memcmp(logical, solution, 81) == 0);
            if (rating == d) ++hits;
            if (clues < minClues) minClues = clues;
            if (clues > maxClues) maxClues = clues;
        }
        printf("%-6s %2d/%d at the level, clues %d-%d, avg %.1f ms, slowest %.1f ms\n",
               difficultyName(difficulty), hits, rounds, minClues, maxClues, totalMs / rounds, slowestMs);
        assert(hits >= rounds * 9 / 10);
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

    puts("Sudoku PASS: solver, rater, generator, rules, undo, restore");
    return 0;
}
