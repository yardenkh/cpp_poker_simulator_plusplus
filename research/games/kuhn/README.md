# Kuhn poker

A separate two-player game for testing a future CFR/CFR+ trainer. It does not
depend on the Hold'em engine, the 52-card deck, or hand-evaluation tables.

## Rules

- Three cards: jack < queen < king. Each player antes one chip.
- Each receives one private card; the third card stays hidden.
- Player 0 acts first. With no outstanding bet, check or bet one chip.
- After a bet, the opponent must fold or call. No raises.
- Two checks or a call lead to showdown; the higher card wins.
- Net profit is +1/-1 after a fold or checked showdown, and +2/-2 after a call.
  Returning an unmatched bet is not extra profit.

## API

`KuhnGame` starts at chance. `chance_outcomes()` lists all six equally likely
ordered deals. Select one with `deal()`; an exact trainer can enumerate them,
while a sampled trainer can select one uniformly.

At a player decision, use `acting_player()`, `legal_actions()` and
`information_set()`, then `apply_action()`. The information set contains only
the acting player's card and the public action history. Terminal `payoffs()`
contains net profit for players 0 and 1, including antes.

The game has value semantics: save a copy to explore alternative actions, then
restore by assignment. Strategies should receive `InformationSet`, not the full
game or selected deal. There is no internal action RNG.

The `poker_kuhn` library is available for the future trainer to link against.
This module implements the game and its tests; no CFR trainer is included yet.

## Reading the implementation

- `KuhnGame.hpp` describes the public API, ownership/lifetime rules, and the
  distinction between simulator state and strategy-visible information.
- `KuhnGame.cpp` derives the next decision or terminal outcome from the public
  history. Because bets are fixed and raises are forbidden, separate mutable
  stack/pot objects would duplicate information already determined by history.
- `KuhnGameTests.cpp` lists expected terminal outcomes independently, exhausts
  the action tree, and checks which hidden states must share a strategy key.

A typical traversal first selects a deal, reads the current information set,
copies the game, and explores each legal action from that copy. At a terminal
state it reads `payoffs()`. A future trainer will use those returned values to
update regrets; this module itself never chooses a strategy or updates regrets.

For example, after `Check, Bet`, player 0 must choose `Fold` or `Call`.
Folding loses the one-chip ante regardless of the cards. Calling reaches a
showdown and wins or loses two chips in net profit. This is why a fold must not
be scored as losing the whole three-chip pot, and why the terminal payoff is
always returned in player order rather than from the last actor's perspective.

## Build and test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target poker_kuhn_tests
ctest --test-dir build -R '^poker_kuhn_tests$' --output-on-failure
```

Tests exhaust six deals and five terminal betting paths per deal, check exact
net payoffs and the 12 information sets, reject illegal operations, and verify
branch independence and copy restoration. Checks remain active in Release.
