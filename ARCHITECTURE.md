# TowTycoon — Architecture (OpenTTD-inspired, Coffee Inc.-deep)

Target architecture for growing TowTycoon into a living city + deep business
sim. **All code is original.** OpenTTD is GPL-licensed, so we take *ideas*
(living world, routing, rival companies) and **re-implement them ourselves** —
we never copy OpenTTD source. Coffee Inc. is the reference for the economic
depth (finances, market share, IPO, M&A).

Near-term architecture: **a modular monolith** with focused simulation rules,
platform services, and presentation kept separate. Systems may call one another
through explicit interfaces for now; add an event bus only when real coupling
requires it.

```
WorldSim ──interfaces──> Dispatch/Routing ──interfaces──> Economy ──> AI
   ^                                                              │
   └──────────────────── events (city reacts) ───────────────────┘
```

---

## Current engine map (what exists today)

| File | Role |
|---|---|
| `Engine.*` | window, renderer, textures, camera, frame timing, asset loading, save path |
| `GameState.h` | state interface (handleEvent/update/render) |
| `States.h` | `MenuState` + SDL-facing `WorldState` orchestration and presentation |
| `simulation/` | fixed-step clock, deterministic job generation, road pathfinding, player/rival finance rules |
| `persistence/SaveGameCodec.h` | versioned save serialization and state validation |
| `SaveStore.*` | bounded save reads and checked atomic writes |

`WorldState` already holds: tile city + buildings, drivable truck, job system
(tiered reg/med/heavy), lots + bidding, an AI rival (weekly pure simulation),
reputation, finances/loans, stock market/IPO/buyout, weather + time-of-day,
versioned text save/load (v6, with persisted deterministic RNG state,
legacy-save migration, OS user-data storage, and temporary-file replacement),
visible rain effects, street-named objective cues, and a HUD minimap.
Deterministic simulation rules and versioned persistence are extracted into
SDL-independent modules under `src/simulation/` and `src/persistence/`; the
UI-facing state currently maps those values to and from game entities.

The SDL window is resizable and uses a fixed 1280-pixel logical width with an
aspect-aware logical height for landscape displays. HUD/footer rows and menu
placement adapt to that height; nearest-neighbor renderer scaling preserves
the pixel-art aesthetic. This is a landscape layout adaptation, not a mobile
platform or touch-input implementation.

**Refactor direction:** keep extracting simulation rules and state ownership
from `WorldState` into focused modules. The event-driven diagram above remains
a possible later direction, not a prerequisite for this modular monolith.

The engine advances state through a 60 Hz fixed-step `update` interface;
rendering only draws. Opening any gameplay panel pauses world simulation,
including income, weather, job generation, and the in-game clock. The fixed
step drops wall-time beyond a 250 ms frame delta to avoid catch-up spirals;
replay determinism is defined over the same sequence of simulation ticks and
inputs, not arbitrary real-time frame schedules.

The executable resolves bundled assets relative to its executable directory
on every platform. MinGW runtime packaging resolves DLLs from the selected
compiler's directory rather than an MSYS2-specific path. Player income is
earned from completed player/AI dispatches rather than idle per-second accrual.
The Roadside Van can complete non-tow service calls paying $85-$150; the bank
lists active loan balances and supports partial or maximum affordable repayment.
Camera zoom is available through the mouse wheel and +/- keys. CTest covers RNG, viewport sizing, fixed-step replay of jobs, finances, rival
decisions and replay-scenario snapshots, save codec compatibility for
versions 1–6, plus checked storage reads and replacement writes.

---

## Layer 1 — World Simulation  *(OpenTTD's living towns/industries)*

Continuously updates and emits events the rest of the game reacts to.

| System | Status | Emits events |
|---|---|---|
| Weather (clear/rain/storm) | **built** — affects tow speed + call volume, mood overlay | `WeatherChanged`, `StormBegan` |
| Time-of-day + rush hours | **built** — drives dynamic call volume | `RushHourStarted`, `NightFell` |
| Breakdown generator | partial (jobs spawn) → make probabilistic on weather/age/zone | `VehicleBroke Down` |
| Police / city authority | planned — impounds, parking sweeps, DUI checkpoints | `ImpoundOrdered` |
| Neighborhood zones | planned — residential/commercial/industrial call patterns | `ZoneDemandShift` |

Next concrete step: tag each building/lot with a **zone type** and bias the
job class + payout + call rate by zone (industrial → more heavy, downtown →
more frequent).

## Layer 2 — Dispatch & Routing  *(OpenTTD's pathfinding)*

| System | Status | Notes |
|---|---|---|
| Job queue (urgency/distance/profit) | partial — jobs exist; add sort + priority | police calls override private |
| **A\* routing on the road grid** | **built** | extracted pure road-grid pathfinder with a replayable test |
| AI tow-truck entities that *drive* | **built** | player auto-truck and rival fleets route to jobs and yards |
| Fleet coordination | planned | don't send two trucks to one job |
| Dynamic reassignment | planned | re-route when stuck in traffic |

Next unlock here: coordinate fleets so multiple employed and rival trucks do
not claim the same active job.

## Layer 3 — Economy & Company Growth  *(Coffee Inc.)*

| System | Status |
|---|---|
| Revenue: private tows, contracts (lots) | **built** |
| Revenue: impounds, repo, storage, auctions, insurance | planned (new job/contract types) |
| Expenses: fuel/upkeep, wages, lot upkeep, loan interest | **built** |
| Reputation gating contracts | **built** (affects bid odds) |
| Licenses/permits unlock zones | planned |
| Fleet upgrades + yard expansion | partial (buy trucks) → add yards/capacity |
| Finances: P&L, valuation, loans | **built** |
| Stock market, IPO, buyout | **built** |

## Layer 4 — AI Competitors  *(OpenTTD's rival companies)*

| System | Status |
|---|---|
| One rival: weekly bidding, own books | **built** (pure weekly-economy simulation module) |
| **Personalities** (Aggressive / Reliable / Opportunistic / Corrupt) | **built** — influence bidding and truck behavior |
| Multiple rivals | planned — vector of competitors, each with its own color livery (art ready) |
| Build yards in strategic spots | planned (needs Layer 2 map awareness) |
| Poach drivers / undercut / react to events | planned (subscribe to WorldSim events) |

## Layer 5 — Map & Terrain  *(OpenTTD's generator)*

| System | Status |
|---|---|
| Procedural city (roads/blocks/buildings) | **built** (deterministic grid) |
| Seeded generation for shareable maps | easy add — expose the seed |
| Districts/landmarks | planned (ties to zones) |
| Heightmaps (hills slow trucks, steep needs special rigs) | planned — a per-tile elevation field feeding routing cost |
| Weather zones (coastal storms, smog) | planned |

---

## Top technical priorities

1. **Finish modular simulation ownership.** `WorldState` still coordinates most
   game state and update rules. Continue extracting focused components with
   explicit inputs/outputs; keep the modular monolith and avoid an event bus
   until it solves a demonstrated coupling problem.
2. **Extend end-to-end replay to user input and persistence boundaries.** The
   simulation replay now compares deterministic scenario snapshots across
   different render rates, including jobs, finances, rival decisions, lots,
   loans and RNG continuation. Next add recorded player-input sequences that
   drive the actual game state and save/resume checkpoints.
3. **Verify save portability across toolchains.** The versioned v1–v6 codec now
   owns serialization, validation and decoding separately from `WorldState`.
   Continue testing legacy-file migration and atomic replacement on each
   supported platform.

## Event catalogue (the glue)

Keep events as a small tagged struct drained each tick. Examples:
`WeatherChanged{to}`, `StormBegan{}`, `RushHourStarted{}`, `JobCreated{id,class,zone}`,
`JobCompleted{id,pay}`, `ContractWon{lot,by}`, `ContractLost{lot}`, `WeekRolled{}`,
`LoanTaken{amt}`, `IPOCompleted{raise}`, `RivalBid{lot,rate}`, `Impound{lot}`.

*Status legend: **built** = in game now and verified; partial = seed exists;
planned = designed, not yet coded.*
