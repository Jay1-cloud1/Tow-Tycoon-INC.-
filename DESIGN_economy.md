# TowTycoon → Economic Tycoon (Coffee Inc.–style) Design & Roadmap

This is the design plan for turning TowTycoon from an arcade tow loop into a
deep business/economy sim in the spirit of **Coffee Inc 2**, adapted to a
towing empire. Keep it next to the project; paste sections into an AI when you
want code or art for a specific system.

---

## 1. What Coffee Inc 2 actually does (the systems worth copying)

From its design (verified from the App Store listing + reviews):

- **Separate personal vs. company finances.** You pay yourself; the company has its own books.
- **Real financial statements:** income statement (P&L), balance sheet, cash flow. Insolvency → warning period → bankruptcy.
- **Loans / financing** to fund expansion before you're profitable.
- **Pricing & products:** you set prices; demand responds. Launch new products/services.
- **Marketing** raises brand awareness → more demand / market share.
- **AI rival companies** that compete for market position and can also go public.
- **Market share** matters (even city laws key off it).
- **Properties/real estate** across cities; interiors/equipment; real estate as an investment.
- **Departments & staff:** HR, Finance, Marketing, Products, Engineering, Executive, Investment. Hire staff + C-level execs; delegate daily ops to managers.
- **Board of directors:** sets exec pay, approves dividends, can reject the CEO.
- **Stock market / investment division:** invest in stocks; **go public (IPO)** via investment banks; **M&A buyouts** of rivals.
- **City laws/economy:** min wage, taxes, regulations → fines if ignored.

**The core loop that makes it addictive:** earn → reinvest → grow market share →
beat rivals → raise capital (loans, then IPO) → acquire rivals → dominate.

---

## 2. How it maps to TowTycoon

| Coffee Inc concept | TowTycoon equivalent |
|---|---|
| Cafés in cities | **Tow yards / lots** across the neighborhood, then the city |
| A café's local customers | A building's **service lot** (who it calls for tows) |
| Menu pricing | **Per-tow / contract rates** you set or negotiate |
| Products | Service tiers: roadside, light, medium, heavy, repossession, emergency |
| Marketing / brand | **Reputation** per customer type (dealers, property mgrs, police, insurers) |
| Market share | **% of the neighborhood's lots you control** vs. the AI competitor |
| Rival coffee cos | **One AI towing competitor** (then more) bidding against you |
| Staff / managers | **Drivers, dispatchers, mechanics, yard managers** (passive ops) |
| Loans | Start the game by **taking a loan** to buy your first rig + lot |
| Stock market / IPO | Company valuation → **go public**, sell shares, raise capital |
| M&A buyout | **Buy out the competitor** to win the neighborhood |

**Win condition:** control the neighborhood (own/contract the majority of lots)
AND either bankrupt or buy out your competitor. Then expand to the next district.

---

## 3. The headline feature you asked for: lots & bidding

Every building has an adjacent **lot** (the space beside/behind it). A lot is a
**towing contract territory**: whoever holds it gets that building's tow calls.

**Bidding loop (the heart of the economy):**
1. A lot opens for bid (new, or an expiring contract).
2. You and the **AI competitor** submit sealed bids. A bid = the cut you take
   (lower rate to the property = more attractive) **plus** your reputation and
   response-time record.
3. Winner holds the contract for N in-game weeks and receives its call volume.
4. Poor service (slow, damaged vehicle, missed calls) → you can **lose** the lot
   at renewal to the competitor.

**Why this creates economy depth:** bidding too low wins lots but kills margin;
bidding high protects margin but cedes territory and market share. The AI adjusts
its aggressiveness based on its cash and how much territory it holds — so you're
always balancing growth vs. profitability, exactly like Coffee Inc.

**Neighborhood takeover meter:** `lots_you_hold / total_lots` vs. the competitor's.
Shown as a bar up top. Hitting, say, 60% unlocks the option to attempt a buyout.

---

## 4. The economy you "control"

You influence a simulated market rather than scripting fixed prices:

- **Demand per lot** = base_calls × (area_density) × (economic_cycle) × (weather) × (season).
- **Your win odds on a bid** = f(your_rate_offer, reputation, current_capacity, response_time_history) vs. competitor's.
- **Rate you charge** sets margin; too high and property managers defect at renewal.
- **Reputation** rises with on-time, damage-free tows; falls with failures.
- **Economic cycle** (boom/recession) scales call volume and auction/vehicle prices — a lever you can ride (expand in booms, hoard cash before busts).
- Later: **you** can nudge the economy as a dominant player (set regional rates, lobby for impound laws) — the "control the economy" fantasy, earned once you're big.

---

## 5. Finances, loans, stock market, IPO, buyout

**Loans (start of game):** choose a starter loan (e.g., $5k–$25k) at an interest
rate set by your (nonexistent) credit. Monthly payments. Missed payments → repo.

**Books:** track Cash, Revenue, Expenses (fuel, wages, loan interest, lot fees,
taxes), monthly P&L, and **Company Valuation** = f(assets + contracted revenue −
debt). Personal account is separate; pay yourself an owner draw.

**Stock market / IPO:**
- Valuation grows with fleet, lots held, and recurring contract revenue.
- At a valuation threshold, an **investment bank** offers an IPO: sell X% of
  shares for a cash injection, in exchange for a **board** that expects dividends/growth.
- A **share price** tracks your performance each period. You can buy back shares,
  issue more, or **buy shares of the competitor**.

**Buyout:** once you hold enough of the competitor's shares (or they're near
insolvency), trigger an acquisition → absorb their lots and fleet → win the district.

---

## 6. Data model sketch (C++)

```cpp
struct Lot        { int buildingIndex; int owner; /*0 you,1 rival,-1 open*/ float callRate; float contractWeeksLeft; float yourRate; };
struct Competitor { int cash; float reputation; int lotsHeld; float aggression; /*bid behavior*/ };
struct Loan       { int principal; float apr; int weeksLeft; int weeklyPayment; };
struct Company    { int cashBusiness; int cashPersonal; float reputationByType[5];
                    long valuation; bool isPublic; float sharePrice; float sharesOutstanding; float youOwnPct; };
struct MarketState{ float economicCycle; /*0.7..1.3*/ int week; int season; float weather; };
```

Jobs already have **class tiers** (regular/medium/heavy) and trucks have
**capability + capacity** (done). Contracts feed jobs of the right class to the
lot's territory.

---

## 7. Build order (dependency-aware; each a real, testable step)

1. **Save/Load + in-game clock (weeks/months).** *Everything below needs persistence and time passing.* ← do this first.
2. **Lots layer:** attach a lot to each building; render ownership tint; a lot generates calls for its holder.
3. **One AI competitor + sealed-bid auctions** for open/expiring lots; takeover meter.
4. **Finances:** loans at game start, weekly expenses, P&L screen, company valuation.
5. **Reputation & rates:** set your rate per contract; reputation affects bid wins and renewals.
6. **Economic cycle + weather/season** scaling demand and prices.
7. **Stock market & IPO:** valuation → go public → share price → buy competitor shares.
8. **Buyout & district win;** then unlock the next district (scales to "control the economy").

---

## 8. AI prompts to generate what each step needs

### 8a. Art (Higgsfield / Gemini) — keep top-down, nose-up, transparent, no text
```
Top-down bird's-eye pixel-art sprite, facing straight up, centered, orthographic,
transparent background, no shadow, no text, 16-bit game asset, consistent scale.
NEED:
- 6 rival tow trucks in a DARK/BLACK livery (same 6 types as mine) for the AI competitor
- a "contract/for-bid" lot marker: a cordoned gravel lot pad, 1 tile, top-down
- UI icons (each separate, transparent): gavel (bid), handshake (contract),
  dollar stack (loan), bank building, stock chart arrow, trophy (district won)
```

### 8b. Economy formulas — paste into an AI to get tunable code
```
I'm building a towing business sim in C++/SDL2. Design the economic simulation as
pure, deterministic, unit-testable functions (no globals), with tunable constants
at the top. Give me:
1) demand_for_lot(base, density, economicCycle, weather, season) -> weekly calls
2) bid_win_probability(myRate, myRep, myCapacity, myResponseScore, rivalRate, rivalRep) -> 0..1
3) rival_bid(rivalCash, rivalLotsHeld, lotValue, aggression) -> rate offer
4) company_valuation(assetsValue, weeklyContractRevenue, debt) -> number
5) share_price_update(prevPrice, lastPeriodProfit, growthRate, sentiment) -> newPrice
Keep every function side-effect free and give me a small main() that prints a
10-week simulation so I can see the numbers before wiring it into the game.
```

### 8c. Competitor AI behavior
```
Write a deterministic AI "towing competitor" for a C++ sim. State: cash, reputation,
lotsHeld, aggression (0..1). Each week it: picks which open lots to bid on (bounded
by cash and capacity), sets a bid rate via a simple rule (undercut when flush and
behind on territory; protect margin when low on cash), services its lots, and
updates reputation. No randomness except a single seeded RNG passed in. Expose
decide_bids(state, openLots, rng) and end_week(state). Include a tiny test harness.
```

### 8d. Save/Load (do first)
```
Add save/load to a C++/SDL2 game. Serialize the whole game state (cash, fleet of
owned trucks, driven slot, drivers, tows, goal, jobs, lots, competitor, loans,
company финances, market week) to a simple versioned text/JSON file next to the exe
(savegame.tt). Provide save(path,state) and load(path,state) with a version field
and graceful handling of older/missing fields. Autosave each in-game week and on quit.
```

---

## 9. Balancing guardrails (so it's not too easy or a grind)

- Starter loan forces early pressure; fleet revenue comes from dispatched AI
  trucks completing jobs rather than automatic per-second payments.
- Bidding margins thin as the competitor gets desperate → late game is about
  capital (IPO) not grinding tows.
- Heavy jobs and premium contracts are the high-reward path that needs big trucks
  and good reputation — ties the whole progression together.

---

*Current status (built & verified in-engine):* drivable fleet with real art;
job tiers (regular/medium/heavy) with class-gated trucks; flatbeds carry 2;
marketplace that spends cash and changes your truck; dispatched AI fleet jobs;
Roadside Van service calls; varied dispatches + a goal tracker. **Next recommended:** Save/Load + clock (section 7, step 1).
```
