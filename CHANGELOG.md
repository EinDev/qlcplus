# Changelog

## [6.0.0](https://github.com/EinDev/qlcplus/compare/eindev-v5.3.1...eindev-v6.0.0) (2026-08-30)


### ⚠ BREAKING CHANGES

* **3d-view:** ContextManager's fixtureDmxScale QML property/signal is renamed to fixtureRotationScale; SettingsView2D/3D.qml's "DMX scale" field is relabeled "Rotation scale" with a new "Position range" field alongside it. Saved shows load unaffected (XML attribute name unchanged).

### Features

* **3d-view:** add independent per-fixture position range in meters, support multi-select for DMX invert/scale/range ([6e30ceb](https://github.com/EinDev/qlcplus/commit/6e30ceb462b1cd1c1c208b8c34a7d481ed085652))
* **3d-view:** independent per-fixture position range in meters ([1f72e9c](https://github.com/EinDev/qlcplus/commit/1f72e9ca99782fc21a63869380ff18d0d05bb9a6))
* add binary noise style option to Noise RGB script ([cf2a316](https://github.com/EinDev/qlcplus/commit/cf2a316d46f346bb7b2f5204f92886f07832f671))
* play back legacy Show after converting its beat-pseudo timing ([c9c549c](https://github.com/EinDev/qlcplus/commit/c9c549c046ed1de9a5fe5f21c43946918219866a))
* **release:** attach Linux/macOS installers too, bootstrap version at 5.3.1 ([1ed6417](https://github.com/EinDev/qlcplus/commit/1ed6417df72b0e18a6038fd5fcc0d34af64d85e2))


### Bug Fixes

* **3d-view:** arrange tools now commit through the DMX-position path ([8e8187b](https://github.com/EinDev/qlcplus/commit/8e8187b1aa1c94edb053741673ebf893948d31b6))
* **3d-view:** raise the circle-arrangement diameter cap from 10m to 2km ([2910dc1](https://github.com/EinDev/qlcplus/commit/2910dc1d25e135e808bbec811cd62c887980fb79))
* add Shift range-select to the Fixture/Group tree used by EFX's Add Fixture panel ([ee278a3](https://github.com/EinDev/qlcplus/commit/ee278a32a8ae3a2115cf0d01513089c46a47eeb8))
* broadcast core.settings.changed before sending the response ([21d8841](https://github.com/EinDev/qlcplus/commit/21d8841429acd93efe1f0ec3f81eca61ac39ec53))
* carry nodePath on Shift-range-selected fixture items ([77fa0f6](https://github.com/EinDev/qlcplus/commit/77fa0f64d3277f27dfba25d09a895a89be95bec2))
* derive script basename with QFileInfo in rgbscript test ([0c002f8](https://github.com/EinDev/qlcplus/commit/0c002f876b5c9f2642a06839ba61bd0ba2f46831))
* give inputoutputmap_test a QCoreApplication and stage input profiles ([ef680e6](https://github.com/EinDev/qlcplus/commit/ef680e683eed3db622701710cc33308a96abebcd))
* make doc_test absolute-path assertions portable to Windows ([7804d4c](https://github.com/EinDev/qlcplus/commit/7804d4c3b84112c7c660c855c05e0a38f0155754))
* make qlcfixturedefcache_test pass on Windows/macOS CI ([cd6b851](https://github.com/EinDev/qlcplus/commit/cd6b8518c4dd850e6bae78e040bbe95423e51f14))
* prevent duplicate/dropped fixtures during Shift range-select drag ([fc550cd](https://github.com/EinDev/qlcplus/commit/fc550cd152417e63e9dc56090e4639ed4699d972))
* qmlimportscanner PATH for windeployqt, throughput-independent mastertimer_test ([52f2626](https://github.com/EinDev/qlcplus/commit/52f2626302d9ce19e720580b9a3f442447863606))
* replace non-numeric bogus font string in rgbtext_test with empty string ([7ca450c](https://github.com/EinDev/qlcplus/commit/7ca450c3f2f5fc84f51d829bb15cfd30819e2786))
* stop rebinding the play-after-convert checkbox's checked state ([c383ae4](https://github.com/EinDev/qlcplus/commit/c383ae4f6885a0786d6d8af1fc0f282a6efb2626))
* stop/unregister MasterTimer test stubs before asserting on them ([4c2990d](https://github.com/EinDev/qlcplus/commit/4c2990dda107bef709cc4f73c701d6f1a39e98ff))
* strip debug instrumentation from Show Manager timing investigation ([536cdeb](https://github.com/EinDev/qlcplus/commit/536cdeb8cc7b024af925d4c38f2cd4b561eedfe5))
* **tests:** eliminate mastertimer_test flakiness/segfault, fix macOS defDirectories() ([a553ed9](https://github.com/EinDev/qlcplus/commit/a553ed9e2a5dfce416360cf7b69befeb7a4193be))
* **tests:** make mastertimer_test::interval() immune to CI scheduler starvation ([52f2626](https://github.com/EinDev/qlcplus/commit/52f2626302d9ce19e720580b9a3f442447863606))
* update qlcchannel_test groupList expectations for 3D channel groups ([b76a4d0](https://github.com/EinDev/qlcplus/commit/b76a4d0e276de09fed011001ba1d449b5002340a))
