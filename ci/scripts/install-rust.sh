#! /bin/bash

set -eux

# Install Rust
RUSTUP_VERSION=1.27.1
RUST_VERSION=1.83.0
RUST_ARCH="x86_64-unknown-linux-gnu"

RUSTUP_URL=https://static.rust-lang.org/rustup/archive/$RUSTUP_VERSION/$RUST_ARCH/rustup-init
curl -o rustup-init $RUSTUP_URL

export RUSTUP_HOME="/usr/local/rustup"
export CARGO_HOME="/usr/local/cargo"
export PATH="/usr/local/cargo/bin:$PATH"

chmod +x rustup-init;
./rustup-init -y --no-modify-path --default-toolchain $RUST_VERSION;
rm rustup-init;
chmod -R a+w $RUSTUP_HOME $CARGO_HOME

cargo install --locked cargo-c --version 0.10.7+cargo-0.84.0

rustup --version
cargo --version
rustc --version
cargo cinstall --version

# Cleanup the registry after install
# so we don't have to save 200mb of the index in the ci image
rm -rf "$CARGO_HOME/registry"
