# runc-pull — how to use it

A `runcpull::` C++ library lives in `modules/module/runcpull/` and ships with every offtarget build today. The standalone `xpmile-pull` binary + release-tarball install path are pending (**Phase H**); see the [Implementation status](runc-pull-tdd-plan.md#implementation-status) table.

Until Phase H lands, two paths are available:

1. **As a library** — call `runcpull::PullOrchestrator` from another C++ program in the same build tree.
2. **As a test harness** — the `offtarget` GTest binary exercises every component against canned manifest + tar fixtures (no real network).

This doc covers both, and previews the binary path the operator will use after Phase H.

---

## 1. Library use (today)

### Wiring

Add to the consuming target's `CMakeLists.txt`:

```cmake
add_executable(your_target your.cpp
    modules/module/runcpull/src/registry_client.cpp
    modules/module/runcpull/src/manifest.cpp
    modules/module/runcpull/src/image_config.cpp
    modules/module/runcpull/src/digest.cpp
    modules/module/runcpull/src/layer_unpack.cpp
    modules/module/runcpull/src/bundle_writer.cpp
    modules/module/runcpull/src/pull_orchestrator.cpp
    modules/module/runcpull/src/cli.cpp
    modules/module/sso/src/sso_http_client.cpp)        # extended in Phase A

target_include_directories(your_target PRIVATE
    modules/module/runcpull/inc
    modules/module/sso/inc
    modules/module/thirdparty)

target_link_libraries(your_target pthread ssl crypto z)
# NO ACE, NO mongo-cxx-driver, NO xmlsec — same trim as planned for xpmile-pull.
```

### End-to-end pull in 15 lines

```cpp
#include "cli.hpp"
#include "pull_orchestrator.hpp"
#include "registry_client.hpp"
#include "sso_http_client.hpp"

int main(int argc, char *argv[]) {
  auto parsed = runcpull::parse_args(argc, argv);
  if (parsed.error != runcpull::CliParseError::NONE) {
    std::cerr << runcpull::format_usage();
    return runcpull::cli_parse_error_to_exit_code(parsed.error);
  }
  if (parsed.args.help)    { std::cout << runcpull::format_usage();   return 0; }
  if (parsed.args.version) { std::cout << runcpull::format_version(); return 0; }

  auto ref = runcpull::parse_ref(parsed.args.image);
  if (ref.error != runcpull::RefError::NONE) return 6;

  runcpull::HostProbe probe;
  runcpull::HostArchResult host = runcpull::resolve_host_arch(probe);

  sso::HttpClient http;                         // ACE_SOCK + OpenSSL, no libcurl
  runcpull::PullOrchestrator orch(http, parsed.args.to_pull_options(host));
  auto result = orch.run(ref.ref, parsed.args.to_dir);

  if (result.error == runcpull::PullError::NONE) {
    std::cout << runcpull::format_pull_summary(result, ref.ref, parsed.args.to_dir) << "\n";
  } else {
    std::cerr << "pull failed: " << result.detail << "\n";
  }
  return runcpull::pull_error_to_exit_code(result.error);
}
```

That's effectively the `main.cpp` Phase H ships — there's nothing magical pending; the orchestration is here today.

### Public surface, headers, and entry points

| Concern | Header | Entry point |
|---|---|---|
| Image reference normalisation | `registry_client.hpp` | `runcpull::parse_ref` |
| Bearer / token flow | `registry_client.hpp` | `runcpull::parse_www_authenticate`, `build_token_url`, `parse_token_response` |
| Host architecture | `registry_client.hpp` | `runcpull::HostProbe`, `resolve_host_arch[_from_override]` |
| Per-image HTTP driver | `registry_client.hpp` | `runcpull::RegistryClient` |
| Manifest parsing | `manifest.hpp` | `runcpull::parse_manifest`, `manifest_pick_platform` |
| Image config parsing | `image_config.hpp` | `runcpull::parse_image_config` |
| SHA-256 verification | `digest.hpp` | `runcpull::Digest`, `parse_digest_string` |
| Tar / gzip / unpack | `layer_unpack.hpp` | `runcpull::TarReader`, `GzipInflater`, `LayerUnpacker` |
| Bundle assembly | `bundle_writer.hpp` | `runcpull::BundleWriter` |
| End-to-end orchestration | `pull_orchestrator.hpp` | `runcpull::PullOrchestrator` |
| CLI plumbing | `cli.hpp` | `parse_args`, `format_pull_summary`, `pull_error_to_exit_code` |

The orchestrator depends on `sso::IHttpClient` (the extended Phase A interface) — in production wire it to `sso::HttpClient`; in tests wire it to a mock that fits the same interface (every Phase B/C/F test does this).

---

## 2. As a test harness — driving the library against fixtures

The full GTest matrix runs inside the existing `offtarget` binary:

```sh
./run.sh build                                         # builds the test image
podman run --rm localhost/xpmile-test:latest \
    ./offtarget --gtest_filter='Manifest*:Tar*:Pull*:Cli*'
```

Useful filters per phase:

| Phase | `--gtest_filter` |
|---|---|
| A | `HttpClient*` (includes the 13 Phase A tests + 2 pre-existing `HttpClientTest` form-encoding tests) |
| B | `RegistryRef*:RegistryToken*:RegistryClient*:HostArch*` |
| C | `Manifest*:ImageConfig*:Digest*` |
| D | `Tar*:Gzip*:Whiteout*:PathTraversal*:FileMode*:Rootless*:UnpackLimit*` |
| E | `BundleWriter*` |
| F | `Pull*` |
| G | `Cli*` |

No real network — every Phase B/F test uses a `MockHttpClient` (anonymous namespace in each `*_test.cc` so link-time isolation is preserved). Tar fixtures are built in-memory by `runcpull_test::InMemoryTarBuilder` (header-only, `modules/module/runcpull/test/in_memory_tar_builder.hpp`). Manifest fixtures are checked in at `modules/module/runcpull/test/fixtures/`.

---

## 3. Operator path — after Phase H ships

Once Phase H lands the operator runs **one binary** instead of the `skopeo` + `umoci` pair `docs/operator-runc.md` currently asks for:

```sh
# Download from a GitHub release (Phase H deliverable):
curl -sSf https://github.com/naushada/xpmile/releases/latest/download/xpmile-runc-bundle-arm64.tar.gz \
   | tar xz -C ~/xpmile-runc

# Then for each image:
~/xpmile-runc/bin/xpmile-pull docker.io/library/mongo:4.4 \
    --to /var/lib/xpmile/bundles/mongo

~/xpmile-runc/bin/xpmile-pull docker.io/naushada/xpmile-wsdbagent:latest \
    --to /var/lib/xpmile/bundles/wsdbagent-marvel
```

Flags accepted today (verified by Phase G's tests):

```
xpmile-pull <image:tag> --to <bundle-dir> [flags]

Required
  <image>              docker.io/<user>/<name>[:<tag>] or <user>/<name>[@digest]
  --to <path>          OCI bundle directory

Optional
  --arch <a>           amd64 / arm64 / arm/v7
  --force              overwrite existing bundle
  --max-entry-size <N> per-entry size limit, suffixes K/M/G/T (e.g. 4G)
  --max-total-size <N> total bundle size limit, same suffixes
  -h, --help           print usage
  -V, --version        print version
```

Exit codes (Phase G):

| Code | Meaning |
|---:|---|
| 0 | success |
| 1 | transport / network failure / blob not found / redirect-limit |
| 2 | auth — token endpoint refused or malformed challenge |
| 3 | manifest parse / unsupported media-type / no matching platform / non-Hub registry |
| 4 | digest mismatch (config or layer) |
| 5 | unpack failure — path traversal, size limit, gzip/tar parse error |
| 6 | usage — missing or invalid argv |

---

## 4. Security invariants — pinned by tests, don't weaken

Every one of these is exercised by a Phase D test you can grep for; they are baseline contract, not nice-to-haves.

- **Server TLS verification on** by default. `sso::HttpClient` uses ACE_SOCK_Connector + OpenSSL with system-CA chain verification + hostname check; no `--insecure` flag.
- **Every blob's SHA-256 is verified** against the manifest's recorded digest before the bytes touch the rootfs. Mismatch → tear down the bundle, exit 4.
- **Path-traversal rejection** in tar entry names (`..` segments, escaping symlinks). Exercised by `PathTraversal*` in `unpack_hardening_test.cc`.
- **No setuid / setgid / sticky bits** on extracted files. `FileMode*` tests.
- **`Authorization` header dropped on cross-origin redirects.** Hub-bearer is for `registry-1.docker.io` only; the CDN doesn't want it. `RegistryClientTest.Blob_FollowsCdnRedirect_DropsBearer` enforces this.
- **Anonymous-only auth in v1.** No env vars, no `~/.docker/config.json` parse. Adding private-repo support later means a dedicated `--auth-token-file` flag — never silently picking up shell env.

---

## 5. Pointers

- Design: [`runc-pull-design.md`](runc-pull-design.md)
- TDD plan + implementation status: [`runc-pull-tdd-plan.md`](runc-pull-tdd-plan.md)
- Operator install path (still on skopeo + umoci until Phase H): [`../../operator-runc.md`](../../operator-runc.md)
- Top-level project README: [`../../../README.md`](../../../README.md)
