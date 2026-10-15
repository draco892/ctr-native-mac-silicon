# Interpolation layouts and modes — 2026-10-15

This change extends eligibility and shared scene reconstruction to 1–4 views,
main/character-selection menus, adventure arenas and in-engine cutscenes. It does
not establish complete interpolation of every special pass in these modes.

## Automated verification

Both normal and ASan/UBSan game builds pass **46/46 CTest executables**.

- A production-adapter fixture exercises all combinations of 1–4 views and
  menu/cutscene/adventure/race modes. It checks viewport draw-environment order,
  generated world geometry, a single unchanged UI model, and unchanged original
  packet bytes at five presentation alphas.
- The same fixture checks full canonical fallback for an offscreen draw environment
  linked at a normal depth and at bucket 1023. Every presentation keeps that chain.
- Portable scene tests sample four independently moving cameras at 121 alphas,
  with different material colours per view and a billboard visible in one view.
  They verify world membership, rejection of UI view activation, and history reset
  on a menu/layout transition.
- Visual metadata tests check that an explicit model viewport is independent of
  material identity bits and does not leak out of its capture scope.
- Existing scheduler, camera/aspect, geometry, animation, wheel, material,
  allocation/lifetime and game-baseline tests remain green.

## Interactive check

A temporary app with separate configuration/save storage ran at 800×600 with a
60 FPS ceiling. The check covered the startup/main-menu animations, Arcade menus,
character selection, a new Adventure profile, Aku Aku's introductory exchange,
movement to the Crash Cove warp pad and the subsequent race transition. Several
stages were compared with interpolation toggled off/on. This was a qualitative
smoke check; no synchronized pixel diff or gameplay/audio parity was measured.
The final expanded offscreen-environment guard was added after that run and is
covered by both automated suites.

The initial broad capture exposed duplicate UI/pool models in the menu. The
world-membership restriction removed those extra world copies in the follow-up
interactive build. A grey ground region in the first adventure hub was also
visible with interpolation disabled; this pass does not classify it as an
interpolation regression or claim to fix the canonical hub renderer.

The multiplayer option was disabled without a second controller. Actual 2–4
player races were therefore not exercised interactively; their OT/material
isolation is covered by the deterministic fixtures. Later cutscenes and other
adventure hubs remain unverified.

Runtime CSVs are in ignored `debug/layout-world`. The observed main-menu level 39
had interpolated regeneration; level 40 character selection and the level 26
adventure hub used the coherent current pose due to retained special/glow paths.
Eligibility alone does not guarantee interpolated movement for every scene.

## Remaining limits

Offscreen kart decal passes (DecalMP) keep the complete canonical chain, with
`scene_canonical_pose_reasons` bit 8. Pixel LOD/custom/billboard/split geometry,
retained effects and screen-space sky glow remain subject to canonical/current-pose
safeguards. Full split-screen smoothing still requires migrating those passes.
2D menus and HUD keep their canonical packets and update cadence.

The host ceiling remains configurable; this work does not guarantee sustained
60/120 FPS. Performance/LOD work and full-game, long-session visual validation
remain separate tasks. The user's root `config.json` is unchanged (60 FPS,
interpolation enabled).

## Opening crate regression

A reported intermittent disappearance of the opening crate exposed an unsupported
scripted sequence in the expanded cutscene eligibility. The Naughty Dog crate
level now keeps its complete original GPU chain for every presentation, clearing
scene history; reason bit 16 identifies this exception. The FPS ceiling remains
configurable, but the intro uses canonical animation cadence.

A production-adapter regression starts from valid scene history, requests scene
interpolation in four different mode states, and verifies the original chain and
unchanged packets at all 121 alpha samples. It also verifies that capture becomes
eligible again on entering the main menu. Both complete normal and ASan/UBSan
suites pass 46/46 tests after this correction.

A fresh isolated 60 FPS/interpolation-enabled startup reached the main menu.
Runtime CSV inspection found no regenerated presentations in the crate level;
eligible intro frames carried canonical reason 16, and scene regeneration
resumed in the main menu. Logs are under ignored `debug/crate-regression`.
