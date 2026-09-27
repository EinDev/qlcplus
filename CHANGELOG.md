# Changelog

## [6.0.0](https://github.com/EinDev/qlcplus/compare/eindev-v5.3.1...eindev-v6.0.0) (2026-08-30)


### ⚠ BREAKING CHANGES

* **3d-view:** ContextManager's fixtureDmxScale QML property/signal is renamed to fixtureRotationScale; SettingsView2D/3D.qml's "DMX scale" field is relabeled "Rotation scale" with a new "Position range" field alongside it. Saved shows load unaffected (XML attribute name unchanged).

### Features

* **3d-view:** add independent per-fixture position range in meters, support multi-select for DMX invert/scale/range ([786d91f](https://github.com/EinDev/qlcplus/commit/786d91f031830cafd0c0e3e702dd52da8b70a8dc))
* **3d-view:** independent per-fixture position range in meters ([16b7f1a](https://github.com/EinDev/qlcplus/commit/16b7f1a385b860a07febe1fab9dd8948bc7e2b7e))
* add binary noise style option to Noise RGB script ([c676a00](https://github.com/EinDev/qlcplus/commit/c676a000ca90a3bb2dc6fefda79490ef0fb89a26))
* play back legacy Show after converting its beat-pseudo timing ([15393f9](https://github.com/EinDev/qlcplus/commit/15393f921095ae86100b6f1988132041eee4fb4c))
* **release:** attach Linux/macOS installers too, bootstrap version at 5.3.1 ([27757f4](https://github.com/EinDev/qlcplus/commit/27757f4d439b8e78176eb0d9b4c9ed3acdfe10b0))


### Bug Fixes

* **3d-view:** arrange tools now commit through the DMX-position path ([3eec555](https://github.com/EinDev/qlcplus/commit/3eec55592fb8536bce325ea59fb3babda184f30e))
* **3d-view:** raise the circle-arrangement diameter cap from 10m to 2km ([1503daa](https://github.com/EinDev/qlcplus/commit/1503daae3163d000f7323ebf9d8e41f4042f349b))
* add Shift range-select to the Fixture/Group tree used by EFX's Add Fixture panel ([370e802](https://github.com/EinDev/qlcplus/commit/370e802c0c6562f9ee1cd771ee0df954fa5fad5e))
* broadcast core.settings.changed before sending the response ([fc6bb22](https://github.com/EinDev/qlcplus/commit/fc6bb22694c5456811476e9bbbaff525f840c437))
* carry nodePath on Shift-range-selected fixture items ([2b8ebeb](https://github.com/EinDev/qlcplus/commit/2b8ebeb806dac8a09f43ce01ec1fbf75541e73b1))
* derive script basename with QFileInfo in rgbscript test ([5658b50](https://github.com/EinDev/qlcplus/commit/5658b500d0becbbad0f046f084d6f3d5948c43a6))
* give inputoutputmap_test a QCoreApplication and stage input profiles ([0e3086d](https://github.com/EinDev/qlcplus/commit/0e3086d19ebd04efab18c1cb72abd218f0e03a21))
* make doc_test absolute-path assertions portable to Windows ([4ee5f1a](https://github.com/EinDev/qlcplus/commit/4ee5f1a7c8ba9ed746ef0ea6d2f7a592402afe70))
* make qlcfixturedefcache_test pass on Windows/macOS CI ([0fea8e6](https://github.com/EinDev/qlcplus/commit/0fea8e622657b32eb774ab820eac711caa46f84a))
* prevent duplicate/dropped fixtures during Shift range-select drag ([ff07318](https://github.com/EinDev/qlcplus/commit/ff07318dbb74fc0791a4cf74731fedf41df808a7))
* qmlimportscanner PATH for windeployqt, throughput-independent mastertimer_test ([abf843d](https://github.com/EinDev/qlcplus/commit/abf843d5e6528744ca5031a2b4afc3f542b3d7a4))
* replace non-numeric bogus font string in rgbtext_test with empty string ([e980924](https://github.com/EinDev/qlcplus/commit/e980924927db8e50ca46fe091ad1e4cda0844792))
* stop rebinding the play-after-convert checkbox's checked state ([64f9d4a](https://github.com/EinDev/qlcplus/commit/64f9d4a38bbf1f118cff770e7c71464d0adc7556))
* stop/unregister MasterTimer test stubs before asserting on them ([124c200](https://github.com/EinDev/qlcplus/commit/124c200f8a355e8e3daf2df9a7aa839a45c94662))
* strip debug instrumentation from Show Manager timing investigation ([0647ad7](https://github.com/EinDev/qlcplus/commit/0647ad7a515bf9c5eec6cbcf26dd8058d5d67bca))
* **tests:** eliminate mastertimer_test flakiness/segfault, fix macOS defDirectories() ([8b463c6](https://github.com/EinDev/qlcplus/commit/8b463c65f83b87130f35e4078da4153936a14c47))
* **tests:** make mastertimer_test::interval() immune to CI scheduler starvation ([abf843d](https://github.com/EinDev/qlcplus/commit/abf843d5e6528744ca5031a2b4afc3f542b3d7a4))
* update qlcchannel_test groupList expectations for 3D channel groups ([9a5ade7](https://github.com/EinDev/qlcplus/commit/9a5ade7e313c2a0302166778403452f4bb73bb41))
