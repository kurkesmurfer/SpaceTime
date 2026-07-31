# VCV Rack release builds

SpaceTime uses the official VCV Rack Plugin Toolchain to cross-compile the
same source tree for Linux x64, Windows x64, macOS x64, and macOS arm64.

## One-time toolchain setup

1. Install and start Docker Desktop. In **Settings > Resources > Advanced**,
   allocate 24 GB of memory to Docker for the initial toolchain image build.
2. Clone the `v2` toolchain branch without spaces in its path:

   ```sh
   git clone --branch v2 https://github.com/VCVRack/rack-plugin-toolchain.git \
     ~/Development/rack-plugin-toolchain
   ```

3. Generate the exact `MacOSX12.3.sdk.tar.xz` requested by the toolchain and
   place it in `~/Development/rack-plugin-toolchain/`.
4. Build the Docker image. This takes several hours on the first run and
   roughly 15 GB of working space:

   ```sh
   cd ~/Development/rack-plugin-toolchain
   JOBS=2 make docker-build
   ```

## Build and verify a release

From the repository root:

```sh
./check.sh
make toolchain JOBS=2
```

The four packages are written to
`~/Development/rack-plugin-toolchain/plugin-build/`. Before tagging a release,
check that the version in `vcv/plugin.json` is correct and test the x64 macOS
package in Rack on an Intel Mac. Windows and Linux should receive equivalent
Rack load and basic audio/MIDI smoke tests.

## Publish a release candidate

Release-candidate packages belong on a GitHub prerelease until every platform
has passed the smoke tests above. Upload all four `.vcvplugin` files plus a
SHA-256 checksum file. Release notes must identify which packages have been
tested natively and warn users to back up important patches.

Do not mark a GitHub release as final or submit it to the VCV Library while any
platform package remains unverified.
