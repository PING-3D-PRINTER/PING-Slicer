<!-- NO-CLAUDE-BRIDGE: 本檔是 OrcaSlicer 上游的通用 repo 指南（Build／Coding Style／Testing／Commit）。PING 的正本是同目錄 CLAUDE.md（10 KB）；本檔在地內容：Windows 別加 --parallel 該檔已收錄、〈兩線同源三句〉該檔開頭指路（x-0925-PA-01），橋接只會把 5.4 KB 重複塞進每個 session。（2026-08-20 Eric 裁，牌 x-0820-PM-05） -->

> 🔴 **跨專案鐵則不在這個檔裡，而 Codex 不會自動載入它**——它的專案文件預算走到 git repo 邊界就停，
> 本 repo 是巢狀獨立 repo ⇒ 根 `D:/dev/2026claude/AGENTS.md` 不會出現在你的 context 裡。
> **開工時先把它讀一次；收工前再看一次〈收工清單〉**（那裡有你這條線要跑的檢查與推送邊界）。
> 題目直接看那份的目錄——在這裡抄一份只會過期。
> ⚠️ **這一段是指路牌，不是規則**——它列的是題目，不是條文。照這幾行做事等於沒讀過那些規則。
> ⚠️ 開不了那個路徑（權限設定擋住）＝**回報，不要當成沒有規則**。
> 📎 `D:/dev/2026claude/事故庫/20260904_Codex文件預算是共用池而且到repo邊界就停.md`

## 🔴 只有一條線：出貨線 `release/v3.6`

**規則**：①功能分支**一律從 `origin/release/v3.6` 切**（桌面 App 開的工作樹基底是 `main`＝上游線，不是我們的線）②**要給別人測到的直接上車**；只自己試的停在功能分支，不另開長期分支 ③動參數就把 `resources/profiles/PING.json` 的 `version` 抬成目前最大值＋1；組車併第二條也動參數的分支，要自己比兩個父的 `version`（兩邊抬成同一個值時 git **不會**報衝突）④本機自己 build 帶 `-DPING_LOCAL_BUILD_TAG=DEV`（標題列「Beta DEV」，不冒用 `version.inc` 的 T 號；掛鉤在 `CMakeLists.txt`、隨 T086 起的車上線；CI 不帶這個參數＝不影響任何一班車）。
**為什麼**：開發線 `ping/v3.5` 已於 2026-10-14 廢除（Eric 裁），封存在 tag `archive/ping-v3.5-final`。兩線時期回併只有「出貨線→開發線」一個方向，停在開發線的東西永遠不會自己走到車上（ABS-CF 卡兩天、10-10 重做一次）。原本開發線獨有的 10 支 `tools/ping/` 維運工具與 `ping_export_deps_cache.yml` 已搬上本線。
📎 正本＝`../00治理文件/SOP_參數入版紀律.md` §V。

# Repository Guidelines

## Project Structure & Module Organization
OrcaSlicer’s C++17 sources live in `src/`, split by feature modules and platform adapters. User assets, icons, and printer presets are in `resources/`; translations stay in `localization/`. Tests sit in `tests/`, grouped by domain (`libslic3r/`, `sla_print/`, etc.) with fixtures under `tests/data/`. CMake helpers reside in `cmake/`, and longer references in `doc/` and `SoftFever_doc/`. Automation scripts belong in `scripts/` and `tools/`. Treat everything in `deps/` and `deps_src/` as vendored snapshots—do not modify without mirroring upstream tags.

## Build, Test, and Development Commands
Use out-of-source builds:
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` configures dependencies and generates build files.
- `cmake --build build --target OrcaSlicer --config Release -- "/m:1" "/p:CL_MPCount=6"` compiles the app.
  ⚠ Windows 本機**不要**加 `--parallel`／`-- -m`；理由、安全值與 C 槽餘量門檻見同目錄 `CLAUDE.md`〈Building on Windows〉。
- `cmake --build build --target tests` then `ctest --test-dir build --output-on-failure` runs automated suites.
Platform helpers such as `build_linux.sh`, `build_release_macos.sh`, and `build_release_vs2022.bat` wrap the same flow with toolchain flags. Use `build_release_macos.sh -sx` when reproducing macOS build issues, and `scripts/DockerBuild.sh` for reproducible container builds.

## Coding Style & Naming Conventions
`.clang-format` enforces 4-space indents, a 140-column limit, aligned initializers, and brace wrapping for classes and functions. Run `clang-format -i <file>` before committing; the CMake `clang-format` target is available when LLVM tools are on your PATH. Prefer `CamelCase` for classes, `snake_case` for functions and locals, and `SCREAMING_CASE` for constants, matching conventions in `src/`. Keep headers self-contained and align include order with the IWYU pragmas.

## Testing Guidelines
Unit tests rely on Catch2 (`tests/catch2/`). Name specs after the component under test—for example `tests/libslic3r/TestPlanarHole.cpp`—and tag long-running cases so `ctest -L fast` remains useful. Cover new algorithms with deterministic fixtures or sample G-code stored in `tests/data/`. Document manual printer validation or regression slicer checks in your PR when automated coverage is insufficient.

## Commit & Pull Request Guidelines
The history favors concise, sentence-style subject lines with optional issue references, e.g., `Fix grid lines origin for multiple plates (#10724)`. Squash fixups locally before opening a PR. Complete `.github/pull_request_template.md`, include reproduction steps or screenshots for UI changes, and mention impacted presets or translations. Link issues via `Closes #NNNN` when applicable, and call out dependency bumps or profile migrations for maintainer review.

## Security & Configuration Tips
Follow `SECURITY.md` for vulnerability reporting. Keep API tokens and printer credentials out of tracked configs; use `sandboxes/` for experimental settings. When touching third-party code in `deps_src/`, record the upstream commit or release in your PR description and run the relevant platform build script to confirm integration.
