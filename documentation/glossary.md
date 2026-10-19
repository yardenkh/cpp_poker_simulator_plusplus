# Glossary

| Term | Meaning in this project |
| --- | --- |
| **All-in** | A player who has put every chip in. Still in the hand, can win pots they contributed to, cannot act again. `Player::all_in`. |
| **Big blind / small blind** | Forced bets posted before cards are seen. Positions in `Table::big_blind_id` / `small_blind_id`. |
| **Burn card** | Card discarded face-down before each community deal. |
| **Button / dealer** | `Table::dealer_id`; moves to the next funded seat after each hand. |
| **Checksum** | Order-independent hash sum of simulation results, used to confirm determinism across worker counts. |
| **Contribution (round / total)** | Chips a player paid this betting round / this whole hand. Tracked by `PotManager`. |
| **Current bet** | Round contribution everyone must match to stay in. `BettingRoundState::current_bet`. |
| **Decision provider** | Anything that chooses actions from a `PlayerView`: bot, human adapter, model wrapper. |
| **Dense score** | Fast evaluator's `uint16_t` strength, 1..7462. |
| **Episode** | One independently reset game or tournament in a simulation, with its own derived seed. |
| **Equity** | Expected share of the pot at showdown (ties count fractionally). Not the same as win probability or expected profit. |
| **Freezeout** | Tournament with no re-entry; elimination is final. |
| **Full raise** | A raise of at least the minimum raise size. Reopens betting. |
| **Heads-up** | Two players. Dealer posts the small blind. |
| **ICM** | Independent Chip Model: converts stacks to expected prize money by assigning places in proportion to stacks. |
| **Legal actions** | `LegalActions`: what the acting player may do now, with call amount and raise bounds. |
| **Minimum raise size** | Increase needed for a full raise; starts at the big blind, then equals the last full raise's size. |
| **Observer** | Trusted listener that sees the full table (`GameObserver`, `TournamentObserver`). |
| **Packed score** | `HandScore` (`uint32_t`) with category and kickers in 4-bit fields. |
| **Payout vs. reward** | Payout = gross chips received in a hand; reward = net change from before the blinds (sums to zero). |
| **PlayerView** | Everything one player may legally see when deciding. |
| **Range** | A set of possible hole-card combos assumed for an opponent (`OpponentRange`). |
| **RaiseTo** | Raise expressed as the **total** round contribution, not the increment. |
| **Short all-in** | All-in raise smaller than a full raise. Raises the call amount but does not reopen raising. |
| **Side pot** | Pot layer only some players can win, created when all-in players contributed different amounts. |
| **Snapshot** | Opaque copy of a `PokerGame`'s full state for branching and replay. |
| **Street** | Betting round: PreFlop, Flop, Turn, River. |
