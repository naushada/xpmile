# TDD Plan: in-house Docker Hub puller (`xpmile-pull`)

Test-first plan for the design in `runc-pull-design.md`. Each phase is
a RED → GREEN → REFACTOR cycle. Phases map roughly 1:1 to the design's
component map (§4).

## Ground rules

- All C++ tests run inside the existing `offtarget` GTest binary —
  the same one that runs the SSO + IdP suites. No separate runner.
- No test makes a real network call. Docker Hub is **always** mocked
  behind the extended `sso::IHttpClient`.
- No test writes outside its own per-test tempdir
  (`testing::TempDir()`). The unpacker's hardening tests assert this
  explicitly.
- Existing tests stay green throughout (current baseline includes
  the SSO 127 and the IdP 118 suites).
- The real `sso::HttpClient` (ACE_SOCK + OpenSSL) is **not**
  unit-tested — same precedent as `sso-tdd-plan.md` Phase B. The
  HttpClient *extensions* (custom headers, redirect, streaming) get
  pure-function tests where possible; the wire layer is
  integration-verified in Phase H's smoke script.
- The CI gating check (`Run offtarget GTest suite`) blocks merges
  until they pass.

## Test file map

| Phase | New test file(s) | GTest suite prefix |
|-------|------------------|---------------------|
| A | `modules/module/sso/test/sso_http_client_ext_test.cc` | `HttpClientHeaders*`, `HttpClientRedirect*`, `HttpClientStreaming*` |
| B | `modules/module/runcpull/test/registry_ref_test.cc` | `RegistryRef*` |
| B | `modules/module/runcpull/test/registry_token_test.cc` | `RegistryToken*` |
| B | `modules/module/runcpull/test/registry_client_test.cc` | `RegistryClient*` |
| B | `modules/module/runcpull/test/host_arch_test.cc` | `HostArch*` |
| C | `modules/module/runcpull/test/manifest_parse_test.cc` | `Manifest*` |
| C | `modules/module/runcpull/test/image_config_parse_test.cc` | `ImageConfig*` |
| C | `modules/module/runcpull/test/digest_test.cc` | `Digest*` |
| D | `modules/module/runcpull/test/tar_parse_test.cc` | `Tar*` |
| D | `modules/module/runcpull/test/gzip_test.cc` | `Gzip*` |
| D | `modules/module/runcpull/test/whiteout_test.cc` | `Whiteout*` |
| D | `modules/module/runcpull/test/unpack_hardening_test.cc` | `PathTraversal*`, `FileMode*`, `Rootless*`, `UnpackLimit*` |
| E | `modules/module/runcpull/test/bundle_writer_test.cc` | `BundleWriter*` |
| F | `modules/module/runcpull/test/pull_orchestrator_test.cc` | `Pull*` |
| G | `modules/module/runcpull/test/cli_test.cc` | `Cli*` |

## Test doubles

| Double | Implements | Purpose |
|--------|-----------|---------|
| `MockHttpClient` (extended) | `sso::IHttpClient` | Already exists for SSO tests. Add: header capture spy, canned per-URL redirect chain, streaming-callback canned-body driver that can split the body into N chunks. |
| `InMemoryTarBuilder` | (test helper, not interface) | Builds tar archives in-memory from `vector<TarEntry>{name, mode, type, content}`. Lets each tar/whiteout test construct exactly the fixture it needs in C++ without checking binary blobs into git. |
| `GzipBuilder` | (test helper) | `gzip_compress(bytes) -> bytes`. Used to wrap `InMemoryTarBuilder` output into the gzipped form layer blobs use. |
| `FakeArchProbe` | `runcpull::IHostProbe` | Injectable `host_arch()` so arch-selection tests don't depend on `uname -m`. |

## Build-time test fixtures

Tar/gzip fixtures are **constructed at test runtime** by the helpers
above — no binary tarballs checked into git. This keeps the repo
clean and the fixtures inspectable. The only checked-in fixtures are
canned JSON manifest snippets under
`modules/module/runcpull/test/fixtures/`:

- `oci_image_index.json` — 4-platform OCI image index (linux/amd64,
  linux/arm64, linux/arm/v7, windows/amd64)
- `docker_manifest_list.json` — same shape, Docker mediaType
- `oci_image_manifest.json` — single-arch OCI image manifest (config
  digest + 3 layer digests)
- `docker_manifest_v2.json` — same shape, Docker mediaType
- `image_config.json` — OCI image config with `Env` / `Cmd` /
  `Entrypoint` / `rootfs.diff_ids` set
- `token_response.json` + `token_response_access_token.json` — two
  shapes of `/token` reply (some registries use `token`, some
  `access_token`)
- `www_authenticate_header.txt` — raw header value to parse

Generated at build time (via `test/CMakeLists.txt` like the IdP keys
target):

- `runcpull_test_data/alpine_minimal/` — a hand-rolled 3-file
  fixture image: one config blob, one layer (a gzipped tar with
  `etc/alpine-release`, `bin/sh`, `usr/local/bin/foo`). Used by the
  Phase F end-to-end orchestrator tests so we don't carry binary
  blobs in git. The CMake step that produces it is ~15 lines of `cmake -E tar`.

---

## Phase A — `sso::HttpClient` extensions

### Why first

Phase B's registry client needs custom `Authorization` + multi-value
`Accept` headers, redirect following, and streaming body downloads —
none of which the existing `sso::HttpClient` does. We extend it (no
fork) so SSO benefits too.

### Step A.1 — RED: custom request header capture in `MockHttpClient`

**File:** `modules/module/sso/test/sso_http_client_ext_test.cc`

```
TEST(HttpClientHeadersTest, MockClient_RecordsRequestHeaders)
TEST(HttpClientHeadersTest, MockClient_HeadersDefaultEmpty_WhenNoneSet)
TEST(HttpClientHeadersTest, MockClient_LastHeadersReplaced_OnNextCall)
```

The mock is the only thing that needs unit tests here; the real
client's header serialisation is covered by the smoke script in
Phase H. **3 tests. RED.**

### Step A.2 — RED: redirect policy (pure function)

```
TEST(HttpClientRedirectTest, AbsoluteLocation_UsedAsIs)
TEST(HttpClientRedirectTest, RelativeLocation_ResolvedAgainstCurrent)
TEST(HttpClientRedirectTest, SameOriginRedirect_KeepsAuthorization)
TEST(HttpClientRedirectTest, CrossOriginRedirect_DropsAuthorization)
TEST(HttpClientRedirectTest, HopLimitExceeded_Errors)
TEST(HttpClientRedirectTest, StatusCodes_301_302_307_308_AllFollowed)
TEST(HttpClientRedirectTest, Status303_RedirectAsGet_BodyDropped)
```

Pure-function `redirect_step(current_url, status, location_header,
headers_in) -> (next_url, headers_out, follow_or_stop)`. **7 tests. RED.**

### Step A.3 — RED: streaming body callback shape

```
TEST(HttpClientStreamingTest, Callback_ReceivesFullBody_AcrossChunks)
TEST(HttpClientStreamingTest, Callback_TotalBytesMatchContentLength)
TEST(HttpClientStreamingTest, Callback_AbortsOnException)
```

Tests are against the `MockHttpClient` streaming-callback driver
(which delivers bytes in test-controlled N-chunk splits). **3 tests. RED.**

### A — GREEN / REFACTOR

GREEN: extend `MockHttpClient` with `last_request_headers`,
`set_canned_response(url, status, headers, body)`,
`set_canned_redirect_chain(url, [(url, status, location), …, final])`,
and a `get_streaming(url, headers, callback)` overload. Add
`redirect_step()` as a free function in `sso_http_client.hpp` and
wire the real `HttpClient::get` to use it. Add headers + streaming
overloads to `IHttpClient`. REFACTOR: pull origin comparison into
`url_same_origin(a, b)`.

**Phase A: 13 tests.**

---

## Phase B — Docker Hub registry client

### Why second

Phase F orchestrator depends on the registry client. C/D/E can run
in parallel with this once A is in.

### Step B.1 — RED: image reference parsing

**File:** `modules/module/runcpull/test/registry_ref_test.cc`

```
TEST(RegistryRefTest, ShortName_DefaultsToLibraryNamespace)
```
- `mongo:4.4` → `{host: "docker.io", name: "library/mongo", tag: "4.4"}`

```
TEST(RegistryRefTest, UserName_DefaultsToHubHost)
```
- `naushada/xpmile-wsdbagent` →
  `{host: "docker.io", name: "naushada/xpmile-wsdbagent", tag: "latest"}`

```
TEST(RegistryRefTest, FullRef_PreservedExactly)
TEST(RegistryRefTest, DigestRef_TagAbsent)
```
- `naushada/foo@sha256:abcd…` → `tag` empty, `digest` set.

```
TEST(RegistryRefTest, TagAndDigest_DigestPreferred)
TEST(RegistryRefTest, InvalidName_Rejected)
TEST(RegistryRefTest, EmptyString_Rejected)
TEST(RegistryRefTest, NonHubRegistryHost_Rejected_InV1)
```
- `quay.io/x/y` → rejected in v1 (out-of-scope per §3).

**8 tests. RED.**

### Step B.2 — RED: token flow

**File:** `modules/module/runcpull/test/registry_token_test.cc`

```
TEST(RegistryTokenTest, ParseWwwAuthenticate_ExtractsRealmAndService)
TEST(RegistryTokenTest, ParseWwwAuthenticate_HandlesQuotedAndUnquoted)
TEST(RegistryTokenTest, ParseWwwAuthenticate_MissingRealm_Errors)
TEST(RegistryTokenTest, BuildTokenUrl_EncodesScope)
```
- realm + service + name + "pull" → URL with
  `service=…&scope=repository:<name>:pull` properly URL-encoded.

```
TEST(RegistryTokenTest, ParseTokenResponse_TokenField)
TEST(RegistryTokenTest, ParseTokenResponse_AccessTokenField)
TEST(RegistryTokenTest, ParseTokenResponse_PrefersToken_WhenBothPresent)
TEST(RegistryTokenTest, ParseTokenResponse_MissingBoth_Errors)
TEST(RegistryTokenTest, ParseTokenResponse_MalformedJson_Errors)
```

**9 tests. RED.**

### Step B.3 — RED: registry client integration (via mock HTTP)

**File:** `modules/module/runcpull/test/registry_client_test.cc`

```
TEST(RegistryClientTest, Probe_GetsTokenFromChallenge)
```
- Mock: `/v2/` → 401 with `Www-Authenticate`; `/token?…` → 200
  `{"token":"T"}` → client returns `T`.

```
TEST(RegistryClientTest, Manifest_SetsAllAcceptVariants)
```
- Mock spies the `Accept:` header; asserts it contains all 4 media
  types.

```
TEST(RegistryClientTest, Manifest_SetsBearerAuthorization)
TEST(RegistryClientTest, Manifest_404_ReturnsManifestNotFound)
TEST(RegistryClientTest, Blob_FollowsCdnRedirect_DropsBearer)
```
- Mock: `/v2/<name>/blobs/<d>` → 307 to
  `https://cdn.example/abc`; `https://cdn.example/abc` → 200 body.
  Spy asserts the second request **omits** `Authorization`.

```
TEST(RegistryClientTest, Blob_StreamsBodyToCallback)
TEST(RegistryClientTest, Probe_Token_Caching_OneTokenPerImage)
```
- Two manifest fetches for the same image → only one `/token?…`
  call. (We don't cache across images; pull is a one-shot CLI.)

**7 tests. RED.**

### Step B.4 — RED: host architecture probe

**File:** `modules/module/runcpull/test/host_arch_test.cc`

```
TEST(HostArchTest, Probe_x86_64_MapsToAmd64)
TEST(HostArchTest, Probe_aarch64_MapsToArm64)
TEST(HostArchTest, Probe_armv7l_MapsToArmV7)
TEST(HostArchTest, Probe_Unknown_Errors)
TEST(HostArchTest, CliOverride_BeatsAutoDetect)
```

**5 tests. RED.**

### B — GREEN / REFACTOR

GREEN: `runcpull::Ref` + `parse_ref`; `parse_www_authenticate`,
`build_token_url`, `parse_token_response`; `RegistryClient` class
holding `IHttpClient*` + token-once-per-image cache; `host_arch()`
free function reading `uname` (via `IHostProbe` for tests).
REFACTOR: collapse the 4-tuple `Accept:` literal into a constant.

**Phase B: 29 tests.**

---

## Phase C — Manifest, image config, digest

### Step C.1 — RED: manifest parsing (image index)

**File:** `modules/module/runcpull/test/manifest_parse_test.cc`

```
TEST(ManifestTest, OciImageIndex_ParsesAllPlatforms)
```
- Fixture `oci_image_index.json` → 4 entries with correct
  `mediaType`, `digest`, `platform.{os,architecture,variant}`.

```
TEST(ManifestTest, OciImageIndex_PicksLinuxArm64)
TEST(ManifestTest, OciImageIndex_PicksLinuxArmV7_ByVariant)
TEST(ManifestTest, OciImageIndex_NoMatch_Errors)
TEST(ManifestTest, OciImageIndex_PrefersExactVariant_OverNone)
```
- Two arm64 entries, one with `"variant":"v8"`, the host is
  `linux/arm64` (no variant) → still picks one of them with a
  defined tiebreak.

```
TEST(ManifestTest, DockerManifestList_ParsesAndSelects)
TEST(ManifestTest, MediaType_UnknownIndex_Errors)
```

**7 tests. RED.**

### Step C.2 — RED: manifest parsing (image manifest)

```
TEST(ManifestTest, OciImageManifest_HasConfigAndLayers)
TEST(ManifestTest, OciImageManifest_LayersInArrayOrder)
TEST(ManifestTest, DockerManifestV2_HasConfigAndLayers)
TEST(ManifestTest, ImageManifest_MissingLayers_Errors)
TEST(ManifestTest, ImageManifest_ConfigDigest_RequiredFormat)
```
- Rejects a manifest whose `config.digest` doesn't match
  `^sha256:[0-9a-f]{64}$`.

**5 tests. RED.**

### Step C.3 — RED: image config parsing

**File:** `modules/module/runcpull/test/image_config_parse_test.cc`

```
TEST(ImageConfigTest, ExtractsEnvAsListOfStrings)
TEST(ImageConfigTest, ExtractsCmdAndEntrypoint)
TEST(ImageConfigTest, MissingEnvCmdEntrypoint_DefaultsEmpty)
TEST(ImageConfigTest, ParsesRootfsDiffIds)
TEST(ImageConfigTest, MalformedJson_Errors)
```

**5 tests. RED.**

### Step C.4 — RED: SHA-256 digest helpers

**File:** `modules/module/runcpull/test/digest_test.cc`

```
TEST(DigestTest, SHA256_OfEmptyString_MatchesKnownHex)
TEST(DigestTest, SHA256_OfKnownBytes_MatchesKnownHex)
TEST(DigestTest, Streaming_NChunks_EqualsOneShot)
TEST(DigestTest, Streaming_LargePayload_NoOverflow)
TEST(DigestTest, Mismatch_DetectedOnFinalize)
TEST(DigestTest, ParseDigestString_ExtractsAlgAndHex)
TEST(DigestTest, ParseDigestString_RejectsBadHex)
TEST(DigestTest, ParseDigestString_RejectsUnsupportedAlg_Sha512)
```

**8 tests. RED.**

### C — GREEN / REFACTOR

GREEN: `runcpull::Manifest`, `runcpull::ImageConfig` types +
`parse_*` functions on top of `nlohmann::json`; `Digest` wrapper
around `EVP_DigestInit/Update/Final` with `update(bytes, len)` /
`finalize() -> hex` / `verify(expected)`. REFACTOR: pull the
arch-tiebreak rule into `manifest_pick_platform()`.

**Phase C: 25 tests.**

---

## Phase D — Layer unpack

### Why this is the biggest phase

Tar parsing + whiteout semantics + path-traversal hardening is where
bugs (and CVEs) historically live for image extractors. We pay for
that with the most tests.

### Step D.1 — RED: tar parsing

**File:** `modules/module/runcpull/test/tar_parse_test.cc`

Each test builds its fixture in-memory via `InMemoryTarBuilder`.

```
TEST(TarTest, UstarHeader_RegularFile_HasNameSizeMode)
TEST(TarTest, UstarHeader_Directory_TypeflagFive)
TEST(TarTest, UstarHeader_Symlink_TargetParsed)
TEST(TarTest, UstarHeader_Hardlink_TargetParsed)
TEST(TarTest, EmptyTar_DoubleZeroBlock_Terminates)
TEST(TarTest, MultipleEntries_ParsedInOrder)
TEST(TarTest, OddSizedContent_PaddedTo512_Skipped)
TEST(TarTest, PaxLongPath_ResolvesEntryName)
TEST(TarTest, PaxLongLinkpath_ResolvesSymlinkTarget)
TEST(TarTest, TruncatedHeader_Errors)
TEST(TarTest, BadChecksum_Errors)
TEST(TarTest, UnknownTypeflag_SkippedOrErrors)
```

**12 tests. RED.**

### Step D.2 — RED: gzip stream

**File:** `modules/module/runcpull/test/gzip_test.cc`

```
TEST(GzipTest, Decompress_KnownStream_MatchesExpected)
TEST(GzipTest, Streamed_NChunks_EqualsOneShot)
TEST(GzipTest, TruncatedStream_Errors)
TEST(GzipTest, InvalidMagic_Errors)
TEST(GzipTest, Empty_LegalGzipOfEmpty_OK)
```

**5 tests. RED.**

### Step D.3 — RED: whiteout handling

**File:** `modules/module/runcpull/test/whiteout_test.cc`

```
TEST(WhiteoutTest, RegularWhiteout_DeletesPriorFile)
```
- Layer 1: `etc/hosts`; layer 2: `etc/.wh.hosts` → after unpack,
  `rootfs/etc/hosts` absent.

```
TEST(WhiteoutTest, RegularWhiteout_FileNotWrittenToRootfs)
```
- The `.wh.hosts` file itself does NOT appear under `rootfs/etc/`.

```
TEST(WhiteoutTest, RegularWhiteout_LeavesSiblingFiles)
TEST(WhiteoutTest, OpaqueWhiteout_ClearsParentDirContents)
TEST(WhiteoutTest, OpaqueWhiteout_NewEntriesInSameLayer_Survive)
TEST(WhiteoutTest, OpaqueWhiteout_LeavesParentDirItself)
TEST(WhiteoutTest, OpaqueWhiteout_LeavesUnrelatedDirs)
TEST(WhiteoutTest, FileResurrectedInLaterLayer_FinalStateLatest)
```

**8 tests. RED.**

### Step D.4 — RED: path-traversal & symlink hardening

**File:** `modules/module/runcpull/test/unpack_hardening_test.cc`

```
TEST(PathTraversalTest, DotDot_InName_Rejected)
TEST(PathTraversalTest, AbsolutePath_StrippedToRelative)
TEST(PathTraversalTest, SymlinkTarget_OutsideRootfs_Rejected)
```
- Entry: `usr/local/bin/sh` is a symlink whose target resolves to
  `../../../../etc/passwd` → unpack aborts; `etc/passwd` outside
  rootfs untouched (assert by checking a sentinel file in the
  per-test tempdir's parent).

```
TEST(PathTraversalTest, SubsequentWriteThroughExistingSymlink_Blocked)
```
- Layer 1: `etc → /tmp/evil` (symlink). Layer 2: regular file
  `etc/passwd`. The write must NOT follow the symlink — should
  either fail or write a real file at `rootfs/etc/passwd` after the
  symlink is removed. Assert `/tmp/evil/passwd` does not exist.

```
TEST(PathTraversalTest, WhiteoutOfSymlink_RemovesSymlink_NotTarget)
```

**5 tests. RED.**

### Step D.5 — RED: file mode hardening

```
TEST(FileModeTest, SetuidBit_Stripped)
TEST(FileModeTest, SetgidBit_Stripped)
TEST(FileModeTest, StickyBit_Stripped)
TEST(FileModeTest, RegularBits_Preserved_0644)
TEST(FileModeTest, ExecBits_Preserved_0755)
```

**5 tests. RED.**

### Step D.6 — RED: rootless uid/gid mapping

```
TEST(RootlessTest, UidGid_ClampedToCallerOnDisk)
TEST(RootlessTest, OriginalImageUidGid_RecordedInBundleMetadata)
```
- The unpacker records the original uid/gid in a sidecar file
  (`.runcpull-meta.json` next to `rootfs/`) so a future v2 can
  rebuild the `linux.uidMappings` table without re-fetching the
  layer.

**2 tests. RED.**

### Step D.7 — RED: size limits

```
TEST(UnpackLimitTest, PerEntrySize_ExceedsLimit_Aborts)
TEST(UnpackLimitTest, TotalSize_ExceedsLimit_AbortsMidStream)
TEST(UnpackLimitTest, LimitOff_NoOpForReasonablePayload)
```

**3 tests. RED.**

### D — GREEN / REFACTOR

GREEN: `runcpull::TarReader` (POSIX ustar + pax `path=` /
`linkpath=` extension headers); `runcpull::GzipInflater` (zlib
streaming wrapper); `LayerUnpacker` that takes a sequence of layer
streams + an output rootfs path and applies them with whiteout
semantics; the `openat` / `O_NOFOLLOW`-based safe write functions.
REFACTOR: pull the resolved-target safety check into
`resolve_inside_root(root_fd, name)`.

**Phase D: 40 tests.**

---

## Phase E — Bundle writer

### Step E.1 — RED: bundle layout

**File:** `modules/module/runcpull/test/bundle_writer_test.cc`

```
TEST(BundleWriterTest, CreatesRootfsAndStubConfig)
```
- `write(bundle_dir, image_config)` → `rootfs/` exists,
  `config.json` is valid JSON.

```
TEST(BundleWriterTest, StubConfig_HasOciVersion)
TEST(BundleWriterTest, StubConfig_HasProcessEnv_FromImageConfig)
TEST(BundleWriterTest, StubConfig_HasProcessArgs_EntrypointThenCmd)
```
- Image config: `Entrypoint=["/bin/sh","-c"]`, `Cmd=["mongod"]` →
  stub `process.args` is `["/bin/sh","-c","mongod"]`.

```
TEST(BundleWriterTest, StubConfig_EmptyMounts_EmptyNamespaces)
```
- Operator overwrites with operator-runc.md §5 templates; we ship
  an empty starting point.

```
TEST(BundleWriterTest, AtomicRename_PartialFailureLeavesNoBundle)
```
- Inject a failure mid-write → `bundle_dir/` does not exist on the
  filesystem; `bundle_dir.tmp/` either gone or clearly named.

```
TEST(BundleWriterTest, RefusesToOverwriteExistingBundle_WithoutForce)
TEST(BundleWriterTest, ForceFlag_OverwritesAtomically)
```

**8 tests. RED.**

### E — GREEN / REFACTOR

GREEN: `BundleWriter` class with `write(bundle_dir, image_config,
options)`; tempdir-then-rename atomic semantics; `--force` for
re-pulling into the same bundle dir. REFACTOR: stub config emitted
by a small inline `nlohmann::json` literal builder.

**Phase E: 8 tests.**

---

## Phase F — Pull orchestrator

### Why later

End-to-end mock-driven tests that exercise everything above. Cheaper
to debug after each component is green individually.

### Step F.1 — RED: happy paths

**File:** `modules/module/runcpull/test/pull_orchestrator_test.cc`

All tests use the `runcpull_test_data/alpine_minimal/` build-time
fixture (a hand-rolled 1-layer image — config blob + one gzipped
tarball with `/etc/alpine-release`).

```
TEST(PullTest, HappyPath_SingleLayer_BundleHasExpectedFile)
```
- Mock returns probe → token → image manifest (single-arch — no
  index step) → config → 1 layer. Result: `bundle/rootfs/etc/alpine-release`
  exists with expected content.

```
TEST(PullTest, HappyPath_MultiLayer_LayersAppliedInOrder)
```
- 3 layers; layer 2 has `etc/.wh.alpine-release`; layer 3 has new
  `etc/alpine-release` → final state is layer 3's version.

```
TEST(PullTest, HappyPath_MultiArchIndex_PicksHostArch)
```
- Mock returns index → arm64 selected → second manifest fetch by
  digest → rest of flow proceeds.

```
TEST(PullTest, HappyPath_DigestRefSkipsIndexStep)
```
- Reference is `…@sha256:…` → orchestrator goes straight to
  per-arch manifest; no index fetch.

```
TEST(PullTest, HappyPath_OneTokenAcrossAllBlobFetches)
```
- For one pull, only one `/token?…` call regardless of how many
  blobs are fetched.

**5 tests. RED.**

### Step F.2 — RED: failure modes (atomicity)

```
TEST(PullTest, LayerHttp500_AbortsAndLeavesNoBundle)
TEST(PullTest, ConfigDigestMismatch_AbortsAndLeavesNoBundle)
TEST(PullTest, LayerDigestMismatch_AbortsAndLeavesNoBundle)
TEST(PullTest, NoMatchingArch_AbortsBeforeAnyDownload)
TEST(PullTest, TokenEndpoint403_AbortsBeforeAnyManifestFetch)
TEST(PullTest, ManifestUnsupportedMediaType_AbortsWithManifestError)
TEST(PullTest, UnpackPathTraversal_AbortsAndLeavesNoBundle)
```
- After any abort: `bundle_dir/` does not exist and no temporary
  `.tmp` directory is leaked.

**7 tests. RED.**

### F — GREEN / REFACTOR

GREEN: `PullOrchestrator::run(ref, bundle_dir, options) ->
PullResult`. Wires the registry client, manifest parser, digest
verifier, gzip inflater, tar reader, unpacker, and bundle writer.
Maps every failure class onto a `PullError` enum. REFACTOR: extract
the `(probe → token → manifest)` chain into a helper that's
naturally retriable for v2.

**Phase F: 12 tests.**

---

## Phase G — CLI

### Step G.1 — RED: argument parsing + exit codes

**File:** `modules/module/runcpull/test/cli_test.cc`

```
TEST(CliTest, MissingRequiredArgs_Exit6_AndUsageOnStderr)
TEST(CliTest, HelpFlag_Exit0_AndUsageOnStdout)
TEST(CliTest, VersionFlag_Exit0_AndPrintsVersion)
TEST(CliTest, ArchOverride_PropagatesToOrchestrator)
TEST(CliTest, ToFlag_PropagatesToOrchestrator)
TEST(CliTest, ForceFlag_PropagatesToBundleWriter)
TEST(CliTest, MaxEntrySize_ParsedWithUnits)
```
- `--max-entry-size 1G` → `1 << 30` bytes.

**7 tests. RED.**

### Step G.2 — RED: PullError → exit code mapping

```
TEST(CliTest, ExitCode_Transport_Is1)
TEST(CliTest, ExitCode_Auth_Is2)
TEST(CliTest, ExitCode_Manifest_Is3)
TEST(CliTest, ExitCode_DigestMismatch_Is4)
TEST(CliTest, ExitCode_Unpack_Is5)
TEST(CliTest, ExitCode_Usage_Is6)
TEST(CliTest, Success_Is0_AndPrintsSummaryLine)
```

**7 tests. RED.**

### G — GREEN / REFACTOR

GREEN: `main()` thin shell over `argv_parse(argc, argv) ->
CliArgs`, then `PullOrchestrator::run`, then `print_summary`. Use
a small argv parser (no getopt dependency — keeps the static
binary thin). REFACTOR: collapse error-class → exit-code into a
single table.

**Phase G: 14 tests.**

---

## Phase H — Tarball, install.sh, end-to-end smoke (manual + scripted)

### Why no GTest

H is shell + CI YAML + binary layout. Not in `offtarget`.

### H.1 — CI assertion: tarball has expected layout

The new `runc-bundle` job in `.github/workflows/publish-images.yml`
runs:

```sh
tar -tzf xpmile-runc-bundle-${VERSION}-${ARCH}.tar.gz | sort > /tmp/got
diff <(sort tests/expected-tarball-layout.txt) /tmp/got
```

`tests/expected-tarball-layout.txt` is checked in and is the
authoritative list of paths inside the tarball. Any addition or
omission fails CI.

### H.2 — Scripted smoke test (Linux runner, not in `offtarget`)

A new script `tests/smoke/xpmile-pull-smoke.sh` runs in CI on the
`runc-bundle` job after the tarball is built:

1. `xpmile-pull docker.io/library/alpine:3.19 --to /tmp/alpine-bundle`
2. Assert `/tmp/alpine-bundle/rootfs/etc/alpine-release` contains
   `3.19.`.
3. Assert `/tmp/alpine-bundle/config.json` parses as JSON and has
   `ociVersion`.
4. Re-run with `--force` → succeeds; without `--force` on an
   existing bundle → exits 6.

This *does* hit the real Docker Hub. It's the only test in the
plan that does, and it's outside `offtarget` for exactly that
reason. CI failure here usually means Docker Hub is having a bad
day; the script retries the layer download once.

### H.3 — Manual verification (Pi 3B)

After tarball published:

1. Operator on Pi 3B: `curl -sSf …/install-agent-runc.sh | bash`.
2. `systemctl status xpmile-mongo xpmile-wsdbagent
   xpmile-wsdbagent-idp xpmile-certs.path` — all four active.
3. `./run-agent.sh refresh-certs` — within ~5 s, `journalctl -u
   xpmile-certs-rotate.service` shows the two agents restarted.
4. `ldd /usr/local/bin/xpmile-pull` — only libc + libssl +
   libcrypto + libz.

These are the same three points listed in §13 of the design doc.

---

## Execution order (dependency graph)

```
Phase A (HttpClient extensions) ─┬───────────────────────────────┐
                                  │                               │
                                  ├── Phase B (registry client) ──┤
                                  │                               │
Phase C (manifest/config/digest) ─┤                               ├── Phase F (orchestrator) ── Phase G (CLI) ── Phase H (tarball + smoke)
                                  │                               │
Phase D (tar/gzip/whiteout/      ─┤                               │
        hardening)                │                               │
                                  │                               │
Phase E (bundle writer) ─────────┴───────────────────────────────┘
```

- A blocks B (B uses the extended mock + redirect policy).
- B / C / D / E can run **in parallel** once A is in — they have
  no inter-dependency.
- F needs all of B + C + D + E.
- G needs F (CLI calls the orchestrator).
- H needs G (smoke runs the binary).

A sensible single-developer order is **A → C → D → B → E → F → G → H**
(C and D first because they're pure-function and biggest; B once the
HTTP extension is in; E last among the pre-F components because it's
the smallest). Two developers can parallelise after A.

## Test count summary

| Phase | Tests | Module(s) |
|-------|------:|-----------|
| A. HttpClient extensions | 13 | sso |
| B. Registry client | 29 | runcpull |
| C. Manifest / config / digest | 25 | runcpull |
| D. Tar / gzip / whiteout / hardening | 40 | runcpull |
| E. Bundle writer | 8 | runcpull |
| F. Pull orchestrator | 12 | runcpull |
| G. CLI | 14 | runcpull |
| H. Tarball + smoke | 0 (1 CI script) | — |
| **Total** | **141** | |

The existing 245 SSO + IdP tests stay green throughout. None of the
141 new `offtarget` tests touch a live MongoDB or the real network —
the one test that does (Phase H.2 smoke) lives outside `offtarget`
and is gated on the `runc-bundle` CI job.

## Open questions (resolved before implementation starts)

None at design sign-off time. Re-list here if anything surfaces:

> _(empty)_
