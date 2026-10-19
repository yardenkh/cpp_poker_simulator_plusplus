# C++ Texas Hold'em Poker Engine — Implementation Plan

Build a readable, correct, reasonably fast C++17 No-Limit Texas Hold'em engine for 2–8 players.

Priority:

```text
correctness > readability > simplicity > testability > performance
```

Keep this boundary:

```text
Poker rules/state -> PlayerView -> DecisionProvider -> Human/AI
```

The engine validates and executes actions. Decision providers only choose actions.

---

# 1. Project structure

Create files as their task is reached:

```text
poker/
├── CMakeLists.txt
├── main.cpp
├── config/PokerConfig.hpp
├── card/Card.hpp
├── card/Card.cpp
├── deck/Deck.hpp
├── deck/Deck.cpp
├── player/Player.hpp
├── player/Player.cpp
├── evaluation/HandEvaluator.hpp
├── evaluation/HandEvaluator.cpp
├── table/Table.hpp
├── table/Table.cpp
├── pot/Pot.hpp
├── pot/PotManager.hpp
├── pot/PotManager.cpp
├── betting/Betting.hpp
├── betting/Betting.cpp
├── view/PlayerView.hpp
├── view/PlayerView.cpp
├── decision/DecisionProvider.hpp
├── decision/AIDecisionProvider.hpp
├── decision/AIDecisionProvider.cpp
├── decision/HumanDecisionProvider.hpp
├── decision/HumanDecisionProvider.cpp
├── game/GameObserver.hpp
├── game/PokerGame.hpp
├── game/PokerGame.cpp
├── ui/TerminalUI.hpp
├── ui/TerminalUI.cpp
└── tests/PokerTests.cpp
```

All engine code:

```cpp
namespace poker {

// ...

}
```

CMake targets:

```text
poker_engine
poker_cli
poker_tests
```

Use C++17 and warnings:

```text
-Wall -Wextra -Wpedantic
```

or `/W4`.

---

# 2. Config

## Create `config/PokerConfig.hpp`

```cpp
inline constexpr int MAX_PLAYERS = 8;
inline constexpr int DECK_SIZE = 52;
inline constexpr int HOLE_CARDS_PER_PLAYER = 2;
inline constexpr int MAX_COMMUNITY_CARDS = 5;

inline constexpr int DEFAULT_STARTING_CHIPS = 2500;
inline constexpr int DEFAULT_SMALL_BLIND = 50;
inline constexpr int DEFAULT_BIG_BLIND = 100;

struct GameConfig {
    int starting_chips = DEFAULT_STARTING_CHIPS;
    int small_blind = DEFAULT_SMALL_BLIND;
    int big_blind = DEFAULT_BIG_BLIND;
};
```

`PokerGame` will later validate:

```text
starting_chips > 0
small_blind > 0
big_blind > 0
small_blind < big_blind
```

---

# 3. Card

## Create `card/Card.hpp`

```cpp
enum class Suit {
    Hearts,
    Diamonds,
    Clubs,
    Spades
};

enum class Face : int {
    Two = 2,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
    Ace
};

struct Card {
    Face face = Face::Two;
    Suit suit = Suit::Clubs;
};

std::string face_to_string(Face face);
std::string suit_to_string(Suit suit);
std::string card_to_string(const Card& card);

bool operator==(const Card& lhs, const Card& rhs);
bool operator!=(const Card& lhs, const Card& rhs);
```

## Create `card/Card.cpp`

Implement all functions.

Card strings:

```text
Ah
Ks
10d
2c
```

## Verify

- equality works;
- all faces/suits stringify correctly.

---

# 4. Deck

## Create `deck/Deck.hpp`

```cpp
class Deck {
public:
    Deck();

    void reset();
    void shuffle(std::mt19937& random_generator);

    Card deal_card();
    void burn_card();

    int cards_remaining() const;
    int cards_dealt() const;

private:
    std::array<Card, DECK_SIZE> cards_{};
    int next_card_index_ = 0;
};
```

## Create `deck/Deck.cpp`

Implement:

```text
reset:
    rebuild all 52 cards
    next_card_index = 0

shuffle:
    std::shuffle(...)
    next_card_index = 0

deal_card:
    return next card
    advance index

burn_card:
    advance index
```

Throw `std::runtime_error` on deck exhaustion.

Do not erase cards from the array.

## Verify

- 52 unique cards;
- reset works;
- same RNG seed gives same shuffle;
- 53rd deal fails.

---

# 5. Player

## Create `player/Player.hpp`

```cpp
enum class PlayerType {
    Human,
    AI
};

struct Player {
    std::array<Card, HOLE_CARDS_PER_PLAYER> hand{};
    int cards_in_hand = 0;

    int cash = 0;
    PlayerType type = PlayerType::AI;

    bool in_hand = false;
    bool all_in = false;

    void reset_for_new_hand();
    void receive_card(const Card& card);

    bool is_eliminated() const;
};
```

## Create `player/Player.cpp`

Implement:

```text
reset_for_new_hand:
    cards_in_hand = 0
    all_in = false
    in_hand = cash > 0

receive_card:
    add card
    reject third card

is_eliminated:
    cash == 0
```

Meaning:

```text
folded: in_hand == false
all-in: in_hand == true && all_in == true
eliminated: cash == 0 after hand
```

---

# 6. Five-card evaluator

## Create `evaluation/HandEvaluator.hpp`

```cpp
enum class HandRank {
    HighCard,
    OnePair,
    TwoPair,
    ThreeOfAKind,
    Straight,
    Flush,
    FullHouse,
    FourOfAKind,
    StraightFlush
};

using HandScore = std::uint32_t;

HandScore evaluate_five_cards(
    const std::array<Card, 5>& cards
);

HandRank hand_rank_from_score(HandScore score);

std::string hand_rank_to_string(HandRank rank);
std::string hand_score_to_string(HandScore score);
```

## Create `evaluation/HandEvaluator.cpp`

Use a packed score where:

```text
larger HandScore == better hand
```

Recommended structure:

```text
[rank][value1][value2][value3][value4][value5]
```

Possible packing:

```cpp
(rank_value << 20)
| (value1 << 16)
| (value2 << 12)
| (value3 << 8)
| (value4 << 4)
| value5
```

Implement:

```text
High Card
One Pair
Two Pair
Three of a Kind
Straight
Flush
Full House
Four of a Kind
Straight Flush
```

Treat Royal Flush as Ace-high Straight Flush.

Support:

```text
A 2 3 4 5
```

as 5-high straight.

No heap allocation in evaluator.

## Verify

Test every rank and same-rank kicker comparisons.

---

# 7. Seven-card evaluator

## Modify `evaluation/HandEvaluator.hpp`

Add:

```cpp
HandScore evaluate_seven_cards(
    const std::array<Card, 7>& cards
);
```

## Modify `evaluation/HandEvaluator.cpp`

Enumerate all 21 five-card subsets.

Call:

```cpp
evaluate_five_cards(...)
```

Return maximum score.

Do not optimize further until profiling says it matters.

---

# 8. Table

## Create `table/Table.hpp`

```cpp
struct Table {
    int num_players = 0;

    std::array<Player, MAX_PLAYERS> players{};

    Deck deck;

    std::array<Card, MAX_COMMUNITY_CARDS> community_cards{};
    int community_card_count = 0;

    int dealer_id = -1;
    int small_blind_id = -1;
    int big_blind_id = -1;
};

void table_init(
    Table& table,
    int num_players,
    int starting_chips
);

int count_players_with_chips(const Table& table);
int count_players_in_hand(const Table& table);
int count_players_able_to_act(const Table& table);

int find_only_remaining_player(const Table& table);

int next_player_with_chips(
    const Table& table,
    int player_id
);

int next_player_in_hand(
    const Table& table,
    int player_id
);

int next_player_able_to_act(
    const Table& table,
    int player_id
);
```

## Create `table/Table.cpp`

Implement these by scanning at most 8 seats.

Do not add cached active-player arrays.

---

# 9. Dealer, blinds positions, dealing

## Modify `table/Table.hpp`

Add:

```cpp
void table_prepare_new_hand(
    Table& table,
    std::mt19937& random_generator
);

void rotate_dealer(Table& table);
void assign_blinds(Table& table);

void deal_hole_cards(Table& table);

void deal_flop(Table& table);
void deal_turn(Table& table);
void deal_river(Table& table);
```

## Modify `table/Table.cpp`

Rules:

### 3+ players

```text
dealer
next player with chips -> small blind
next player with chips -> big blind
```

### Heads-up

```text
dealer = small blind
other player = big blind
```

Hole cards:

```text
one card around table
second card around table
```

Community cards:

```text
flop:  burn 1, deal 3
turn:  burn 1, deal 1
river: burn 1, deal 1
```

Maintain `community_card_count`.

Verify heads-up, eliminated-seat skipping, dealing order, and burn counts.

---

# 10. PotManager: contribution accounting

## Create `pot/Pot.hpp`

```cpp
struct Pot {
    int amount = 0;
    std::array<bool, MAX_PLAYERS> eligible_players{};
};
```

## Create `pot/PotManager.hpp`

```cpp
class PotManager {
public:
    PotManager();

    void reset_for_new_hand();
    void reset_round_contributions();

    int contribute(
        Table& table,
        int player_id,
        int requested_amount
    );

    int total_pot_amount() const;
    int total_contribution(int player_id) const;
    int round_contribution(int player_id) const;

    void build_pots(const Table& table);

    std::array<int, MAX_PLAYERS> distribute_pots(
        Table& table,
        const std::array<HandScore, MAX_PLAYERS>& hand_scores
    );

    std::array<int, MAX_PLAYERS> award_entire_pot(
        Table& table,
        int winner_id
    );

    int num_pots() const;
    const Pot& pot(int pot_index) const;

private:
    std::array<int, MAX_PLAYERS> total_contributions_{};
    std::array<int, MAX_PLAYERS> round_contributions_{};

    std::array<Pot, MAX_PLAYERS> pots_{};
    int num_pots_ = 0;
};
```

## Create `pot/PotManager.cpp`

First implement:

```text
reset_for_new_hand
reset_round_contributions
contribute
total_pot_amount
total_contribution
round_contribution
```

All chips entering a pot must go through:

```cpp
PotManager::contribute(...)
```

`contribute()`:

```text
actual = min(requested_amount, player.cash)
player.cash -= actual
total_contributions[player] += actual
round_contributions[player] += actual

if player.cash == 0:
    player.all_in = true
```

Never manually subtract betting chips elsewhere.

---

# 11. Side pots

## Modify `pot/PotManager.cpp`

Implement:

```cpp
void PotManager::build_pots(const Table& table);
```

Algorithm:

1. collect unique positive contribution levels;
2. sort ascending;
3. create one layer per level.

For each level:

```text
layer = current_level - previous_level
contributors = players with contribution >= current_level
pot.amount = layer * contributors
```

Eligible player:

```text
contribution >= current_level
AND
player.in_hand
```

Folded chips remain in pots, but folded players are not eligible.

Example:

```text
P0 = 100
P1 = 300
P2 = 500

pots:
300
400
200
```

Verify equal stacks, one side pot, multiple side pots, folded contributors, unmatched final layer.

---

# 12. Pot payouts

## Modify `pot/PotManager.cpp`

Implement:

```cpp
std::array<int, MAX_PLAYERS> PotManager::distribute_pots(
    Table& table,
    const std::array<HandScore, MAX_PLAYERS>& hand_scores
);

std::array<int, MAX_PLAYERS> PotManager::award_entire_pot(
    Table& table,
    int winner_id
);
```

For every pot:

```text
find best eligible score
find all tied winners
split amount
distribute remainder chips
```

Odd chips:

```text
start with first tied winner clockwise after dealer
```

Always preserve:

```text
sum(player stacks) + committed chips
```

---

# 13. Betting types

## Create `betting/Betting.hpp`

```cpp
enum class PokerStage {
    PreFlop,
    Flop,
    Turn,
    River,
    Showdown
};

std::string poker_stage_to_string(PokerStage stage);

enum class ActionType {
    Fold,
    Check,
    Call,
    RaiseTo,
    AllIn
};

struct PlayerDecision {
    ActionType action = ActionType::Fold;

    // Used only for RaiseTo.
    int raise_to = 0;
};

struct LegalActions {
    bool can_fold = false;
    bool can_check = false;
    bool can_call = false;
    bool can_raise = false;
    bool can_all_in = false;

    int amount_to_call = 0;
    int min_raise_to = 0;
    int max_raise_to = 0;
};

struct BettingRoundState {
    int current_bet = 0;
    int minimum_raise_size = 0;

    std::array<bool, MAX_PLAYERS>
        acted_since_last_full_raise{};
};

void betting_state_reset(
    BettingRoundState& state,
    int current_bet,
    int minimum_raise_size
);

int amount_to_call(
    const PotManager& pot_manager,
    const BettingRoundState& betting_state,
    int player_id
);

LegalActions get_legal_actions(
    const Table& table,
    const PotManager& pot_manager,
    const BettingRoundState& betting_state,
    int player_id
);

bool validate_decision(
    const Table& table,
    const PotManager& pot_manager,
    const BettingRoundState& betting_state,
    int player_id,
    const PlayerDecision& decision
);

void execute_decision(
    Table& table,
    PotManager& pot_manager,
    BettingRoundState& betting_state,
    int player_id,
    const PlayerDecision& decision
);

bool player_needs_action(
    const Table& table,
    const PotManager& pot_manager,
    const BettingRoundState& betting_state,
    int player_id
);

bool betting_round_complete(
    const Table& table,
    const PotManager& pot_manager,
    const BettingRoundState& betting_state
);

int next_player_needing_action(
    const Table& table,
    const PotManager& pot_manager,
    const BettingRoundState& betting_state,
    int after_player_id
);
```

Create `betting/Betting.cpp`.

Keep `current_bet` here, not in `PotManager`.

---

# 14. Check / call / fold

## Implement in `betting/Betting.cpp`

Amount to call:

```text
current_bet - round_contribution
```

clamped to zero.

Rules:

```text
Check:
    legal when amount_to_call == 0

Call:
    legal when amount_to_call > 0
    may become partial all-in call

Fold:
    player.in_hand = false
    existing contribution remains
```

Implement these first in:

```cpp
get_legal_actions(...)
validate_decision(...)
execute_decision(...)
```

Verify legal/illegal checks, normal calls, all-in calls, and folds.

---

# 15. Full raises

## Continue `betting/Betting.cpp`

`RaiseTo` means:

```text
make current-round contribution equal to target
```

not:

```text
add target chips
```

Example:

```text
current bet = 100
minimum raise size = 100
```

minimum raise target:

```text
200
```

If raised:

```text
100 -> 300
```

raise size is:

```text
200
```

therefore:

```text
minimum_raise_size = 200
next minimum target = 500
```

Normal full raise requires:

```text
target > current_bet
target - current_bet >= minimum_raise_size
player can afford target
```

Full raise:

```text
updates current_bet
updates minimum_raise_size
reopens betting
```

---

# 16. All-in and short raises

## Continue `betting/Betting.cpp`

`AllIn` contributes all remaining cash.

Classify result:

```text
new contribution < current bet
    -> partial all-in call

new contribution == current bet
    -> all-in call

new contribution > current bet
but increase < minimum_raise_size
    -> short all-in raise

increase >= minimum_raise_size
    -> full all-in raise
```

Short raise:

```text
updates current_bet
does not update minimum_raise_size
does not reopen raising for players that already acted
```

Those players may still need to call/fold.

Use:

```cpp
acted_since_last_full_raise[player_id]
```

to implement this correctly.

This rule deserves dedicated tests.

---

# 17. Betting-round completion

## Finish `betting/Betting.cpp`

Implement:

```cpp
player_needs_action(...)
betting_round_complete(...)
next_player_needing_action(...)
```

A round is complete when every non-folded, non-all-in player:

```text
has acted since last full raise
AND
has matched current bet
```

Special case:

If only one non-all-in player remains:

```text
owes chips -> final call/fold decision
owes nothing -> round ends
```

Do not allow meaningless betting when every opponent is all-in.

---

# 18. PlayerView

## Create `view/PlayerView.hpp`

```cpp
struct PlayerView {
    int player_id = -1;
    int num_players = 0;

    PokerStage stage = PokerStage::PreFlop;

    std::array<Card, HOLE_CARDS_PER_PLAYER> hole_cards{};
    int hole_card_count = 0;

    std::array<Card, MAX_COMMUNITY_CARDS> community_cards{};
    int community_card_count = 0;

    std::array<int, MAX_PLAYERS> player_stacks{};
    std::array<bool, MAX_PLAYERS> players_in_hand{};
    std::array<bool, MAX_PLAYERS> players_all_in{};

    std::array<int, MAX_PLAYERS> round_contributions{};
    std::array<int, MAX_PLAYERS> total_contributions{};

    int dealer_id = -1;
    int small_blind_id = -1;
    int big_blind_id = -1;

    int pot_amount = 0;
    int current_bet = 0;

    LegalActions legal_actions;
};

PlayerView make_player_view(
    const Table& table,
    const PotManager& pot_manager,
    const BettingRoundState& betting_state,
    PokerStage stage,
    int player_id
);
```

## Create `view/PlayerView.cpp`

Expose:

```text
acting player's hole cards
community cards
stacks
fold/all-in state
contributions
positions
pot
current bet
legal actions
```

Never expose opponents' hole cards.

Do not put AI-derived fields such as equity or bluff scores here.

---

# 19. DecisionProvider

## Create `decision/DecisionProvider.hpp`

```cpp
class DecisionProvider {
public:
    virtual ~DecisionProvider() = default;

    virtual PlayerDecision choose_action(
        const PlayerView& view
    ) = 0;
};
```

This is the only interface future player logic needs to implement.

---

# 20. Simple AI

## Create `decision/AIDecisionProvider.hpp`

```cpp
class AIDecisionProvider final : public DecisionProvider {
public:
    PlayerDecision choose_action(
        const PlayerView& view
    ) override;
};
```

## Create `decision/AIDecisionProvider.cpp`

Use deterministic placeholder logic:

```text
if check is legal:
    check
else if call is legal and amount_to_call <= 25% of stack:
    call
else:
    fold
```

No intentional raising.

This exists only so full games can run automatically.

---

# 21. GameObserver

## Create `game/GameObserver.hpp`

```cpp
class GameObserver {
public:
    virtual ~GameObserver() = default;

    virtual void on_hand_started(
        int hand_number,
        const Table& table,
        const PotManager& pot_manager
    ) {}

    virtual void on_stage_started(
        PokerStage stage,
        const Table& table,
        const PotManager& pot_manager
    ) {}

    virtual void on_player_action(
        int player_id,
        const PlayerDecision& decision,
        const Table& table,
        const PotManager& pot_manager,
        const BettingRoundState& betting_state
    ) {}

    virtual void on_showdown(
        const Table& table,
        const PotManager& pot_manager,
        const std::array<HandScore, MAX_PLAYERS>& hand_scores
    ) {}

    virtual void on_hand_finished(
        const Table& table,
        const std::array<int, MAX_PLAYERS>& payouts
    ) {}
};
```

No `.cpp` needed.

The game must work with:

```cpp
observer_ == nullptr
```

and produce no output.

---

# 22. PokerGame

## Create `game/PokerGame.hpp`

```cpp
class PokerGame {
public:
    PokerGame(
        int num_players,
        const GameConfig& config = GameConfig{}
    );

    void set_random_seed(std::uint32_t seed);

    void set_decision_provider(
        int player_id,
        DecisionProvider& provider
    );

    void set_observer(GameObserver* observer);

    void play_one_hand();
    void run_match(int max_hands = 0);

    bool match_finished() const;
    int winner_id() const;

    const Table& table() const;

private:
    GameConfig config_;
    Table table_;
    PotManager pot_manager_;
    BettingRoundState betting_state_;

    PokerStage current_stage_ = PokerStage::PreFlop;

    std::array<DecisionProvider*, MAX_PLAYERS>
        decision_providers_{};

    GameObserver* observer_ = nullptr;

    std::mt19937 random_generator_;
    int hand_number_ = 0;

    void start_new_hand();
    void post_blinds();

    bool run_betting_round(PokerStage stage);

    int first_player_to_act(PokerStage stage) const;
    int next_player_needing_action(int after_player_id) const;

    PlayerView create_player_view(int player_id) const;

    void finish_uncontested_hand(int winner_id);
    void run_showdown();
};
```

## Create `game/PokerGame.cpp`

Implement first:

```text
constructor
set_random_seed
set_decision_provider
set_observer
match_finished
winner_id
table
```

Validate player count and `GameConfig`.

`PokerGame` owns the RNG.

Decision providers are non-owning pointers and must outlive the game.

---

# 23. Start hand + post blinds

## Implement in `game/PokerGame.cpp`

### `start_new_hand()`

```text
verify at least 2 players have chips
increment hand number
reset PotManager
prepare table
reset/shuffle deck
assign blinds
deal hole cards
notify observer
```

### `post_blinds()`

Use:

```cpp
pot_manager_.contribute(...)
```

for both blinds.

If a blind exceeds the stack, contribute the remaining stack.

After posting:

```text
current_bet = largest actual blind contribution
minimum_raise_size = configured big blind
```

---

# 24. Action order

## Implement in `game/PokerGame.cpp`

```cpp
int PokerGame::first_player_to_act(
    PokerStage stage
) const;

int PokerGame::next_player_needing_action(
    int after_player_id
) const;
```

Rules:

```text
Pre-flop:
    first eligible player clockwise after big blind

Flop/Turn/River:
    first eligible player clockwise after dealer
```

Heads-up:

```text
Pre-flop:
    dealer/small blind acts first

Post-flop:
    big blind acts first
```

---

# 25. Betting-round loop

## Implement in `game/PokerGame.cpp`

```cpp
bool PokerGame::run_betting_round(
    PokerStage stage
);
```

Flow:

```text
find first player needing action

while round incomplete:
    create PlayerView
    ask player's DecisionProvider
    validate decision
    execute decision
    notify observer

    if one player remains:
        return false

    find next player needing action

return true
```

Invalid provider decisions should fail clearly during development, not be silently converted.

Use scripted providers in tests to force exact action sequences.

---

# 26. Player hand evaluation

## Modify `evaluation/HandEvaluator.hpp`

Add:

```cpp
HandScore evaluate_player_hand(
    const Table& table,
    int player_id
);
```

## Modify `evaluation/HandEvaluator.cpp`

Build:

```text
2 hole cards + 5 board cards
```

and call:

```cpp
evaluate_seven_cards(...)
```

---

# 27. Showdown + uncontested finish

## Implement in `game/PokerGame.cpp`

### `run_showdown()`

```text
evaluate each non-folded player
build pots
notify observer
distribute pots
notify observer of payouts
```

### `finish_uncontested_hand(int winner_id)`

```text
award entire pot to winner
do not evaluate hands
notify observer
```

---

# 28. Complete `play_one_hand()`

## Implement in `game/PokerGame.cpp`

Flow:

```text
start hand
post blinds

pre-flop betting
if one remains -> award pot

flop
reset round contributions
bet
if one remains -> award pot

turn
reset round contributions
bet
if one remains -> award pot

river
reset round contributions
bet
if one remains -> award pot

showdown

rotate dealer
```

If remaining players are all-in and no decision is needed:

```text
deal remaining streets automatically
go to showdown
```

Still apply normal burn-card rules.

---

# 29. Match loop

## Implement in `game/PokerGame.cpp`

```cpp
void PokerGame::run_match(int max_hands);
```

Rules:

```text
max_hands <= 0:
    run until one player has chips

max_hands > 0:
    stop at max_hands or winner
```

Players with zero chips are skipped in future hands.

Same seed + same deterministic providers should reproduce the same game.

---

# 30. Terminal UI

Implemented: `TerminalUI` displays public events and the acting player's view,
parses all five commands, retries invalid requests using the engine validator,
and stops cleanly when terminal input closes. Opponents' cards are displayed only
when they remain in the hand at showdown. Stream-based tests cover this behavior.

## Create `ui/TerminalUI.hpp`

```cpp
class TerminalUI final : public GameObserver {
public:
    PlayerDecision request_decision(
        const PlayerView& view
    );

    void print_player_view(
        const PlayerView& view
    ) const;

    // Override GameObserver callbacks here.
};
```

Use the exact `GameObserver` callback signatures.

## Create `ui/TerminalUI.cpp`

Support:

```text
fold
check
call
raise 500
allin
```

Convert:

```text
raise 500
```

to:

```cpp
PlayerDecision{
    ActionType::RaiseTo,
    500
};
```

The UI displays legal actions but does not implement legality rules.

---

# 31. HumanDecisionProvider

Implemented: the human provider borrows a `TerminalUI` and delegates action input
to it. The engine still validates and executes the returned decision.

## Create `decision/HumanDecisionProvider.hpp`

```cpp
class HumanDecisionProvider final : public DecisionProvider {
public:
    explicit HumanDecisionProvider(
        TerminalUI& terminal_ui
    );

    PlayerDecision choose_action(
        const PlayerView& view
    ) override;

private:
    TerminalUI& terminal_ui_;
};
```

## Create `decision/HumanDecisionProvider.cpp`

`choose_action()` delegates to:

```cpp
terminal_ui_.request_decision(view);
```

---

# 32. `main.cpp`

Implemented: `poker_cli` reads a player count, assigns seat 0 to the human and
other seats to the simple AI, attaches the UI observer, and runs a match.
Terminal adapters build in `poker_terminal`, keeping headless simulation callers
independent of terminal input/output.

Keep `main.cpp` limited to setup:

```text
read player count
create TerminalUI
create HumanDecisionProvider
create AIDecisionProvider
create PokerGame

seat 0 -> human
other seats -> simple AI

set UI observer
run match
```

No poker rules here.

---

# 33. Required regression tests

Before real AI work, `tests/PokerTests.cpp` must cover:

### Deck

```text
52 unique cards
reset
shuffle reproducibility
exhaustion
```

### Evaluator

```text
every hand rank
wheel
kickers
7-card best subset
```

### Table

```text
dealer rotation
heads-up blinds
3+ blinds
seat skipping
hole-card dealing
burn cards
```

### Pots

```text
normal contribution
all-in contribution
one side pot
multiple side pots
folded money
ties
odd chips
chip conservation
```

### Betting

```text
check
call
fold
minimum raise
illegal small raise
all-in call
short all-in raise
full all-in raise
reopening rules
big blind option
round completion
heads-up action order
```

### PlayerView

```text
own hole cards visible
opponent hole cards absent
```

### Full hands

Use deterministic scripted providers for:

```text
everyone folds
normal showdown
pre-flop all-in
side-pot showdown
heads-up hand
```

### Determinism

```text
same seed + same providers + same start
=> same result
```

---

# 34. Headless performance check

After all correctness tests pass:

Run many hands with:

```text
AIDecisionProvider
observer = nullptr
Release build
```

Measure:

```text
hands / second
```

Then profile.

Do not optimize components just because they look slow.

Keep these basic performance rules:

```text
fixed arrays for fixed-size poker state
no heap allocation in 5-card evaluator
no terminal output in headless mode
one game RNG
no unnecessary full-Table copies
```

---

# 35. Freeze the engine boundary

Before starting the real AI, keep these outside the engine:

```text
Monte Carlo equity
opponent statistics
bluff estimates
feature vectors
Q-values
neural-network state
CFR state
AI raise-size abstractions
```

The engine supports arbitrary legal:

```cpp
PlayerDecision{
    ActionType::RaiseTo,
    target
};
```

The future AI may choose candidates such as:

```text
100-chip increments
1/3 pot
1/2 pot
3/4 pot
1 pot
1.5 pot
all-in
```

without changing `Betting.cpp`.

`DecisionProvider` receives `PlayerView`, never raw `Table`.

---

# 36. Implementation order

```text
1.  CMake + config
2.  Card
3.  Deck
4.  Player
5.  5-card evaluator
6.  7-card evaluator
7.  Table
8.  Dealer/blinds/dealing
9.  Pot contribution accounting
10. Side-pot construction
11. Pot payout
12. Betting types
13. Check/call/fold
14. Full raises
15. All-in + short raises
16. Betting-round completion
17. PlayerView
18. DecisionProvider
19. Simple AI
20. GameObserver
21. PokerGame skeleton
22. Hand setup + blinds
23. Action order
24. Betting-round loop
25. Player hand evaluation
26. Showdown
27. Complete hand flow
28. Match loop
29. Terminal UI
30. Human provider
31. main.cpp
32. Regression tests
33. Headless benchmark
34. Freeze engine API
35. Start real AI work
```
