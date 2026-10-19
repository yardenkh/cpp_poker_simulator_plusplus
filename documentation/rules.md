# Game rules as implemented

No-limit Texas Hold'em for 2–8 seats. This page states the engine's actual
behaviour, including simplifications. These rules are regression-tested; they
are not a claim of compatibility with every casino or tournament rulebook.

## Setup

- Default stacks 2,500, blinds 50/100 (`config/PokerConfig.hpp`). No rake, no
  antes.
- Seat 0 starts with the button. With 3+ players, seat 1 is the small blind and
  seat 2 the big blind.
- **Heads-up** (two seats, or only two funded seats left): the dealer posts the
  small blind; the other funded player posts the big blind.
- Eliminated seats (zero cash) are skipped for button, blinds and dealing.

## Dealing

- The deck is reset and shuffled at the start of each hand using the game's
  `std::mt19937`.
- Hole cards are dealt one at a time around the table, twice.
- One card is burned before the flop (3 cards), the turn (1) and the river (1).

## Blinds

- Blinds are paid through `PotManager::contribute`, which caps payment at the
  player's cash (a short blind makes that player all-in).
- The opening pre-flop target (`current_bet`) is the **larger actual blind
  payment**, which may be less than the configured big blind if the big blind is
  short.
- The minimum raise size starts at the **configured** big blind on every
  street.

## Action order

- Pre-flop: the search for the next actor starts after the big blind, so the
  first player left of the big blind acts first and the big blind gets the
  option if everyone calls.
- Post-flop: the search starts after the dealer.
- A player needs to act if they are in the hand, not all-in, have chips, and
  either owe chips or have not acted since the last full raise while an
  opponent can still act.

## Actions

| Action | Rule |
| --- | --- |
| Fold | Allowed whenever the player may act. Contributed chips stay in the pot. |
| Check | Only when nothing is owed. |
| Call | When something is owed. A player who cannot cover the call pays everything and is all-in. |
| RaiseTo N | N is the **total** round contribution. Requires `min_raise_to ≤ N ≤ max_raise_to`, raising rights still open, and an opponent able to respond. |
| AllIn | Pays all remaining cash. Can be a partial call, exact call, full raise or short raise. As a raise it needs open raising rights and an opponent able to respond. |

`min_raise_to = current_bet + minimum_raise_size`;
`max_raise_to = round contribution + remaining cash`.

## Full raises vs. short all-ins

- A raise whose increase is ≥ the current minimum raise size is a **full
  raise**: it sets the new minimum raise size and reopens betting for everyone
  (all "acted" flags are cleared).
- A smaller all-in is a **short raise**: it increases the amount others must
  call but does **not** change the minimum raise size and does **not** reopen
  raising for players who already acted. They may only call or fold the extra
  amount. Several short all-ins do not add up to a reopening.

## End of a betting round

The round ends when nobody needs to act. If only one player can still act and
they owe nothing, the round ends (no betting against all-in players). If every
remaining player is all-in, the board is dealt out automatically.

## Winning

- If all but one player fold, that player receives the entire pot immediately
  (no showdown, no cards evaluated).
- Otherwise, at showdown every remaining player's best five of seven cards is
  scored. Pots are built in layers (see below); each layer is split equally
  among the best eligible hands.

## Side pots

Contributions are sorted into levels. Each distinct level forms a layer whose
amount is `(level − previous level) × number of contributors at that level`.
Folded players' chips are included but folded players are not eligible to win.

Example: contributions 50, 100, 100 → main pot 150 (three players),
side pot 100 (two players).

An unmatched top contribution forms a layer with a single eligible player and
is returned to them through the normal payout. Gross payouts can therefore
include returned chips; use `last_rewards()` for net results.

## Odd chips

When a layer does not divide evenly, leftover chips go one at a time to tied
winners, starting with the first seat after the dealer.

## After the hand

`rewards[i] = cash after the hand − cash before the blinds`; rewards across
seats sum to zero. The button then moves to the next funded seat. A match ends
when at most one seat has chips.

## Tournament rules

See [modules/tournament.md](modules/tournament.md). In short:
single-table freezeout, equal stacks, no re-entry; blind levels advance after a
fixed number of completed hands (the last level holds forever); players busted
in the same hand are ranked by their stack before the blinds of that hand, and
equal stacks tie and split the combined prizes of the places they occupy.
