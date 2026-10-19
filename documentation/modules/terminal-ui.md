# Terminal UI and CLI

Files: `ui/TerminalUI.hpp`, `ui/TerminalUI.cpp`, `decision/HumanDecisionProvider.*`,
`main.cpp`

These live in the separate `poker_terminal` library so headless users only
link `poker_engine`.

## TerminalUI

`TerminalUI` is both a `GameObserver` and a `TournamentObserver`, and also the
input source for the human player.

```cpp
TerminalUI();                                           // std::cin / std::cout
TerminalUI(std::istream& in, std::ostream& out, bool interactive = false);
PlayerDecision request_decision(const PlayerView& view);
void print_player_view(const PlayerView& view) const;
```

- On a real terminal it redraws a dashboard (table, stacks, last actions,
  available choices). When output is redirected it prints plain text.
- The stream constructor lets tests script input and capture output; passing
  `interactive = true` exercises dashboard redraws without a terminal.
- It caches only display data (last actions, starting stacks, showdown text,
  tournament level/prizes). The engine stays the source of truth.
- The "YOU" seat is whichever seat last asked for a decision through
  `request_decision` (seat 0 in the CLI). The dashboard is built from that
  seat's perspective; other hole cards appear only in the showdown summary.

### Input commands

`fold`/`f`, `check`/`k`, `call`/`c`, `allin`/`a`, `raise N`/`r N` (raise **to**
round total N). A line must be exactly one command (plus the number for
raise); malformed lines and extra words are rejected rather than partially
accepted.

## HumanDecisionProvider

`choose_action(view)` → `terminal_ui.request_decision(view)`. The UI is
borrowed.

## main.cpp

1. Parse arguments: none → tournament; `--game` → fixed-blind match;
   `--help` → usage (exit 0); anything else → usage (exit 1).
2. Ask for a player count 2..8 (re-prompts on bad input; EOF exits cleanly).
3. Seat 0 = human, others = a separate `AIDecisionProvider` per seat.
4. Seed from `std::random_device` once.
5. Tournament mode: prize pool `1000 × players` units, paying 70/30 heads-up or
   60/30/10 otherwise, default blind schedule; the UI is attached as both
   observers.
6. Any exception prints its message and returns 1.
