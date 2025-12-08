# Repository Guidelines

## Purpose

This repository contains the SellStrategy module under `result/`, compiled as a C++14 library for Linux (GCC 4.8) and used on a securities trading server. The goal is to provide stable, well-tested sell strategies (intraday, auction, close) on top of abstract trading/market data APIs.

## Directory Layout

- `result/src/core/` – core types and interfaces:
  - `ITradingApi.h`, `IMarketDataApi.h`, `TradingMarketApi.h`
  - config, RNG, order/market data structures, common utilities
- `result/src/strategies/` – strategy implementations:
  - `IntradaySellStrategy`, `AuctionSellStrategy`, `CloseSellStrategy`
- `result/src/adapters/` – concrete SDK adapters:
  - `SecTradingApi`, `TdfMarketDataApi`
- `result/include/` – public headers for external users:
  - `sell_strategy_api.h`, `sell_strategy_c_api.h`, `ImprovedLogger.h`
- `result/docs/` – design/deployment docs (read before architecture changes).

## C++ Rules (GCC 4.8 / C++14)

- Compile with `-std=c++14`, but avoid features not supported by GCC 4.8
  (no `<filesystem>`, `std::optional`, structured bindings, etc.).
- Use simple OO: virtual functions only where already designed; do not add new
  inheritance hierarchies without updating the design docs.
- Ownership:
  - Use `std::shared_ptr` for shared ownership of APIs.
  - Use raw pointers only as non-owning references (`TradingMarketApi*` in strategies).
  - Ensure the owner (`shared_ptr`) outlives any raw pointer users.

## Architecture Rules

- Strategies depend only on `TradingMarketApi` and core types; never include
  SDK headers from `result/include_external/` in strategy code.
- Keep the separation:
  - interfaces: `ITradingApi`, `IMarketDataApi`
  - composition: `TradingMarketApi`
  - adapters: `SecTradingApi`, `TdfMarketDataApi`
- Preserve existing function names and behavior in public headers to avoid ABI/API breaks.

## Build & Validation

- Build (from `result/`): `mkdir -p build && cd build && cmake .. && make -j4`.
- Verify on both Windows and Linux when changing core or public headers.
- Run example or integration binaries (see `USAGE.md`, `LINUX_DEPLOYMENT_GUIDE.md`)
  and check logs under `result/log/` using `ImprovedLogger`.
